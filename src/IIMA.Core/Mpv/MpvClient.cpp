#include "MpvClient.h"
#include "MpvNodes.h"
#include "SeekLatency.h"
#include <Unknwn.h>
#include <Windows.h>
#include "include/mpv/client.h"
#include <atomic>
#include <algorithm>
#include <utility>
#include <deque>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <string_view>
#include <filesystem>

static_assert(sizeof(void*) == 8, "IIMA libmpv integration requires the x64 pointer ABI");
static_assert(sizeof(int) == 4 && sizeof(std::int64_t) == 8);
static_assert(MPV_CLIENT_API_VERSION == ((2UL << 16) | 6UL), "Review pinned C ABI before updating");

namespace iima {
namespace {
struct Api {
    HMODULE module{};
    std::shared_ptr<void> moduleLease;
    using AcquireSwapChain = int (*)(mpv_handle*, void**);
    AcquireSwapChain acquireSwapChain{};
#define API(name) decltype(&mpv_##name) name{};
    API(client_api_version) API(create) API(initialize) API(terminate_destroy)
    API(request_log_messages) API(set_option_string) API(command_async) API(observe_property) API(wait_event)
    API(error_string) API(set_property_string) API(get_property_async) API(get_property) API(free_node_contents) API(set_property_async) API(free)
#undef API
    ~Api() = default;
    void Load(std::wstring const& path) {
        if (!std::filesystem::path(path).is_absolute()) throw std::runtime_error("libmpv path must be absolute");
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) throw std::runtime_error("Cannot load app-local libmpv-2.dll or its dependencies (Win32 " + std::to_string(GetLastError()) + ")");
        moduleLease=std::shared_ptr<void>(module,[](void* p) { FreeLibrary(static_cast<HMODULE>(p)); });
#define API(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(module, "mpv_" #name)); if (!name) throw std::runtime_error("Missing libmpv export: mpv_" #name);
        API(client_api_version) API(create) API(initialize) API(terminate_destroy)
        API(request_log_messages) API(set_option_string) API(command_async) API(observe_property) API(wait_event) API(error_string) API(set_property_string) API(get_property_async) API(get_property) API(free_node_contents) API(set_property_async) API(free)
#undef API
        acquireSwapChain = reinterpret_cast<AcquireSwapChain>(GetProcAddress(module,"mpv_iima_acquire_d3d11_swapchain_v1"));
        const auto version = client_api_version();
        if ((version >> 16) != (MPV_CLIENT_API_VERSION >> 16) || version < MPV_CLIENT_API_VERSION)
            throw std::runtime_error("libmpv C API is incompatible with pinned headers");
    }
};
}
struct MpvClient::State {
    explicit State(Sink s) : sink(std::move(s)) {}
    Sink sink;
    MpvLogBuffer logs;
    std::mutex mutex;
    struct Request {
        std::uint64_t id;
        std::vector<std::string> arguments;
        bool playlistQuery{};
        bool watchedPropertiesQuery{};
        bool audioFilters{};
        std::optional<FilterEditPlan> filterEdit;
        std::optional<std::int64_t> filterEntry;
        std::string filterPath;
        bool measureSeek{};
        bool videoGeometryQuery{};
        bool windowScaleQuery{};
        bool pauseQuery{};
        bool subtitleStateQuery{};
        std::optional<std::string> recoverAudioDevice;
    };
    std::deque<Request> commands;
    std::vector<std::function<void()>> closedCallbacks;
    bool started{}, closing{}, closed{};
    std::atomic_bool pauseWhenOpen{};
    std::uint64_t nextId{1};
    void Emit(MpvEvent e) noexcept { try { sink(std::move(e)); } catch (...) {} }
    void Run(std::wstring const& path) noexcept {
        Api api;
        mpv_handle* handle{};
        try {
            api.Load(path);
            handle = api.create();
            if (!handle) throw std::runtime_error("mpv_create failed");
            auto check = [&](int code, char const* operation) {
                if (code < 0) throw std::runtime_error(std::string(operation) + ": " + api.error_string(code));
            };
            // No global config/scripts/watch-later. mpv remains the state authority.
            for (auto const& [name, value] : std::vector<std::pair<char const*,char const*>>{
                {"config","no"}, {"load-scripts","no"}, {"idle","yes"}, {"terminal","no"},
                {"input-default-bindings","no"}, {"input-vo-keyboard","no"}, {"osc","no"},
                // The native host owns per-window system/display sleep policy.
                {"stop-screensaver","no"},
                {"vo","gpu-next"}, {"gpu-api","d3d11"}, {"gpu-context","d3d11"},
                {"d3d11-output-mode","composition"}, {"d3d11-composition-size","1280x720"},
                {"pause","yes"}, {"keep-open","yes"}, {"save-position-on-quit","no"}})
                check(api.set_option_string(handle, name, value), name);
            check(api.initialize(handle), "mpv_initialize");
            std::uint64_t id = 1;
            for (auto const* name : {"time-pos", "duration", "time-remaining", "playtime-remaining", "volume", "speed", "volume-max", "audio-delay", "sub-delay", "secondary-sub-delay", "sub-pos", "secondary-sub-pos", "sub-scale", "secondary-sub-scale", "ab-loop-a", "ab-loop-b", "video-bitrate", "audio-bitrate", "file-size", "window-scale"})
                check(api.observe_property(handle, id++, name, MPV_FORMAT_DOUBLE), name);
            for (auto const* name : {"pause", "paused-for-cache", "demuxer-via-network", "vo-configured", "mute", "sub-visibility", "secondary-sub-visibility", "seekable"})
                check(api.observe_property(handle, id++, name, MPV_FORMAT_FLAG), name);
            for(auto const* name : {"path", "mpv-version", "current-tracks/video/codec-desc", "hwdec", "hwdec-current", "current-vo", "loop-file", "loop-playlist", "keep-open", "ab-loop-count", "media-title", "audio-device", "deinterlace", "sub-codepage", "sub-font", "sub-font-size", "sub-bold", "sub-italic", "sub-color", "sub-outline-color", "sub-outline-size", "sub-back-color"})
                check(api.observe_property(handle,id++,name,MPV_FORMAT_STRING),name);
            for(auto const* name : {"playlist", "track-list", "chapter-list", "metadata", "video-params", "video-out-params", "osd-dimensions", "audio-params", "audio-device-list", "vf", "af"})
                check(api.observe_property(handle,id++,name,MPV_FORMAT_NODE),name);
            // IINA MPVController observes selection/chapter/equalizers as
            // INT64. Selection has mpv's -2=no/-1=auto sentinels; equalizers
            // use -100..100. Unavailable observations remain unknown.
            for (auto const* name : {"cache-speed", "cache-buffering-state", "demuxer-cache-state/fw-bytes", "video-rotate", "chapter", "vid", "aid", "sid", "secondary-sid", "brightness", "contrast", "saturation", "gamma", "hue"})
                check(api.observe_property(handle, id++, name, MPV_FORMAT_INT64), name);
            MpvEvent ready{MpvEvent::Kind::Ready}; ready.flag=api.acquireSwapChain!=nullptr; ready.integer=api.client_api_version(); Emit(std::move(ready));
            std::uint64_t inFlight{};
            bool inFlightVideoGeometry{};
            bool inFlightWindowScale{};
            bool inFlightPause{};
            SeekLatency seekLatency;
            auto reportSeek = [&](std::optional<SeekLatencyResult> sample) {
                if (!sample) return;
                MpvEvent event{MpvEvent::Kind::SeekMeasurement};
                event.requestId = sample->request; event.number = sample->seconds;
                Emit(std::move(event));
            };
            std::string inFlightFilter;
            auto readFilterResult = [&](MpvEvent& event, std::string const& name) {
                event.name = name;
                mpv_node value{};
                auto result = api.get_property(handle,name.c_str(),MPV_FORMAT_NODE,&value);
                if (result >= 0) {
                    event.filters = ParseFilterChain(value);
                    api.free_node_contents(&value);
                } else event.filters.error = api.error_string(result);
                std::int64_t position{-1};
                if (api.get_property(handle,"playlist-playing-pos",MPV_FORMAT_INT64,&position) >= 0 && position >= 0) {
                    auto idProperty = "playlist/" + std::to_string(position) + "/id";
                    api.get_property(handle,idProperty.c_str(),MPV_FORMAT_INT64,&event.entryId);
                }
                char* rawPath{};
                if (api.get_property(handle,"path",MPV_FORMAT_STRING,&rawPath) >= 0 && rawPath)
                    event.properties["path"] = rawPath;
                std::unique_ptr<char,decltype(api.free)> path(rawPath,api.free);
            };
            std::uint64_t logEpoch{};
            bool done = false;
            while (!done) {
                // Latest intent slot, independent of the in-flight command lane.
                // "no" destroys mpv's native log buffer (player/client.c), so a
                // new start/clear cannot replay records from an earlier epoch.
                auto desired=logs.Desired();
                if (desired.first!=logEpoch) {
                    auto error=api.request_log_messages(handle,"no");
                    if (error>=0 && desired.second!="no") error=api.request_log_messages(handle,desired.second.c_str());
                    logEpoch=desired.first;logs.Complete(logEpoch,error);
                }
                std::optional<Request> pending;
                { std::lock_guard lock(mutex); if (closing) break;
                  if(!inFlight && !commands.empty()) { pending=std::move(commands.front()); commands.pop_front(); } }
                // client.h explicitly permits async command reordering. Serialize at replies.
                if(pending) {
                    const auto request=pending->id;
                    if (!pending->playlistQuery && !pending->watchedPropertiesQuery && !pending->videoGeometryQuery && !pending->windowScaleQuery && !pending->pauseQuery && !pending->subtitleStateQuery) {
                        reportSeek(seekLatency.Cancel());
                        if (pending->measureSeek) seekLatency.Begin(request);
                    }
                    inFlightFilter.clear();
                    inFlightVideoGeometry = pending->videoGeometryQuery;
                    inFlightWindowScale = pending->windowScaleQuery;
                    inFlightPause = pending->pauseQuery;
                    int error{};
                    if (pending->subtitleStateQuery) {
                        MpvEvent result{MpvEvent::Kind::SubtitleStateReply};
                        result.requestId=request;
                        // Synchronous GETs run only after the previous lane reply.
                        // No async request is outstanding and this branch never
                        // claims inFlight; both success/failure emit one owned reply.
                        try {
                            auto require=[&](int code) {
                                if(code<0) { result.error=code; throw std::runtime_error(api.error_string(code)); }
                            };
                            auto identity=[&]() {
                                std::int64_t position{-1},entry{-1};
                                require(api.get_property(handle,"playlist-playing-pos",MPV_FORMAT_INT64,&position));
                                if(position<0) throw std::runtime_error("No current media entry for subtitle state");
                                const auto property="playlist/"+std::to_string(position)+"/id";
                                require(api.get_property(handle,property.c_str(),MPV_FORMAT_INT64,&entry));
                                if(entry<0) throw std::runtime_error("Invalid current media entry for subtitle state");
                                char* raw{};
                                const auto code=api.get_property(handle,"path",MPV_FORMAT_STRING,&raw);
                                std::unique_ptr<char,decltype(api.free)> owned(raw,api.free);
                                require(code);
                                std::size_t size{};
                                if(raw) while(size<=131072 && raw[size]) ++size;
                                if(!raw || !size || size>131072) throw std::runtime_error("Invalid media path for subtitle state");
                                return std::pair{entry,std::string(raw,size)};
                            };
                            const auto before=identity();
                            mpv_node node{};
                            const auto code=api.get_property(handle,"track-list",MPV_FORMAT_NODE,&node);
                            auto release=[&](mpv_node* value){api.free_node_contents(value);};
                            std::unique_ptr<mpv_node,decltype(release)> owned(code>=0 ? &node : nullptr,release);
                            require(code);
                            if(!CopySubtitleStateTracks(result,node))
                                throw std::runtime_error("Subtitle track state is malformed, ambiguous or exceeds limits");
                            auto slot=[&](char const* name) {
                                std::int64_t value{-1};
                                require(api.get_property(handle,name,MPV_FORMAT_INT64,&value));
                                // mp_property_switch_track GET: initialized off=-2,
                                // uninitialized auto=-1. Never reinterpret auto as off.
                                if(value==-2) return std::int64_t{0};
                                if(value<=0 || std::none_of(result.tracks.begin(),result.tracks.end(),
                                    [&](auto const& track){return track.id==value;}))
                                    throw std::runtime_error("Subtitle selection does not match fresh tracks");
                                return value;
                            };
                            result.primarySubtitle=slot("sid");
                            result.secondarySubtitle=slot("secondary-sid");
                            if(*result.primarySubtitle && result.primarySubtitle==result.secondarySubtitle)
                                throw std::runtime_error("Ambiguous duplicate subtitle selections");
                            const auto after=identity();
                            if(before!=after) throw std::runtime_error("Media changed during subtitle state read");
                            result.entryId=after.first;
                            result.properties["path"]=after.second;
                            result.subtitleStateComplete=true;
                            // Separate GETs are not an atomic snapshot. A change
                            // after these reads still requires Session intent guards.
                        } catch(std::exception const& failure) {
                            if(result.error>=0) result.error=MPV_ERROR_PROPERTY_ERROR;
                            result.text=failure.what();
                            result.tracks.clear();result.primarySubtitle.reset();result.secondarySubtitle.reset();
                            result.properties.clear();result.entryId=-1;result.subtitleStateComplete=false;
                        }
                        bool cancelled{};
                        { std::lock_guard lock(mutex); cancelled=closing; }
                        if(!cancelled) Emit(std::move(result));
                        continue;
                    } else if (pending->watchedPropertiesQuery) {
                        MpvEvent result{MpvEvent::Kind::WatchedPropertiesReply};
                        result.requestId = request;
                        for (auto const& name : pending->arguments) {
                            { std::lock_guard lock(mutex); if (closing) break; }
                            WatchedPropertyValue value;
                            value.name = name;
                            char* rawValue{};
                            const int code = api.get_property(handle, name.c_str(), MPV_FORMAT_STRING, &rawValue);
                            // mpv owns the allocation contract even when copying throws.
                            std::unique_ptr<char, decltype(api.free)> owned(rawValue, api.free);
                            if (code < 0) value.error = api.error_string(code);
                            else if (!rawValue) value.error = "Property returned no string";
                            else {
                                std::size_t length{};
                                while (length < MaxWatchedPropertyValueBytes && rawValue[length]) ++length;
                                value.truncated = length == MaxWatchedPropertyValueBytes && rawValue[length] != '\0';
                                // Do not cut a UTF-8 code point in the display copy.
                                if (value.truncated)
                                    while (length && (static_cast<unsigned char>(rawValue[length]) & 0xc0) == 0x80) --length;
                                value.value.assign(rawValue, length);
                            }
                            result.watchedProperties.push_back(std::move(value));
                        }
                        // Close may cancel the batch midway; no partial success is published.
                        bool cancelled{};
                        { std::lock_guard lock(mutex); cancelled = closing; }
                        if (!cancelled) Emit(std::move(result));
                        continue;
                    } else if (pending->recoverAudioDevice) {
                        char* rawDevice{};
                        error=api.get_property(handle,"audio-device",MPV_FORMAT_STRING,&rawDevice);
                        std::unique_ptr<char,decltype(api.free)> device(rawDevice,api.free);
                        bool matches=false;
                        if (error>=0 && rawDevice) {
                            std::size_t length{};
                            while(length<=4096 && rawDevice[length]) ++length;
                            matches=length<=4096 && std::string_view(rawDevice,length)==*pending->recoverAudioDevice;
                        }
                        MpvEvent list{MpvEvent::Kind::Property}; list.name="audio-device-list";
                        if (error>=0 && matches) {
                            mpv_node node{};
                            error=api.get_property(handle,"audio-device-list",MPV_FORMAT_NODE,&node);
                            auto release=[&](mpv_node* value) { api.free_node_contents(value); };
                            std::unique_ptr<mpv_node,decltype(release)> owned(error>=0 ? &node : nullptr,release);
                            if (error>=0) CopyCollectionProperty(list,node);
                        }
                        const bool missing=matches && list.audioDevicesComplete && !list.audioDevices.empty() &&
                            std::none_of(list.audioDevices.begin(),list.audioDevices.end(),[&](auto const& item) {
                                return item.name==*pending->recoverAudioDevice;
                            });
                        if (error>=0 && !missing) {
                            MpvEvent skipped{MpvEvent::Kind::CommandReply}; skipped.requestId=request;
                            Emit(std::move(skipped)); continue;
                        }
                        if (error>=0) {
                            // UI device selections use this same serialized lane.
                            // A newer selection queued after this check executes
                            // after auto; one queued earlier defeats the compare.
                            char const* args[]{"set","audio-device","auto",nullptr};
                            error=api.command_async(handle,request,args);
                        }
                    } else if (pending->filterEdit) {
                        auto name = pending->audioFilters ? "af" : "vf";
                        inFlightFilter = name;
                        if (pending->filterEntry) {
                            // Bind tracked edits to the actual playing entry, not
                            // a cached UI row. Still not an atomic mpv transaction.
                            std::int64_t position{-1}, entry{-1};
                            error = api.get_property(handle,"playlist-playing-pos",MPV_FORMAT_INT64,&position);
                            if (error >= 0 && position >= 0) {
                                auto property = "playlist/" + std::to_string(position) + "/id";
                                error = api.get_property(handle,property.c_str(),MPV_FORMAT_INT64,&entry);
                            } else error = MPV_ERROR_PROPERTY_UNAVAILABLE;
                            char* rawPath{};
                            if (error >= 0) error = api.get_property(handle,"path",MPV_FORMAT_STRING,&rawPath);
                            std::unique_ptr<char,decltype(api.free)> path(rawPath,api.free);
                            if (error >= 0 && (entry != *pending->filterEntry || !rawPath || pending->filterPath != rawPath))
                                error = MPV_ERROR_PROPERTY_ERROR;
                        }
                        mpv_node fresh{};
                        if (error >= 0) error = api.get_property(handle, name, MPV_FORMAT_NODE, &fresh);
                        if (error >= 0) {
                            auto snapshot = ParseFilterChain(fresh);
                            api.free_node_contents(&fresh);
                            if (!pending->filterEdit->FreshMatches(snapshot)) error = MPV_ERROR_PROPERTY_ERROR;
                            else {
                                FilterNodeView view(pending->filterEdit->after);
                                auto node = view.Get();
                                error = api.set_property_async(handle, request, name, MPV_FORMAT_NODE, &node);
                            }
                        }
                    } else if (pending->pauseQuery) {
                        error=api.get_property_async(handle,request,"pause",MPV_FORMAT_FLAG);
                    } else if (pending->windowScaleQuery) {
                        error=api.get_property_async(handle,request,"window-scale",MPV_FORMAT_DOUBLE);
                    } else if (pending->videoGeometryQuery) {
                        error=api.get_property_async(handle,request,"video-out-params",MPV_FORMAT_NODE);
                    } else if(pending->playlistQuery) {
                        error=api.get_property_async(handle,request,"playlist",MPV_FORMAT_NODE);
                    } else {
                        std::vector<char const*> argv;
                        for(auto const& arg:pending->arguments) argv.push_back(arg.c_str());
                        argv.push_back(nullptr);
                        error=api.command_async(handle,request,argv.data());
                    }
                    if(error<0) {
                        reportSeek(seekLatency.Reply(request,false));
                        MpvEvent e{pending->pauseQuery ? MpvEvent::Kind::PauseReply : pending->windowScaleQuery ? MpvEvent::Kind::WindowScaleReply :
                            pending->videoGeometryQuery ? MpvEvent::Kind::VideoGeometryReply :
                            pending->playlistQuery ? MpvEvent::Kind::PlaylistReply : MpvEvent::Kind::CommandReply};
                        e.requestId=request; e.error=error; e.text=api.error_string(error);
                        if(pending->playlistQuery) e.name="playlist";
                        if(pending->videoGeometryQuery) e.name="video-out-params";
                        if(pending->windowScaleQuery) e.name="window-scale";
                        if (!inFlightFilter.empty()) { readFilterResult(e,inFlightFilter); inFlightFilter.clear(); }
                        Emit(std::move(e));
                    } else inFlight=request;
                }
                // Bounded wait permits shutdown without calling the handle concurrently.
                auto const* raw = api.wait_event(handle, 0.05);
                MpvEvent e{MpvEvent::Kind::Property};
                e.requestId = raw->reply_userdata; e.error = raw->error;
                if (e.error < 0) e.text = api.error_string(e.error);
                switch (raw->event_id) {
                case MPV_EVENT_NONE: continue;
                case MPV_EVENT_LOG_MESSAGE:
                    if (raw->data) {
                        auto const* message=static_cast<mpv_event_log_message const*>(raw->data);
                        try { logs.Append(logEpoch,message->log_level,message->prefix,message->text); }
                        catch (...) { logs.Overflow(logEpoch); } // Diagnostic allocation failure must not stop playback.
                    }
                    continue; // Never send raw content to the ordinary sink/disk logger.
                case MPV_EVENT_START_FILE: e.kind=MpvEvent::Kind::StartFile; if(raw->data) e.entryId=static_cast<mpv_event_start_file*>(raw->data)->playlist_entry_id; break;
                case MPV_EVENT_FILE_LOADED: e.kind=MpvEvent::Kind::Loaded; break;
                case MPV_EVENT_END_FILE: {
                    e.kind=MpvEvent::Kind::EndFile;
                    auto const& end=*static_cast<mpv_event_end_file*>(raw->data);
                    e.entryId=end.playlist_entry_id; e.reason=end.reason; e.error=end.error;
                    if (end.playlist_insert_num_entries > 0) e.insertedEntryId=end.playlist_insert_id;
                    if(end.error<0) e.text=api.error_string(end.error); break;
                }
                case MPV_EVENT_SEEK: e.kind=MpvEvent::Kind::Seek; break;
                case MPV_EVENT_PLAYBACK_RESTART: e.kind=MpvEvent::Kind::Restart; break;
                case MPV_EVENT_VIDEO_RECONFIG: e.kind=MpvEvent::Kind::VideoReconfig; break;
                case MPV_EVENT_SET_PROPERTY_REPLY:
                    e.kind = MpvEvent::Kind::CommandReply;
                    if (e.requestId == inFlight) {
                        if (!inFlightFilter.empty()) { readFilterResult(e,inFlightFilter); inFlightFilter.clear(); }
                        inFlight = 0;
                    }
                    break;
                case MPV_EVENT_COMMAND_REPLY: {
                    e.kind=MpvEvent::Kind::CommandReply; if(e.requestId==inFlight) inFlight=0;
                    if(raw->data) {
                        auto const& result=static_cast<mpv_event_command*>(raw->data)->result;
                        if(result.format==MPV_FORMAT_NODE_MAP && result.u.list) {
                            for(int i=0;i<result.u.list->num;++i)
                                if(std::string_view(result.u.list->keys[i])=="playlist_entry_id" && result.u.list->values[i].format==MPV_FORMAT_INT64)
                                    e.entryId=result.u.list->values[i].u.int64;
                        }
                    }
                    break;
                }
                case MPV_EVENT_GET_PROPERTY_REPLY: {
                    auto property = static_cast<mpv_event_property const*>(raw->data);
                    const bool pause = (property && property->name && std::string_view(property->name)=="pause") ||
                        (e.requestId==inFlight && inFlightPause);
                    if (pause) {
                        e.kind=MpvEvent::Kind::PauseReply; e.name="pause";
                        if(e.requestId==inFlight) { inFlight=0; inFlightPause=false; }
                        if(e.error>=0 && property && property->name && std::string_view(property->name)=="pause" &&
                            property->format==MPV_FORMAT_FLAG && property->data) e.flag=*static_cast<int const*>(property->data)!=0;
                        else if(e.error>=0) e.error=MPV_ERROR_PROPERTY_FORMAT;
                        break;
                    }
                    const bool windowScale = (property && property->name &&
                        std::string_view(property->name)=="window-scale") ||
                        (e.requestId==inFlight && inFlightWindowScale);
                    if (windowScale) {
                        e.kind=MpvEvent::Kind::WindowScaleReply; e.name="window-scale";
                        if(e.requestId==inFlight) { inFlight=0; inFlightWindowScale=false; }
                        if (e.error>=0 && property && property->name &&
                            std::string_view(property->name)=="window-scale" &&
                            property->format==MPV_FORMAT_DOUBLE && property->data)
                            e.number=*static_cast<double const*>(property->data);
                        else if(e.error>=0) e.error=MPV_ERROR_PROPERTY_FORMAT;
                        break;
                    }
                    const bool videoGeometry = (property && property->name &&
                        std::string_view(property->name)=="video-out-params") ||
                        (e.requestId==inFlight && inFlightVideoGeometry);
                    e.kind=videoGeometry ? MpvEvent::Kind::VideoGeometryReply : MpvEvent::Kind::PlaylistReply;
                    e.name=videoGeometry ? "video-out-params" : "playlist";
                    if(e.requestId==inFlight) { inFlight=0; inFlightVideoGeometry=false; }
                    bool valid=false;
                    if(e.error>=0 && property && property->name && e.name==property->name &&
                       property->format==MPV_FORMAT_NODE && property->data) {
                        auto const& node=*static_cast<mpv_node const*>(property->data);
                        if (videoGeometry) {
                            if (node.format==MPV_FORMAT_NODE_MAP && node.u.list && node.u.list->num>0) {
                                CopyCollectionProperty(e,node);
                                valid=!e.properties.empty();
                            }
                        } else if(node.format==MPV_FORMAT_NODE_ARRAY && node.u.list && node.u.list->num>=0) {
                            CopyCollectionProperty(e,node);
                            valid=e.playlist.size()==static_cast<std::size_t>(node.u.list->num);
                        }
                    }
                    if(e.error>=0 && !valid) {
                        e.error=MPV_ERROR_PROPERTY_FORMAT;
                        e.text=videoGeometry ? "Fresh video geometry query returned an invalid node map" :
                            "Fresh playlist query returned an invalid node array";
                        e.playlist.clear(); e.properties.clear();
                    }
                    break;
                }
                case MPV_EVENT_SHUTDOWN: e.kind=MpvEvent::Kind::Shutdown; done=true; break;
                case MPV_EVENT_QUEUE_OVERFLOW: e.kind=MpvEvent::Kind::Failure; e.text="libmpv event queue overflow; playback state cannot be trusted"; done=true; break;
                case MPV_EVENT_PROPERTY_CHANGE: {
                    auto const& p=*static_cast<mpv_event_property*>(raw->data);
                    e.name=p.name ? p.name : "";
                    if(p.data && p.format==MPV_FORMAT_NODE) CopyCollectionProperty(e,*static_cast<mpv_node*>(p.data));
                    if(p.data && p.format==MPV_FORMAT_INT64) e.integer=*static_cast<std::int64_t*>(p.data);
                    if(p.data && p.format==MPV_FORMAT_DOUBLE) e.number=*static_cast<double*>(p.data);
                    if(p.data && p.format==MPV_FORMAT_FLAG) e.flag=*static_cast<int*>(p.data)!=0;
                    if(p.data && p.format==MPV_FORMAT_STRING) {
                        auto text=*static_cast<char**>(p.data);
                        if(text) {
                            // New metadata-bearing properties are bounded before
                            // allocation, not by resizing an already huge copy.
                            const std::size_t limit=e.name=="media-title" ? 16384 :
                                e.name=="audio-device" ? 4096 : 256*1024;
                            std::size_t length{};
                            while(length<=limit && text[length]) ++length;
                            if(length<=limit) e.text.assign(text,length);
                        }
                    }
                    break;
                }
                default: continue;
                }
                // IINA MPVController.handleEvent applies pauseWhenOpen at END_FILE
                // on the event queue, before UI dispatch, to minimize the next-item race.
                if(pauseWhenOpen.load() && (e.kind==MpvEvent::Kind::Loaded ||
                    (e.kind==MpvEvent::Kind::EndFile && e.reason!=MPV_END_FILE_REASON_STOP))) {
                    const int result=api.set_property_string(handle,"pause","yes");
                    if(result<0) { MpvEvent failure{MpvEvent::Kind::CommandReply}; failure.error=result;
                        failure.text=std::string("Pause on open: ")+api.error_string(result); Emit(std::move(failure)); }
                }
                if(api.acquireSwapChain && (e.kind==MpvEvent::Kind::VideoReconfig || e.kind==MpvEvent::Kind::Loaded)) {
                    void* chain{};
                    const int result=api.acquireSwapChain(handle,&chain);
                    if(result>=0 && chain)
                        e.swapChain=std::shared_ptr<void>(chain,[lease=api.moduleLease](void* p) { static_cast<IUnknown*>(p)->Release(); });
                    else e.text=std::string("Composition output is not ready: ") + (result<0 ? api.error_string(result) : "no swapchain returned");
                }
                // Timestamp before UI dispatch; UI queue delay must not select
                // exact versus keyframe seeking. Require a successful reply too.
                if (e.kind == MpvEvent::Kind::Seek) reportSeek(seekLatency.Seek(SeekLatency::Clock::now()));
                else if (e.kind == MpvEvent::Kind::Restart) reportSeek(seekLatency.Restart(SeekLatency::Clock::now()));
                else if (e.kind == MpvEvent::Kind::CommandReply) reportSeek(seekLatency.Reply(e.requestId,e.error >= 0));
                else if (e.kind == MpvEvent::Kind::StartFile || e.kind == MpvEvent::Kind::EndFile ||
                         e.kind == MpvEvent::Kind::Shutdown) reportSeek(seekLatency.Cancel());
                const bool loaded = e.kind == MpvEvent::Kind::Loaded;
                Emit(std::move(e));
                if (loaded) {
                    // START_FILE clears media-scoped selection. On same-file
                    // replay mpv may suppress unchanged observer values, so
                    // query both actual slots after FILE_LOADED on this worker.
                    // This is not an atomic pair; later observations remain
                    // authoritative if scripts change tracks concurrently.
                    // Re-read persistent loop options after clearing the old media
                    // snapshot; unchanged values may not emit an observer event.
                    for (auto const* name : {"ab-loop-a", "ab-loop-b"}) {
                        MpvEvent point{MpvEvent::Kind::Property};
                        point.name = name;
                        double value{};
                        point.error = api.get_property(handle, name, MPV_FORMAT_DOUBLE, &value);
                        if (point.error >= 0) point.number = value;
                        Emit(std::move(point));
                    }
                    {
                        MpvEvent count{MpvEvent::Kind::Property};
                        count.name = "ab-loop-count";
                        char* value{};
                        count.error = api.get_property(handle, "ab-loop-count", MPV_FORMAT_STRING, &value);
                        std::unique_ptr<char, decltype(api.free)> owned(value, api.free);
                        if (count.error >= 0 && value) count.text = value;
                        Emit(std::move(count));
                    }
                    for (auto const* name : {"vid", "aid", "sid", "secondary-sid"}) {
                        MpvEvent selection{MpvEvent::Kind::Property};
                        selection.name = name;
                        std::int64_t value{};
                        selection.error = api.get_property(handle, name, MPV_FORMAT_INT64, &value);
                        if (selection.error >= 0) selection.integer = value;
                        Emit(std::move(selection));
                    }
                }
            }
        } catch (std::exception const& error) { MpvEvent e{MpvEvent::Kind::Failure}; e.text=error.what(); Emit(std::move(e)); }
        catch (...) { MpvEvent e{MpvEvent::Kind::Failure}; e.text="Unexpected libmpv worker failure"; Emit(std::move(e)); }
        logs.Close();
        if(handle) api.terminate_destroy(handle);
        // Release worker DLL ownership. Queued COM owners retain their own DLL lease.
        api.moduleLease.reset(); api.module=nullptr;
        std::vector<std::function<void()>> callbacks;
        { std::lock_guard lock(mutex); closed=true; closing=true; commands.clear(); callbacks.swap(closedCallbacks); }
        for(auto& callback : callbacks) { try { callback(); } catch(...) {} }
    }
};
MpvClient::MpvClient(Sink sink) : state_(std::make_shared<State>(std::move(sink))) {}
MpvClient::~MpvClient() { CloseAsync(); }
void MpvClient::SetPauseWhenOpen(bool value) { state_->pauseWhenOpen.store(value); }
bool MpvClient::StartMpvLogCapture(std::string level) {
    std::lock_guard lock(state_->mutex);
    if (!state_->started || state_->closing || state_->closed) return false;
    return state_->logs.Start(std::move(level));
}
void MpvClient::StopMpvLogCapture() {
    std::lock_guard lock(state_->mutex);
    if (state_->started && !state_->closing && !state_->closed) state_->logs.Stop();
}
void MpvClient::ClearMpvLogCapture() {
    std::lock_guard lock(state_->mutex);
    if (state_->started && !state_->closing && !state_->closed) state_->logs.Clear();
}
MpvLogSnapshot MpvClient::SnapshotMpvLogs() const { return state_->logs.Snapshot(); }
void MpvClient::Start(std::wstring path) {
    std::lock_guard lock(state_->mutex);
    if(state_->started || state_->closing) return;
    state_->started=true;
    try { std::thread([state=state_, path=std::move(path)] { state->Run(path); }).detach(); }
    catch(...) { state_->started=false; throw; }
}
std::uint64_t MpvClient::Command(std::vector<std::string> args, bool measureSeek) {
    std::lock_guard lock(state_->mutex);
    if(state_->closing || !state_->started || args.empty()) return 0;
    if (measureSeek && (args.size() != 3 || args[0] != "seek" ||
        (args[2] != "relative" && args[2] != "relative+exact"))) return 0;
    auto id=state_->nextId++;
    State::Request request{id,std::move(args),false}; request.measureSeek = measureSeek;
    state_->commands.push_back(std::move(request)); return id;
}
std::uint64_t MpvClient::GetPlaylistAsync() {
    std::lock_guard lock(state_->mutex);
    if(state_->closing || !state_->started) return 0;
    const auto id=state_->nextId++;
    state_->commands.push_back({id,{},true});
    return id;
}
std::uint64_t MpvClient::GetVideoGeometryAsync() {
    std::lock_guard lock(state_->mutex);
    if(state_->closing || !state_->started || state_->commands.size() >= 256) return 0;
    State::Request request{state_->nextId++, {}, false};
    request.videoGeometryQuery = true;
    const auto id = request.id;
    state_->commands.push_back(std::move(request));
    return id;
}
std::uint64_t MpvClient::ReadSubtitleStateAsync() {
    std::lock_guard lock(state_->mutex);
    if(state_->closing || !state_->started || state_->commands.size()>=256) return 0;
    State::Request request{state_->nextId++,{},false};request.subtitleStateQuery=true;
    const auto id=request.id;state_->commands.push_back(std::move(request));return id;
}
std::uint64_t MpvClient::GetPauseAsync() {
    std::lock_guard lock(state_->mutex);
    if(state_->closing || !state_->started || state_->commands.size()>=256) return 0;
    State::Request request{state_->nextId++,{},false};request.pauseQuery=true;
    const auto id=request.id;state_->commands.push_back(std::move(request));return id;
}
std::uint64_t MpvClient::GetWindowScaleAsync() {
    std::lock_guard lock(state_->mutex);
    if(state_->closing || !state_->started || state_->commands.size() >= 256) return 0;
    State::Request request{state_->nextId++, {}, false};
    request.windowScaleQuery = true;
    const auto id = request.id;
    state_->commands.push_back(std::move(request));
    return id;
}
std::uint64_t MpvClient::RecoverAudioDeviceAsync(std::string expectedDevice) {
    if(expectedDevice.empty() || expectedDevice=="auto" || expectedDevice.size()>4096 ||
        expectedDevice.find('\0')!=std::string::npos) return 0;
    std::lock_guard lock(state_->mutex);
    if(state_->closing || !state_->started || state_->commands.size()>=256) return 0;
    State::Request request{state_->nextId++,{},false};
    request.recoverAudioDevice=std::move(expectedDevice);
    const auto id=request.id;
    state_->commands.push_back(std::move(request));
    return id;
}
std::uint64_t MpvClient::ReadWatchedPropertiesAsync(std::vector<std::string> names) {
    if (names.empty() || names.size() > MaxWatchedProperties) return 0;
    for (auto const& name : names) if (!ValidateWatchedPropertyName(name)) return 0;
    std::lock_guard lock(state_->mutex);
    if (state_->closing || !state_->started || state_->commands.size() >= 256) return 0;
    State::Request request{state_->nextId++, std::move(names), false};
    request.watchedPropertiesQuery = true;
    const auto id = request.id;
    state_->commands.push_back(std::move(request));
    return id;
}
std::uint64_t MpvClient::EditFilterChainAsync(bool audio, FilterEditPlan plan,
    std::optional<std::int64_t> expectedEntry, std::string expectedPath) {
    std::lock_guard lock(state_->mutex);
    if (state_->closing || !state_->started || !plan.valid) return 0;
    const auto id = state_->nextId++;
    State::Request request{id, {}, false};
    request.audioFilters = audio;
    request.filterEdit = std::move(plan);
    request.filterEntry = expectedEntry; request.filterPath = std::move(expectedPath);
    state_->commands.push_back(std::move(request));
    return id;
}
void MpvClient::CloseAsync(std::function<void()> completion) {
    state_->logs.Close();
    bool immediate{};
    { std::lock_guard lock(state_->mutex); state_->closing=true;
      immediate=state_->closed || !state_->started;
      if(!immediate && completion) state_->closedCallbacks.push_back(completion); }
    if(immediate && completion) completion();
}
}

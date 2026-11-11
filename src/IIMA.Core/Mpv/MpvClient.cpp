#include "MpvClient.h"
#include "MpvNodes.h"
#include <Unknwn.h>
#include <Windows.h>
#include "include/mpv/client.h"
#include <atomic>
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
    API(set_option_string) API(command_async) API(observe_property) API(wait_event)
    API(error_string) API(set_property_string)
#undef API
    ~Api() = default;
    void Load(std::wstring const& path) {
        if (!std::filesystem::path(path).is_absolute()) throw std::runtime_error("libmpv path must be absolute");
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) throw std::runtime_error("Cannot load app-local libmpv-2.dll or its dependencies (Win32 " + std::to_string(GetLastError()) + ")");
        moduleLease=std::shared_ptr<void>(module,[](void* p) { FreeLibrary(static_cast<HMODULE>(p)); });
#define API(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(module, "mpv_" #name)); if (!name) throw std::runtime_error("Missing libmpv export: mpv_" #name);
        API(client_api_version) API(create) API(initialize) API(terminate_destroy)
        API(set_option_string) API(command_async) API(observe_property) API(wait_event) API(error_string) API(set_property_string)
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
    std::mutex mutex;
    std::deque<std::pair<std::uint64_t, std::vector<std::string>>> commands;
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
                {"vo","gpu-next"}, {"gpu-api","d3d11"}, {"gpu-context","d3d11"},
                {"d3d11-output-mode","composition"}, {"d3d11-composition-size","1280x720"},
                {"pause","yes"}, {"keep-open","yes"}, {"save-position-on-quit","no"}})
                check(api.set_option_string(handle, name, value), name);
            check(api.initialize(handle), "mpv_initialize");
            std::uint64_t id = 1;
            for (auto const* name : {"time-pos", "duration", "volume", "speed", "volume-max", "audio-delay", "sub-delay"})
                check(api.observe_property(handle, id++, name, MPV_FORMAT_DOUBLE), name);
            for (auto const* name : {"pause", "paused-for-cache", "vo-configured", "mute", "sub-visibility"})
                check(api.observe_property(handle, id++, name, MPV_FORMAT_FLAG), name);
            for(auto const* name : {"path", "mpv-version", "current-tracks/video/codec-desc", "hwdec-current", "current-vo", "loop-file", "loop-playlist", "keep-open"})
                check(api.observe_property(handle,id++,name,MPV_FORMAT_STRING),name);
            for(auto const* name : {"playlist", "track-list", "chapter-list"})
                check(api.observe_property(handle,id++,name,MPV_FORMAT_NODE),name);
            check(api.observe_property(handle,id++,"chapter",MPV_FORMAT_INT64),"chapter");
            MpvEvent ready{MpvEvent::Kind::Ready}; ready.flag=api.acquireSwapChain!=nullptr; ready.integer=api.client_api_version(); Emit(std::move(ready));
            std::uint64_t inFlight{};
            bool done = false;
            while (!done) {
                std::optional<std::pair<std::uint64_t,std::vector<std::string>>> pending;
                { std::lock_guard lock(mutex); if (closing) break;
                  if(!inFlight && !commands.empty()) { pending=std::move(commands.front()); commands.pop_front(); } }
                // client.h explicitly permits async command reordering. Serialize at replies.
                if(pending) {
                    auto const& [request,args]=*pending;
                    std::vector<char const*> argv;
                    for(auto const& arg:args) argv.push_back(arg.c_str());
                    argv.push_back(nullptr);
                    int error=api.command_async(handle,request,argv.data());
                    if(error<0) { MpvEvent e{MpvEvent::Kind::CommandReply}; e.requestId=request; e.error=error; e.text=api.error_string(error); Emit(std::move(e)); }
                    else inFlight=request;
                }
                // Bounded wait permits shutdown without calling the handle concurrently.
                auto const* raw = api.wait_event(handle, 0.05);
                MpvEvent e{MpvEvent::Kind::Property};
                e.requestId = raw->reply_userdata; e.error = raw->error;
                if (e.error < 0) e.text = api.error_string(e.error);
                switch (raw->event_id) {
                case MPV_EVENT_NONE: continue;
                case MPV_EVENT_START_FILE: e.kind=MpvEvent::Kind::StartFile; if(raw->data) e.entryId=static_cast<mpv_event_start_file*>(raw->data)->playlist_entry_id; break;
                case MPV_EVENT_FILE_LOADED: e.kind=MpvEvent::Kind::Loaded; break;
                case MPV_EVENT_END_FILE: {
                    e.kind=MpvEvent::Kind::EndFile;
                    auto const& end=*static_cast<mpv_event_end_file*>(raw->data);
                    e.entryId=end.playlist_entry_id; e.reason=end.reason; e.error=end.error;
                    if(end.error<0) e.text=api.error_string(end.error); break;
                }
                case MPV_EVENT_SEEK: e.kind=MpvEvent::Kind::Seek; break;
                case MPV_EVENT_PLAYBACK_RESTART: e.kind=MpvEvent::Kind::Restart; break;
                case MPV_EVENT_VIDEO_RECONFIG: e.kind=MpvEvent::Kind::VideoReconfig; break;
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
                case MPV_EVENT_SHUTDOWN: e.kind=MpvEvent::Kind::Shutdown; done=true; break;
                case MPV_EVENT_QUEUE_OVERFLOW: e.kind=MpvEvent::Kind::Failure; e.text="libmpv event queue overflow; playback state cannot be trusted"; done=true; break;
                case MPV_EVENT_PROPERTY_CHANGE: {
                    auto const& p=*static_cast<mpv_event_property*>(raw->data);
                    e.name=p.name ? p.name : "";
                    if(p.data && p.format==MPV_FORMAT_NODE) CopyCollectionProperty(e,*static_cast<mpv_node*>(p.data));
                    if(p.data && p.format==MPV_FORMAT_INT64) e.integer=*static_cast<std::int64_t*>(p.data);
                    if(p.data && p.format==MPV_FORMAT_DOUBLE) e.number=*static_cast<double*>(p.data);
                    if(p.data && p.format==MPV_FORMAT_FLAG) e.flag=*static_cast<int*>(p.data)!=0;
                    if(p.data && p.format==MPV_FORMAT_STRING) { auto s=*static_cast<char**>(p.data); if(s) e.text=s; }
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
                Emit(std::move(e));
            }
        } catch (std::exception const& error) { MpvEvent e{MpvEvent::Kind::Failure}; e.text=error.what(); Emit(std::move(e)); }
        catch (...) { MpvEvent e{MpvEvent::Kind::Failure}; e.text="Unexpected libmpv worker failure"; Emit(std::move(e)); }
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
void MpvClient::Start(std::wstring path) {
    std::lock_guard lock(state_->mutex);
    if(state_->started || state_->closing) return;
    state_->started=true;
    try { std::thread([state=state_, path=std::move(path)] { state->Run(path); }).detach(); }
    catch(...) { state_->started=false; throw; }
}
std::uint64_t MpvClient::Command(std::vector<std::string> args) {
    std::lock_guard lock(state_->mutex);
    if(state_->closing || !state_->started || args.empty()) return 0;
    auto id=state_->nextId++; state_->commands.emplace_back(id,std::move(args)); return id;
}
void MpvClient::CloseAsync(std::function<void()> completion) {
    bool immediate{};
    { std::lock_guard lock(state_->mutex); state_->closing=true;
      immediate=state_->closed || !state_->started;
      if(!immediate && completion) state_->closedCallbacks.push_back(completion); }
    if(immediate && completion) completion();
}
}

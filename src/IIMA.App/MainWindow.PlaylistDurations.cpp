// IINA SidebarPlaylistPane.PlaylistCellView.update / refreshTotalLength and
// PlaybackInfo duration cache. Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Windows adaptation: bounded local MF metadata probes, no sample reads or URL inputs;
// partial totals explicitly distinguish unavailable duration from zero.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/PlaylistDurationProbe.h"
#include <thread>
#include <set>
#include <cmath>
#include <algorithm>
#include <filesystem>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
constexpr std::size_t MaxCache=512,MaxBytes=1024*1024,MaxPath=32767,Prefix=128,Selection=32,Batch=8,MetadataReserve=2048;
bool ValidDuration(std::optional<double> seconds) {return seconds && std::isfinite(*seconds) && *seconds>0 && *seconds<=1e12;}
hstring DurationText(double seconds) {
    const auto value=static_cast<std::uint64_t>(seconds);
    auto two=[](std::uint64_t v){return (v<10 ? hstring(L"0") : hstring{})+to_hstring(v);};
    return to_hstring(value/3600)+L":"+two(value/60%60)+L":"+two(value%60);
}
struct DurationTask {std::int64_t id;std::string path;};
struct DurationResult {DurationTask task;std::optional<double> seconds;std::optional<iima::PlaylistDurationFingerprint> fingerprint;std::optional<std::string> title,artist;};
}
namespace winrt::IIMA::implementation {
void MainWindow::CancelPlaylistDurations(bool clearCache) {
    playlistDurationStop_.request_stop();
    ++playlistDurationEpoch_;
    if(clearCache) { playlistDurationCache_.clear(); playlistDurationBytes_=0; }
    else for(auto it=playlistDurationCache_.begin();it!=playlistDurationCache_.end();) {
        if(!it->second.complete) {playlistDurationBytes_-=it->first.size()+MetadataReserve;it=playlistDurationCache_.erase(it);}
        else ++it;
    }
    UpdatePlaylistMetadata();
    // Do not free the worker slot: a synchronous native call may still own it.
    // Close does not await that call; the worker owns only copies and a weak UI.
}
void MainWindow::UpdatePlaylistDurations(bool refresh) {
    if(closing_ || playlistDurationProjecting_) return;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{playlistDurationProjecting_};
    playlistDurationProjecting_=true;
    auto const& state=session_->Snapshot();
    const bool enabled=userDataLoaded_ && preferences_.Boolean("prefetchPlaylistVideoDuration");
    const auto ids=SelectedPlaylistIds(); std::set<std::int64_t> selected(ids.begin(),ids.end());
    // Only this bounded discovery window is retained. Current/title-only mpv
    // revisions do not invalidate it or cause another metadata read.
    std::vector<std::pair<std::int64_t,std::string>> window;
    std::size_t windowBytes{},selectedWindow{};
    auto addWindow=[&](auto const& entry) {
        if(entry.path.size()>MaxPath || entry.path.size()>MaxBytes-windowBytes ||
            std::any_of(window.begin(),window.end(),[&](auto const& value){return value.first==entry.id;})) return;
        window.emplace_back(entry.id,entry.path);windowBytes+=entry.path.size();
    };
    for(auto const& entry:state.playlist) if(selected.contains(entry.id) && selectedWindow++<Selection) addWindow(entry);
    for(std::size_t i=0;i<std::min(Prefix,state.playlist.size());++i) addWindow(state.playlist[i]);
    if(refresh || enabled!=playlistDurationEnabled_ || playlistDurationInput_!=inputGeneration_) {
        CancelPlaylistDurations();
        playlistDurationEnabled_=enabled;
        playlistDurationInput_=inputGeneration_;
    } else if(window!=playlistDurationWindow_ || playlistDurationMedia_!=state.mediaGeneration || playlistDurationIntent_!=state.mediaOpenIntentRevision) {
        CancelPlaylistDurations(false); // Same queue's completed file metadata remains valid.
    }
    playlistDurationWindow_=std::move(window);
    playlistDurationMedia_=state.mediaGeneration; playlistDurationIntent_=state.mediaOpenIntentRevision;
    if(playlistDurationRevision_!=state.playlistRevision) {
        for(auto it=playlistDurationCache_.begin();it!=playlistDurationCache_.end();) {
            const bool survives=std::any_of(state.playlist.begin(),state.playlist.end(),[&](auto const& entry){
                return entry.id==it->second.entryId && entry.path==it->first;
            });
            if(!survives) {playlistDurationBytes_-=it->first.size()+MetadataReserve;it=playlistDurationCache_.erase(it);} else ++it;
        }
    }
    playlistDurationRevision_=state.playlistRevision;
    const bool ready=enabled && state.session==iima::SessionState::Ready && !session_->PlaylistEditBusy() &&
        !state.mediaOriginPending && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty();
    if(!ready && playlistDurationBusy_ && !playlistDurationStop_.stop_requested()) CancelPlaylistDurations(false);
    auto known=[&](iima::PlaylistItem const& entry)->std::optional<double> {
        if(entry.current && entry.id==state.entryId && !state.mediaOriginPending && state.media==iima::MediaState::Loaded && ValidDuration(state.duration)) return state.duration;
        auto found=playlistDurationCache_.find(entry.path);
        return found!=playlistDurationCache_.end() && found->second.complete ? found->second.seconds : std::nullopt;
    };
    std::size_t totalKnown{},selectedKnown{},selectedCount{}; double total{},selectedTotal{};
    const bool rowsMatch=playlist_.size()==state.playlist.size() && PlaylistView().Items().Size()==state.playlist.size() &&
        std::equal(playlist_.begin(),playlist_.end(),state.playlist.begin(),[](auto const& a,auto const& b){return a.id==b.id && a.path==b.path;});
    for(std::size_t i=0;i<state.playlist.size();++i) {
        auto const& entry=state.playlist[i];auto seconds=known(entry);
        const bool chosen=selected.contains(entry.id); if(chosen) ++selectedCount;
        if(seconds) {++totalKnown;total+=*seconds;if(chosen){++selectedKnown;selectedTotal+=*seconds;}}
        if(rowsMatch) {
            auto item=PlaylistView().Items().GetAt(static_cast<uint32_t>(i)).try_as<Controls::ListViewItem>();
            auto row=item && unbox_value<std::int64_t>(item.Tag())==entry.id ? item.Content().try_as<Controls::Grid>() : nullptr;
            if(row && row.Children().Size()>=2) if(auto label=row.Children().GetAt(1).try_as<Controls::TextBlock>()) {
                auto text=seconds ? DurationText(*seconds) : hstring(L"—");
                if(label.Text()!=text) label.Text(text);
            }
        }
    }
    // The playlist itself is mpv-owned. A total larger than the display budget
    // is reported explicitly rather than overflowing the clock conversion.
    auto sum=[](double seconds){return std::isfinite(seconds) && seconds<=1e15 ? DurationText(seconds) : hstring(L"超出显示范围");};
    PlaylistDurationSummary().Text(L"总计已知 "+to_hstring(totalKnown)+L"/"+to_hstring(state.playlist.size())+L" 项："+sum(total)+
        L"；选中已知 "+to_hstring(selectedKnown)+L"/"+to_hstring(selectedCount)+L" 项："+sum(selectedTotal));
    RefreshPlaylistDurationsButton().IsEnabled(enabled && state.session==iima::SessionState::Ready);
    PlaylistDurationHint().Text(!enabled ? L"预取已关闭；不显示缓存标签，仅显示播放器已观测的当前时长。" :
        playlistDurationCache_.size()>=MaxCache || playlistDurationBytes_+MetadataReserve+MaxPath>MaxBytes ?
        L"已达或接近512条／1MiB缓存预算（每项预留2KiB标签）；刷新会清除时长与标签缓存。—表示未知。" :
        L"仅本地文件：自动前128项＋选中最多32项；—表示未知，合计仅含已知项。文件改动后请刷新时长与标签缓存。");
    UpdatePlaylistMetadata();
    if(!ready || playlistDurationBusy_ || !rowsMatch) return;
    std::vector<DurationTask> tasks;
    auto consider=[&](auto const& entry) {
        if(tasks.size()>=Batch || entry.id<0 || playlistDurationCache_.contains(entry.path) ||
            playlistDurationCache_.size()>=MaxCache || entry.path.empty() || entry.path.size()>MaxPath ||
            entry.path.size()+MetadataReserve>MaxBytes-playlistDurationBytes_) return;
        // Syntactic prefilter only. All disk/type/identity/drive checks run on
        // the worker; URLs and UNC paths never trigger automatic network IO.
        if(entry.path.size()<3 || entry.path[1]!=':' || (entry.path[2]!='\\' && entry.path[2]!='/') ||
            !((entry.path[0]>='A' && entry.path[0]<='Z') || (entry.path[0]>='a' && entry.path[0]<='z'))) return;
        playlistDurationBytes_+=entry.path.size()+MetadataReserve;
        playlistDurationCache_.emplace(entry.path,PlaylistDurationCacheEntry{std::nullopt,false,std::nullopt,entry.id});
        tasks.push_back({entry.id,entry.path});
    };
    for(auto const& [id,path]:playlistDurationWindow_) {
        auto entry=std::find_if(state.playlist.begin(),state.playlist.end(),[&](auto const& value){return value.id==id && value.path==path;});
        if(entry!=state.playlist.end()) consider(*entry);
    }
    if(tasks.empty()) return;
    playlistDurationStop_=std::stop_source{};
    auto stop=playlistDurationStop_.get_token();const auto epoch=playlistDurationEpoch_;
    playlistDurationBusy_=true;
    auto weak=get_weak();auto dispatcher=DispatcherQueue();
    try {
        std::thread([weak,dispatcher,stop,epoch,tasks=std::move(tasks)]() mutable {
            std::vector<DurationResult> results;
            bool initialized{};
            try {
                init_apartment(apartment_type::multi_threaded);initialized=true;
                for(auto const& task:tasks) {
                    if(stop.stop_requested()) break;
                    auto result=iima::ProbePlaylistDuration(std::filesystem::path(to_hstring(task.path).c_str()),stop);
                    const bool ready=result.status==iima::PlaylistDurationProbeStatus::Ready && result.fingerprint;
                    results.push_back({task,ready && ValidDuration(result.seconds) ? result.seconds : std::nullopt,
                        ready ? result.fingerprint : std::nullopt,
                        ready && result.title && result.title->size()<=1024 ? result.title : std::nullopt,
                        ready && result.artist && result.artist->size()<=1024 ? result.artist : std::nullopt});
                }
            } catch(...) {} // Cache unreturned probes as unavailable for this epoch.
            if(initialized) uninit_apartment();
            dispatcher.TryEnqueue([weak,epoch,tasks=std::move(tasks),results=std::move(results)] {
                if(auto self=weak.get()) {
                    self->playlistDurationBusy_=false; // Only completion releases the slot.
                    if(self->closing_) return;
                    auto const& current=self->session_->Snapshot();
                    if(epoch==self->playlistDurationEpoch_ &&
                        self->playlistDurationMedia_==current.mediaGeneration && self->playlistDurationIntent_==current.mediaOpenIntentRevision &&
                        self->playlistDurationInput_==self->inputGeneration_ && self->preferences_.Boolean("prefetchPlaylistVideoDuration")) {
                        for(auto const& task:tasks) {
                            auto entry=std::find_if(current.playlist.begin(),current.playlist.end(),[&](auto const& e){return e.id==task.id && e.path==task.path;});
                            auto cache=self->playlistDurationCache_.find(task.path);
                            if(entry==current.playlist.end() || cache==self->playlistDurationCache_.end()) continue;
                            auto result=std::find_if(results.begin(),results.end(),[&](auto const& r){return r.task.id==task.id && r.task.path==task.path;});
                            cache->second={result==results.end() ? std::nullopt : result->seconds,true,result==results.end() ? std::nullopt : result->fingerprint,task.id,
                                result==results.end() ? std::nullopt : result->title, result==results.end() ? std::nullopt : result->artist};
                        }
                    }
                    self->UpdatePlaylistDurations();
                }
            }); // If dispatch is gone, never touch UI or start a replacement worker.
        }).detach();
    } catch(...) {
        playlistDurationBusy_=false;
        for(auto& [path,entry]:playlistDurationCache_) if(!entry.complete) entry.complete=true;
        PlaylistDurationHint().Text(L"无法启动时长读取；可稍后显式刷新重试。");
    }
}
}

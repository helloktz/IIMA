// IINA SidebarPlaylistPane matchedSubs / PlayerCore.fileStarted.
// Copyright (c) 2016-2025 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
using namespace winrt;
namespace {
bool SameSubtitlePath(std::string const& left,std::string const& right) {
    try {
        auto a=std::filesystem::path(to_hstring(left).c_str()).lexically_normal().native();
        auto b=std::filesystem::path(to_hstring(right).c_str()).lexically_normal().native();
        return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
    } catch (...) { return false; }
}
}
namespace winrt::IIMA::implementation {
bool MainWindow::HasPlaylistSubtitleOverride(std::int64_t id,std::string const& path) const {
    if (playlistSubtitleInput_!=inputGeneration_) return false;
    auto const& queue=session_->Snapshot().playlist;
    auto owned=std::find_if(queue.begin(),queue.end(),[&](auto const& item) {
        return item.id==id && SameSubtitlePath(item.path,path);
    });
    return owned!=queue.end() && playlistSubtitleAssociations_.Find(id,owned->path);
}
std::vector<std::string> MainWindow::PlaylistSubtitlePaths(std::int64_t id,std::string const& path) const {
    auto const& state=session_->Snapshot();
    if (std::none_of(state.playlist.begin(),state.playlist.end(),
        [&](auto const& item) { return item.id==id && item.path==path; })) return {};
    if (playlistSubtitleInput_==inputGeneration_)
        if (auto explicitPaths=playlistSubtitleAssociations_.Find(id,path)) return *explicitPaths;
    if (session_->PlaylistEditBusy() || automaticSubtitleQueue_.size()!=state.playlist.size() ||
        !std::equal(automaticSubtitleQueue_.begin(),automaticSubtitleQueue_.end(),state.playlist.begin(),
            [](auto const& a,auto const& b) { return a.id==b.id && a.path==b.path; }) ||
        automaticSubtitlePending_ || automaticSubtitleInput_!=inputGeneration_ ||
        automaticSubtitleIntent_!=session_->Snapshot().mediaOpenIntentRevision ||
        automaticSubtitleEditIntent_!=session_->PlaylistEditIntentRevision()) return {};
    if (std::none_of(automaticSubtitleQueue_.begin(),automaticSubtitleQueue_.end(),
        [&](auto const& item) { return item.id==id && item.path==path; })) return {};
    auto found=std::find_if(automaticSubtitleMatches_.begin(),automaticSubtitleMatches_.end(),
        [&](auto const& match) { return SameSubtitlePath(to_string(match.media.native()),path); });
    std::vector<std::string> result;
    if (found!=automaticSubtitleMatches_.end())
        for (auto const& sub:found->subtitles) result.push_back(to_string(sub.native()));
    return result;
}
bool MainWindow::SetPlaylistSubtitlePaths(std::int64_t id,std::string const& path,std::vector<std::string> paths) {
    auto const& state=session_->Snapshot();
    if (closing_ || state.session!=iima::SessionState::Ready || session_->PlaylistEditBusy() ||
        std::count_if(state.playlist.begin(),state.playlist.end(),
            [&](auto const& item) { return item.id==id && item.path==path; })!=1) return false;
    if (playlistSubtitleInput_!=inputGeneration_) {
        playlistSubtitleAssociations_.Clear(); playlistSubtitleInput_=inputGeneration_;
    }
    playlistSubtitleAssociations_.Reconcile(state.playlist);
    if (!playlistSubtitleAssociations_.Set(id,path,std::move(paths))) return false;
    // Association editing does not reload/unload/select the currently playing
    // subtitle. Even a pending first restart must wait for the next generation.
    if (state.entryId==id) playlistSubtitleSeenGeneration_=state.mediaGeneration;
    return true;
}
void MainWindow::UpdatePlaylistSubtitleAssociations() {
    auto const& state=session_->Snapshot();
    if (closing_ || playlistSubtitleInput_!=inputGeneration_ || state.session==iima::SessionState::Failed ||
        state.session==iima::SessionState::Closing || state.session==iima::SessionState::Closed) {
        playlistSubtitleAssociations_.Clear(); playlistSubtitleInput_=inputGeneration_;
        playlistSubtitleSeenGeneration_=0; playlistSubtitleWaitingGeneration_=0;
        return;
    }
    // Reordering is harmless. An observed removal/path replacement permanently
    // revokes the old ID; re-adding that path receives a fresh mpv ID.
    playlistSubtitleAssociations_.Reconcile(state.playlist);
    if (!state.mediaGeneration || state.mediaGeneration==playlistSubtitleSeenGeneration_) return;
    if (playlistSubtitleWaitingGeneration_!=state.mediaGeneration) {
        playlistSubtitleWaitingGeneration_=state.mediaGeneration;
        playlistSubtitleUserIntent_=session_->SubtitleEditIntentRevision();
        playlistSubtitleDeadline_={};
    }
    if (pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ ||
        session_->SubtitleEditIntentRevision()!=playlistSubtitleUserIntent_) {
        playlistSubtitleSeenGeneration_=state.mediaGeneration; return;
    }
    if (state.media!=iima::MediaState::Loaded || session_->PlaylistEditBusy()) return;
    const auto now=std::chrono::steady_clock::now();
    if (playlistSubtitleDeadline_==std::chrono::steady_clock::time_point{}) playlistSubtitleDeadline_=now+std::chrono::seconds(10);
    if (now>playlistSubtitleDeadline_) { playlistSubtitleSeenGeneration_=state.mediaGeneration; return; }
    if (state.path.empty() || state.tracks.empty() || !state.mediaPlaybackRestarted) return;
    auto owned=std::find_if(state.playlist.begin(),state.playlist.end(),
        [&](auto const& item) { return item.id==state.entryId && SameSubtitlePath(item.path,state.path); });
    if (owned==state.playlist.end()) return;
    auto match=playlistSubtitleAssociations_.Find(owned->id,owned->path);
    playlistSubtitleSeenGeneration_=state.mediaGeneration;
    if (!match) return;
    auto paths=*match; // Own values across any reentrant snapshot callback.
    const auto generation=state.mediaGeneration, intent=state.mediaOpenIntentRevision;
    for (auto const& path:paths) {
        auto const& latest=session_->Snapshot();
        if (latest.mediaGeneration!=generation || latest.mediaOpenIntentRevision!=intent) break;
        if (std::any_of(latest.tracks.begin(),latest.tracks.end(),[&](auto const& track) {
            return track.type=="sub" && !track.externalPath.empty() && SameSubtitlePath(track.externalPath,path);
        })) continue;
        session_->AddSubtitle(path,false); // Observations, not a guessed sid, own selection.
    }
}
}

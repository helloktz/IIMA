// IINA PlayerCore.autoLoadFilesInCurrentFolder/fileStarted and AutoFileMatcher.
// Copyright (c) 2017 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/AutomaticFileMatcher.h"
#include <algorithm>
#include <chrono>

using namespace winrt;
namespace {
bool SameLocalPath(std::filesystem::path const& a, std::filesystem::path const& b) {
    auto left=a.lexically_normal().native(), right=b.lexically_normal().native();
    return CompareStringOrdinal(left.c_str(),static_cast<int>(left.size()),
        right.c_str(),static_cast<int>(right.size()),TRUE)==CSTR_EQUAL;
}
bool SameLocalText(std::string const& a, std::string const& b) {
    if (a==b) return true;
    try {
        return SameLocalPath(std::filesystem::path(to_hstring(a).c_str()),
            std::filesystem::path(to_hstring(b).c_str()));
    } catch (...) { return false; }
}
}
namespace winrt::IIMA::implementation {
void MainWindow::CancelAutomaticFiles() {
    automaticFilesStop_.request_stop();
    ++automaticFilesGeneration_;
    automaticFilesPath_.clear(); automaticFilesStarted_=false;
}
void MainWindow::PrepareAutomaticFiles(std::vector<std::string> const& paths, bool eligible) {
    CancelAutomaticFiles();
    if (!eligible || paths.size()!=1 || !userDataLoaded_ ||
        (!preferences_.Boolean("playlistAutoAdd") && preferences_.Integer("subAutoLoadIINA")==0)) return;
    automaticFilesPath_=paths.front();
    automaticFilesIntent_=session_->Snapshot().mediaOpenIntentRevision+1;
    automaticFilesInputRevision_=inputGeneration_;
    automaticFilesEditIntent_=session_->PlaylistEditIntentRevision();
    automaticFilesSubtitleIntent_=session_->SubtitleEditIntentRevision();
    automaticFilesDeadline_={};
}
void MainWindow::UpdateAutomaticFiles() {
    if (automaticFilesPath_.empty()) return;
    auto const& state=session_->Snapshot();
    if (closing_ || (automaticFilesStarted_ && state.mediaGeneration!=automaticFilesMediaGeneration_) ||
        inputGeneration_!=automaticFilesInputRevision_ ||
        state.mediaOpenIntentRevision>automaticFilesIntent_ ||
        session_->PlaylistEditIntentRevision()!=automaticFilesEditIntent_ ||
        pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ || !activationInputs_.empty()) {
        CancelAutomaticFiles(); return;
    }
    if (state.mediaOpenIntentRevision!=automaticFilesIntent_ || state.mediaOriginPending ||
        state.media==iima::MediaState::Loading) return;
    if (state.session!=iima::SessionState::Ready || state.media!=iima::MediaState::Loaded ||
        !state.mediaExplicitOpen || (!state.path.empty() && !SameLocalText(state.path,automaticFilesPath_))) {
        CancelAutomaticFiles(); return;
    }
    if (automaticFilesStarted_) return;
    const auto now=std::chrono::steady_clock::now();
    if (automaticFilesDeadline_==std::chrono::steady_clock::time_point{})
        automaticFilesDeadline_=now+std::chrono::seconds(10);
    if (now>automaticFilesDeadline_ || session_->PlaylistEditBusy()) { CancelAutomaticFiles(); return; }
    // The initial playlist observation may arrive after FILE_LOADED. Wait only
    // for the positively identified singleton, never extend an existing queue.
    // FILE_LOADED does not order property observations. Wait for current path,
    // a real initial track list and the first playback restart before scanning.
    if (state.path.empty() || state.tracks.empty() || !state.mediaPlaybackRestarted) return;
    if (state.playlist.size()!=1 || state.playlist.front().id!=state.entryId ||
        !state.playlist.front().current || !SameLocalText(state.playlist.front().path,state.path)) return;
    iima::AutomaticFileMatchOptions options;
    options.expandPlaylist=preferences_.Boolean("playlistAutoAdd");
    options.subtitles.mode=static_cast<iima::SubtitleAutoLoad>(preferences_.Integer("subAutoLoadIINA"));
    options.subtitles.forceMatchUnmatched=options.expandPlaylist;
    const auto priority=preferences_.String("subAutoLoadPriorityString");
    std::size_t start{};
    while (start<priority.size() && options.subtitles.priorityStrings.size()<32) {
        auto end=priority.find(',',start);
        auto part=priority.substr(start,end==priority.npos ? priority.size()-start : end-start);
        auto first=part.find_first_not_of(" \t\r\n"), last=part.find_last_not_of(" \t\r\n");
        if (first!=part.npos) options.subtitles.priorityStrings.push_back(iima::DecodeMatchUtf8(part.substr(first,last-first+1)));
        if (end==priority.npos) break;
        start=end+1;
    }
    const auto generation=automaticFilesGeneration_, intent=automaticFilesIntent_, input=automaticFilesInputRevision_;
    const auto mediaGeneration=state.mediaGeneration, revision=state.playlistRevision, tracks=state.tracksRevision;
    const auto editIntent=automaticFilesEditIntent_, subtitleIntent=automaticFilesSubtitleIntent_;
    const auto entry=state.entryId;
    const auto selectedSubtitle=state.subtitleId;
    const bool expand=options.expandPlaylist;
    const auto media=std::filesystem::path(to_hstring(state.path).c_str());
    auto weak=get_weak();
    automaticFilesStarted_=true; automaticFilesMediaGeneration_=mediaGeneration;
    try {
        automaticFilesStop_=iima::AutomaticFileMatcher::Discover(media,std::move(options),generation,DispatcherQueue(),
            [weak,generation,intent,input,mediaGeneration,revision,tracks,editIntent,subtitleIntent,entry,selectedSubtitle,expand,media]
            (iima::AutomaticFileMatchResult result) {
                auto self=weak.get();
                if (!self || self->closing_ || result.cancelled || self->automaticFilesGeneration_!=generation) return;
                auto const& current=self->session_->Snapshot();
                if (self->inputGeneration_!=input || current.mediaOpenIntentRevision!=intent ||
                    current.mediaGeneration!=mediaGeneration || current.entryId!=entry ||
                    current.media!=iima::MediaState::Loaded || current.playlistRevision!=revision ||
                    self->session_->PlaylistEditIntentRevision()!=editIntent || self->session_->PlaylistEditBusy() ||
                    self->pickerOpen_ || self->interactiveCrop_.Active() || self->cropOriginal_ ||
                    self->inputResolving_ || !self->pendingFiles_.empty() || !self->activationInputs_.empty()) {
                    self->CancelAutomaticFiles(); return;
                }
                self->automaticFilesPath_.clear(); // Reentrant Notify cannot restart this scan.
                if (!result.complete) {
                    self->ShowError(L"同目录自动匹配未完整完成，未修改队列或字幕。");
                    return;
                }
                std::vector<std::string> before, after;
                bool found=false;
                for (auto const& path:result.orderedMedia) {
                    if (SameLocalPath(path,media)) { found=true; continue; }
                    (found ? after : before).push_back(to_string(path.native()));
                }
                if (!found) return;
                // Do not override track changes made by mpv auto loading or a
                // user while discovery ran. Existing external paths are deduped.
                std::vector<std::string> subtitles;
                bool subtitleLimited=false;
                if (current.tracksRevision==tracks && current.subtitleId==selectedSubtitle &&
                    self->session_->SubtitleEditIntentRevision()==subtitleIntent) {
                    auto match=std::find_if(result.matches.media.begin(),result.matches.media.end(),
                        [&](auto const& item) { return SameLocalPath(item.media,media); });
                    if (match!=result.matches.media.end()) for (auto const& path:match->subtitles) {
                        if (subtitles.size()==64) { subtitleLimited=true; break; }
                        if (std::any_of(current.tracks.begin(),current.tracks.end(),[&](auto const& track) {
                            return track.type=="sub" && !track.externalPath.empty() &&
                                SameLocalPath(std::filesystem::path(to_hstring(track.externalPath).c_str()),path);
                        })) continue;
                        auto text=to_string(path.native());
                        if (std::find(subtitles.begin(),subtitles.end(),text)==subtitles.end()) subtitles.push_back(std::move(text));
                    }
                }
                if (expand && (!before.empty() || !after.empty()))
                    self->session_->ExpandAutoPlaylist(std::move(before),std::move(after),entry,revision,intent,mediaGeneration);
                for (auto const& path:subtitles) {
                    auto const& latest=self->session_->Snapshot();
                    if (latest.mediaGeneration!=mediaGeneration || latest.mediaOpenIntentRevision!=intent) break;
                    self->session_->AddSubtitle(path,false); // mpv selects; do not force sid=1.
                }
                std::wstring warning;
                if (subtitleLimited) warning=L"自动字幕最多添加64项；其余匹配未添加，请手动选择。";
                if (!result.warnings.empty()) {
                    if (!warning.empty()) warning+=L"\n";
                    warning+=L"同目录自动匹配已跳过部分内容：" + result.warnings.front();
                }
                if (!warning.empty()) self->ShowError(hstring(warning));
            });
    } catch (...) {
        CancelAutomaticFiles();
        ShowError(L"无法开始同目录自动匹配，未修改队列或字幕。");
    }
}
}

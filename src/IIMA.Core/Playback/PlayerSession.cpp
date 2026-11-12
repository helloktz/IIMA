// Adapted from IINA PlayerCore.swift / MPVController.swift / PlaybackInfo.swift.
// Copyright © 2016 lhc. All rights reserved. IINA's GPL-3.0 license applies
// to the adapted logic; see references/iina/LICENSE and docs/PORTING.md.
#include "PlayerSession.h"
#include <algorithm>
#include <cmath>

namespace iima {
PlayerSession::PlayerSession(Dispatch dispatch, Changed changed)
    : dispatch_(std::move(dispatch)), changed_(std::move(changed)) {}
std::shared_ptr<PlayerSession> PlayerSession::Create(Dispatch dispatch, Changed changed) {
    auto session=std::shared_ptr<PlayerSession>(new PlayerSession(std::move(dispatch),std::move(changed)));
    std::weak_ptr<PlayerSession> weak=session;
    session->client_=std::make_unique<MpvClient>([weak, dispatch=session->dispatch_](MpvEvent event) {
        dispatch([weak,event=std::move(event)]() mutable { if(auto self=weak.lock()) self->OnEvent(std::move(event)); });
    });
    return session;
}
void PlayerSession::Start(std::wstring path) { client_->Start(std::move(path)); }
void PlayerSession::Notify() { if(changed_) changed_(snapshot_); }
void PlayerSession::SetPresentationReady(bool ready) {
    ready=ready && static_cast<bool>(snapshot_.swapChain);
    if(presentationReady_==ready) return;
    presentationReady_=ready;
    MaybeResume();
}
void PlayerSession::MaybeResume() {
    if(resumePending_ && presentationReady_ && snapshot_.media==MediaState::Loaded &&
       !pauseRequest_ && !loadRequest_ && !waitingForStart_ && expectedEntry_>=0 && expectedEntry_==snapshot_.entryId) {
        resumePending_=false; Pause(false);
    }
}
bool PlayerSession::CanOpen() {
    if(snapshot_.session!=SessionState::Ready) return false;
    if(!snapshot_.compositionAvailable) {
        snapshot_.error="Video output is unavailable: install the pinned libmpv build with the IIMA owned-swapchain extension. The borrowed display-swapchain property cannot be used safely.";
        Notify(); return false;
    }
    return true;
}
void PlayerSession::Open(std::string path) { OpenMultiple({std::move(path)}); }
void PlayerSession::OpenMultiple(std::vector<std::string> paths,bool append) {
    if(paths.empty() || !CanOpen()) return;
    if(std::any_of(paths.begin(),paths.end(),[](auto const& p) { return p.empty() || p.find('\0')!=std::string::npos; })) {
        snapshot_.error="A media path is empty or contains a NUL character"; Notify(); return;
    }
    // No shell or command string parsing: UTF-8 paths remain individual API arguments.
    if(append) {
        for(auto& path:paths) client_->Command({"loadfile",std::move(path),"append"});
        return;
    }
    presentationReady_=false; resumePending_=!settings_.pauseWhenOpen; expectedEntry_=-1; loadRequest_=0;
    pendingCommands_.clear();
    transitionIssued_=false; waitingForStart_=true; loadReturnsEntry_=true;
    // Match IINA openMainWindow: create VO even for audio-only media, but only
    // once opening, not during app initialization. Composition still owns output.
    pendingCommands_.push_back({"set","force-window","yes"});
    for(std::size_t i=0;i<paths.size();++i)
        pendingCommands_.push_back({"loadfile",std::move(paths[i]),i==0 ? "replace" : "append"});
    pauseRequest_=client_->Command({"set","pause","yes"});
    if(!pauseRequest_) { pendingCommands_.clear(); resumePending_=false; snapshot_.error="Player is closing"; Notify(); }
}
void PlayerSession::PausedCommand(std::vector<std::string> command) {
    if(!CanOpen()) return;
    presentationReady_=false; resumePending_=!settings_.pauseWhenOpen; loadRequest_=0;
    transitionIssued_=false; waitingForStart_=true; loadReturnsEntry_=false;
    pendingCommands_={std::move(command)};
    pauseRequest_=client_->Command({"set","pause","yes"});
    if(!pauseRequest_) { pendingCommands_.clear(); resumePending_=false; snapshot_.error="Player is closing"; Notify(); }
}
void PlayerSession::PlaylistNext() {
    auto current=std::find_if(snapshot_.playlist.begin(),snapshot_.playlist.end(),[](auto const& p){return p.current;});
    if(current!=snapshot_.playlist.end() && std::next(current)==snapshot_.playlist.end()) return;
    expectedEntry_=current!=snapshot_.playlist.end() ? std::next(current)->id : -1;
    PausedCommand({"playlist-next","weak"});
}
void PlayerSession::PlaylistPrevious() {
    if(!snapshot_.playlist.empty() && snapshot_.playlist.front().current) { Seek(0); return; }
    auto current=std::find_if(snapshot_.playlist.begin(),snapshot_.playlist.end(),[](auto const& p){return p.current;});
    expectedEntry_=current!=snapshot_.playlist.end() && current!=snapshot_.playlist.begin() ? std::prev(current)->id : -1;
    PausedCommand({"playlist-prev","weak"});
}
void PlayerSession::PlaylistPlay(std::size_t index) {
    if(index<snapshot_.playlist.size()) { expectedEntry_=snapshot_.playlist[index].id; PausedCommand({"playlist-play-index",std::to_string(index)}); }
}
void PlayerSession::PlaylistRemove(std::size_t index) {
    if(snapshot_.session==SessionState::Ready && index<snapshot_.playlist.size())
        client_->Command({"playlist-remove",std::to_string(index)});
}
void PlayerSession::PlaylistMove(std::size_t from,std::size_t finalIndex) {
    if(snapshot_.session!=SessionState::Ready || from>=snapshot_.playlist.size() || finalIndex>=snapshot_.playlist.size() || from==finalIndex) return;
    // mpv takes an insertion-before target, not the post-removal final index.
    client_->Command({"playlist-move",std::to_string(from),std::to_string(finalIndex+(from<finalIndex ? 1 : 0))});
}
void PlayerSession::SelectTrack(std::string type,std::int64_t id) {
    if(snapshot_.session!=SessionState::Ready || id<0) return;
    std::string property=type=="audio" ? "aid" : type=="sub" ? "sid" : type=="video" ? "vid" : "";
    if(property.empty()) return;
    if(id && std::none_of(snapshot_.tracks.begin(),snapshot_.tracks.end(),[&](auto const& track){return track.type==type && track.id==id;})) return;
    client_->Command({"set",property,id ? std::to_string(id) : "no"});
}
void PlayerSession::SelectChapter(std::size_t index) {
    if(snapshot_.session!=SessionState::Ready || index>=snapshot_.chapters.size()) return;
    client_->Command({"seek",std::to_string(snapshot_.chapters[index].time),"absolute"});
    Pause(false);
}
void PlayerSession::ScreenshotToFile(std::string path) {
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded || path.empty() || path.find('\0')!=std::string::npos) return;
    if(std::none_of(snapshot_.tracks.begin(),snapshot_.tracks.end(),[](auto const& track) { return track.type=="video" && track.selected; })) {
        snapshot_.error="Screenshots require an active video track"; Notify(); return;
    }
    const char* mode=settings_.screenshotMode==ScreenshotMode::Subtitles ? "subtitles" :
        settings_.screenshotMode==ScreenshotMode::Window ? "window" : "video";
    client_->Command({"screenshot-to-file",std::move(path),mode});
}
void PlayerSession::AddSubtitle(std::string path) {
    if(snapshot_.session!=SessionState::Ready || path.empty() || path.find('\0')!=std::string::npos) return;
    auto found=std::find_if(snapshot_.tracks.begin(),snapshot_.tracks.end(),[&](auto const& t){return t.type=="sub" && t.externalPath==path;});
    if(found!=snapshot_.tracks.end()) client_->Command({"sub-reload",std::to_string(found->id)});
    else client_->Command({"sub-add",std::move(path),"select"});
}
void PlayerSession::Pause(bool value) {
    if(value) resumePending_=false; // A user pause cancels deferred auto-resume.
    if(snapshot_.session==SessionState::Ready && (value || (presentationReady_ && snapshot_.media==MediaState::Loaded))) client_->Command({"set","pause",value ? "yes" : "no"});
}
void PlayerSession::TogglePause() { if(snapshot_.session==SessionState::Ready && presentationReady_ && snapshot_.media==MediaState::Loaded) client_->Command({"cycle","pause"}); }
void PlayerSession::Seek(double seconds) {
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
       !snapshot_.duration || *snapshot_.duration<=0 || !std::isfinite(seconds)) return;
    client_->Command({"seek",std::to_string(std::clamp(seconds,0.0,*snapshot_.duration)),"absolute+exact"});
}
void PlayerSession::Stop() {
    pendingCommands_.clear(); pauseRequest_=0; resumePending_=false; loadRequest_=0;
    transitionIssued_=false; waitingForStart_=false; expectedEntry_=-1;
    if(snapshot_.session==SessionState::Ready) client_->Command({"stop"});
}
void PlayerSession::SetVolume(double value) {
    if(snapshot_.session==SessionState::Ready && std::isfinite(value))
        client_->Command({"set","volume",std::to_string(std::clamp(value,0.0,settings_.maxVolume))});
}
void PlayerSession::SetSpeed(double value) {
    if(snapshot_.session==SessionState::Ready && std::isfinite(value) && value>=0.01 && value<=100)
        client_->Command({"set","speed",std::to_string(value)});
}
void PlayerSession::SetCompositionSize(unsigned width,unsigned height) {
    if(snapshot_.session==SessionState::Ready && width && height && width<=16384 && height<=16384)
        client_->Command({"set","d3d11-composition-size",std::to_string(width)+"x"+std::to_string(height)});
}
void PlayerSession::ApplySettings(UserSettings const& settings) {
    if(!ValidUserSettings(settings)) { snapshot_.error="Playback settings contain an invalid value"; Notify(); return; }
    settings_=settings;
    SetPauseWhenOpen(settings.pauseWhenOpen);
    if(snapshot_.session!=SessionState::Ready) { applySettingsOnReady_=snapshot_.session==SessionState::Initializing; return; }
    SetMaxVolume(settings.maxVolume);
    SetVolume(settings.volume);
    SetSpeed(settings.speed);
    SetMuted(settings.muted);
    SetSubtitlesVisible(settings.subtitlesVisible);
    SetAudioDelay(settings.audioDelay);
    SetSubtitleDelay(settings.subtitleDelay);
    SetAutoPlayNext(settings.autoPlayNext);
    SetRepeatMode(settings.repeatMode);
    SetScreenshotMode(settings.screenshotMode);
}
void PlayerSession::SetPauseWhenOpen(bool value) {
    settings_.pauseWhenOpen=value;
    client_->SetPauseWhenOpen(value);
    if(value) resumePending_=false;
}
void PlayerSession::SetAutoPlayNext(bool value) {
    settings_.autoPlayNext=value;
    // IINA MPVController maps disabled automatic advance to keep-open=always.
    // IIMA keeps the last frame at final EOF (keep-open=yes) when enabled.
    if(snapshot_.session==SessionState::Ready) client_->Command({"set","keep-open",value ? "yes" : "always"});
}
void PlayerSession::SetMuted(bool value) {
    settings_.muted=value;
    if(snapshot_.session==SessionState::Ready) client_->Command({"set","mute",value ? "yes" : "no"});
}
void PlayerSession::ToggleMute() {
    if(snapshot_.session==SessionState::Ready) client_->Command({"cycle","mute"});
}
void PlayerSession::SetSubtitlesVisible(bool value) {
    settings_.subtitlesVisible=value;
    if(snapshot_.session==SessionState::Ready) client_->Command({"set","sub-visibility",value ? "yes" : "no"});
}
void PlayerSession::SetAudioDelay(double seconds) {
    if(!std::isfinite(seconds)) return;
    settings_.audioDelay=seconds;
    if(snapshot_.session==SessionState::Ready) client_->Command({"set","audio-delay",std::to_string(seconds)});
}
void PlayerSession::SetSubtitleDelay(double seconds) {
    if(!std::isfinite(seconds)) return;
    settings_.subtitleDelay=seconds;
    if(snapshot_.session==SessionState::Ready) client_->Command({"set","sub-delay",std::to_string(seconds)});
}
void PlayerSession::SetRepeatMode(RepeatMode mode) {
    if(mode!=RepeatMode::Off && mode!=RepeatMode::File && mode!=RepeatMode::Playlist) return;
    settings_.repeatMode=mode;
    if(snapshot_.session!=SessionState::Ready) return;
    if(mode==RepeatMode::File) client_->Command({"set","loop-file","inf"});
    else {
        client_->Command({"set","loop-playlist",mode==RepeatMode::Playlist ? "inf" : "no"});
        client_->Command({"set","loop-file","no"});
    }
}
void PlayerSession::SetMaxVolume(double value) {
    if(!std::isfinite(value) || value<100 || value>1000) return;
    settings_.maxVolume=value;
    if(snapshot_.session==SessionState::Ready) {
        client_->Command({"set","volume-max",std::to_string(value)});
        if(snapshot_.volume>value) SetVolume(value);
    }
}
void PlayerSession::SetScreenshotMode(ScreenshotMode mode) {
    if(mode==ScreenshotMode::Subtitles || mode==ScreenshotMode::Video || mode==ScreenshotMode::Window)
        settings_.screenshotMode=mode;
}
void PlayerSession::OnEvent(MpvEvent e) {
    if(snapshot_.session==SessionState::Closing || snapshot_.session==SessionState::Closed) return;
    if(e.kind==MpvEvent::Kind::CommandReply && e.requestId==pauseRequest_) {
        pauseRequest_=0;
        if(e.error>=0) for(auto& command:pendingCommands_) {
            const bool firstLoad=command.size()>2 && command[0]=="loadfile" && command[2]=="replace";
            const auto id=client_->Command(std::move(command));
            if(firstLoad || !loadReturnsEntry_) {
                loadRequest_=id;
                transitionIssued_=id!=0;
                if(!id) resumePending_=false;
            }
        }
        else resumePending_=false;
        pendingCommands_.clear();
    }
    if(e.kind==MpvEvent::Kind::CommandReply && loadRequest_ && e.requestId==loadRequest_) {
        loadRequest_=0;
        if(e.error<0) resumePending_=false;
        else if(loadReturnsEntry_) expectedEntry_=e.entryId;
    }
    // A playlist command can reload the current entry. Its old Loaded state and
    // rebound swapchain must not satisfy this new transaction before START_FILE.
    // Replies and lifecycle events are independent, so require both in either order.
    if(e.kind==MpvEvent::Kind::StartFile && transitionIssued_ && !pauseRequest_)
        waitingForStart_=false;
    const auto previousChain=snapshot_.swapChain;
    const bool wasPlaying=!snapshot_.paused;
    ApplyPlaybackEvent(snapshot_,e);
    if(e.kind==MpvEvent::Kind::Ready && applySettingsOnReady_) { applySettingsOnReady_=false; ApplySettings(settings_); }
    if(!pauseRequest_ && !loadRequest_ && !waitingForStart_ &&
       e.kind==MpvEvent::Kind::VideoReconfig && previousChain && !snapshot_.swapChain && wasPlaying && snapshot_.media==MediaState::Loaded) {
        // Device/output loss: don't leave invisible audio playing. A later user
        // Pause(true) cancels this deferred recovery, preserving manual intent.
        resumePending_=true; expectedEntry_=snapshot_.entryId; waitingForStart_=false;
        client_->Command({"set","pause","yes"});
    }
    if(previousChain!=snapshot_.swapChain || e.kind==MpvEvent::Kind::Loaded || e.kind==MpvEvent::Kind::VideoReconfig)
        presentationReady_=false;
    Notify(); // Presenter must acknowledge this current owned chain, not an older binding.
    MaybeResume();
    if(e.kind==MpvEvent::Kind::Shutdown) CloseAsync();
}
void PlayerSession::CloseAsync(std::function<void()> completion) {
    if(snapshot_.session!=SessionState::Closed) snapshot_.session=SessionState::Closing;
    pendingCommands_.clear(); pauseRequest_=0; loadRequest_=0; transitionIssued_=false; waitingForStart_=false; presentationReady_=false; resumePending_=false; snapshot_.swapChain.reset(); Notify();
    auto weak=weak_from_this();
    client_->CloseAsync([weak, dispatch=dispatch_, completion=std::move(completion)] {
        dispatch([weak,completion] {
            if(auto self=weak.lock()) { self->snapshot_.session=SessionState::Closed; self->Notify(); }
            if(completion) completion();
        });
    });
}
}

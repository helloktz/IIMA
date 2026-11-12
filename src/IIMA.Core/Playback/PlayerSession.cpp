// Adapted from IINA PlayerCore.swift / MPVController.swift / PlaybackInfo.swift.
// Copyright © 2016 lhc. All rights reserved. IINA's GPL-3.0 license applies
// to the adapted logic; see references/iina/LICENSE and docs/PORTING.md.
#include "PlayerSession.h"
#include "../Network/MediaUrl.h"
#include "../Windows/MusicModePolicy.h"
#include "../Screenshots/ScreenshotOptions.h"
#include <algorithm>
#include <cmath>
#include <charconv>

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
// IINA distinguishes openMainWindow's justOpenedFile from fileStarted's
// justStartedFile. A bool attached to the next event cannot preserve that
// distinction across queued old events. Pinned mpv cmd_loadfile creates a new,
// monotonically allocated playlist entry and returns playlist_entry_id.
void PlayerSession::BeginMediaOriginIntent(bool explicitOpen) {
    CancelExternalVideoLoad();
    CancelExternalTrackRemoval();
    CancelSubtitleReload();
    CancelScrub();
    if (snapshot_.networkOpenAttempt.phase != OpenAttemptPhase::None &&
        snapshot_.networkOpenAttempt.phase != OpenAttemptPhase::Cancelled)
        snapshot_.networkOpenAttempt.phase = OpenAttemptPhase::Superseded;
    networkPauseRequest_=0; networkLoadRequest_=0; networkEntry_.reset();
    networkLifecycle_.clear(); networkJournalOverflow_=false;
    networkBaselineGeneration_=snapshot_.mediaGeneration; networkOwnedGeneration_.reset();
    pendingWindowScale_.reset(); blockedWindowScale_.reset();
    snapshot_.externalWindowScale.reset();
    videoGeometryRequest_ = 0;
    snapshot_.mediaVideoGeometryReady = false;
    ++snapshot_.mediaOpenIntentRevision;
    snapshot_.mediaExplicitOpen = false;
    snapshot_.mediaOriginPending = explicitOpen;
    openOriginRequest_ = 0;
    openOriginEntry_.reset();
    openStarts_.clear();
}
void PlayerSession::ResolveMediaOrigin() {
    if (!snapshot_.mediaOriginPending || !openOriginEntry_) return;
    auto found = std::find_if(openStarts_.begin(), openStarts_.end(), [&](auto const& start) {
        return start.entry == *openOriginEntry_;
    });
    if (found == openStarts_.end()) return; // Reply preceded the actual start.
    const auto occurrences = std::count_if(openStarts_.begin(), openStarts_.end(), [&](auto const& start) {
        return start.entry == *openOriginEntry_;
    });
    // Same-entry reload before the reply is ambiguous. Never give that reload
    // the original Open's permission, nor revive a generation that has ended.
    snapshot_.mediaExplicitOpen = occurrences == 1 && found->generation == snapshot_.mediaGeneration &&
        snapshot_.entryId == *openOriginEntry_ &&
        (snapshot_.media == MediaState::Loading || snapshot_.media == MediaState::Loaded);
    snapshot_.mediaOriginPending = false;
    openOriginRequest_ = 0; openOriginEntry_.reset(); openStarts_.clear();
}
void PlayerSession::ObserveMediaOrigin(MpvEvent const& event) {
    if (event.kind == MpvEvent::Kind::StartFile) {
        ++snapshot_.mediaGeneration;
        snapshot_.mediaExplicitOpen = false;
        // Events while the pause command is outstanding cannot belong to the
        // not-yet-submitted replacement. Matching the returned unique ID also
        // rejects older START_FILE events queued before submission.
        if (snapshot_.mediaOriginPending && (openOriginRequest_ || openOriginEntry_)) {
            if (openStarts_.size() == 16) {
                snapshot_.mediaOriginPending = false;
                openOriginRequest_ = 0; openOriginEntry_.reset(); openStarts_.clear();
            } else openStarts_.push_back({event.entryId, snapshot_.mediaGeneration});
        }
        ResolveMediaOrigin();
    } else if (event.kind == MpvEvent::Kind::CommandReply && openOriginRequest_ &&
               event.requestId == openOriginRequest_) {
        openOriginRequest_ = 0;
        if (event.error < 0 || event.entryId < 0) {
            snapshot_.mediaOriginPending = false; openStarts_.clear();
        } else {
            openOriginEntry_ = event.entryId;
            ResolveMediaOrigin();
        }
    } else if (event.kind == MpvEvent::Kind::EndFile && event.entryId == snapshot_.entryId) {
        snapshot_.mediaExplicitOpen = false;
        ResolveMediaOrigin();
    } else if (event.kind == MpvEvent::Kind::Failure || event.kind == MpvEvent::Kind::Shutdown) {
        snapshot_.mediaExplicitOpen = false; snapshot_.mediaOriginPending = false;
        openOriginRequest_ = 0; openOriginEntry_.reset(); openStarts_.clear();
    }
}
void PlayerSession::SetPresentationReady(bool ready) {
    // Known audio-only media needs no video swapchain. Empty/unknown tracks
    // never bypass the handshake; real video still requires UI binding.
    ready=ready && (static_cast<bool>(snapshot_.swapChain) ||
        (snapshot_.media==MediaState::Loaded && AudioOnlyPlaybackEligible(snapshot_.tracks, !snapshot_.tracks.empty())));
    if(presentationReady_==ready) return;
    presentationReady_=ready;
    if(!ready) CancelScrub();
    MaybeResume();
    if(ready) RefreshWindowPausePolicy();
}
void PlayerSession::MaybeResume() {
    if(!sleepPauseHeld_ && resumePending_ && presentationReady_ && snapshot_.media==MediaState::Loaded &&
       !pauseRequest_ && !loadRequest_ && !waitingForStart_ && expectedEntry_>=0 && expectedEntry_==snapshot_.entryId) {
        resumePending_=false;
        // Automatic transition recovery is not explicit user pause intent.
        client_->Command({"set","pause","no"});
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
    if (append) ++playlistEditIntentRevision_;
    if(paths.empty() || !CanOpen()) return;
    if(std::any_of(paths.begin(),paths.end(),[](auto const& p) { return p.empty() || p.find('\0')!=std::string::npos; })) {
        snapshot_.error="A media path is empty or contains a NUL character"; Notify(); return;
    }
    if(!append) { CancelSubtitleReload(); sleepPauseHeld_=false; CancelWindowPausePolicy(); }
    CancelPlaylistEdits();
    playlistShuffled_=false;
    // No shell or command string parsing: UTF-8 paths remain individual API arguments.
    if(append) {
        for(auto& path:paths) client_->Command({"loadfile",std::move(path),"append"});
        return;
    }
    BeginMediaOriginIntent(true);
    if (paths.size()==1 && IsNetworkMediaPath(paths.front()))
        snapshot_.networkOpenAttempt={snapshot_.mediaOpenIntentRevision,OpenAttemptPhase::Pending,{}};
    presentationReady_=false; resumePending_=!settings_.pauseWhenOpen; expectedEntry_=-1; loadRequest_=0;
    pendingCommands_.clear();
    transitionIssued_=false; waitingForStart_=true; loadReturnsEntry_=true;
    CancelWatchedProperties();
    // Match IINA openMainWindow: create VO even for audio-only media, but only
    // once opening, not during app initialization. Composition still owns output.
    pendingCommands_.push_back({"set","force-window","yes"});
    for(std::size_t i=0;i<paths.size();++i)
        pendingCommands_.push_back({"loadfile",std::move(paths[i]),i==0 ? "replace" : "append"});
    PrependNextMediaPreferences();
    pauseRequest_=client_->Command({"set","pause","yes"});
    if (snapshot_.networkOpenAttempt.intent==snapshot_.mediaOpenIntentRevision &&
        snapshot_.networkOpenAttempt.phase==OpenAttemptPhase::Pending) {
        networkPauseRequest_=pauseRequest_;
        if (!pauseRequest_) snapshot_.networkOpenAttempt.phase=OpenAttemptPhase::Failed;
    }
    if(!pauseRequest_) { snapshot_.mediaOriginPending=false; pendingCommands_.clear(); resumePending_=false; snapshot_.error="Player is closing"; }
    Notify();
}
void PlayerSession::PausedCommand(std::vector<std::string> command) {
    if(!CanOpen()) return;
    BeginMediaOriginIntent(false);
    CancelSubtitleReload();
    sleepPauseHeld_=false;
    CancelWindowPausePolicy();
    CancelPlaylistEdits();
    presentationReady_=false; resumePending_=!settings_.pauseWhenOpen; loadRequest_=0;
    transitionIssued_=false; waitingForStart_=true; loadReturnsEntry_=false;
    CancelWatchedProperties();
    pendingCommands_={std::move(command)};
    PrependNextMediaPreferences();
    pauseRequest_=client_->Command({"set","pause","yes"});
    if(!pauseRequest_) { snapshot_.mediaOriginPending=false; pendingCommands_.clear(); resumePending_=false; snapshot_.error="Player is closing"; }
    Notify();
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
void PlayerSession::SelectTrack(std::string type,std::int64_t id) {
    BeginTrackEditIntent();
    if (type=="sub") { CancelSubtitleReload(); ++subtitleEditIntentRevision_; }
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
        waitingForStart_ || playlistEdit_ || id<0) return;
    std::string property=type=="audio" ? "aid" : type=="sub" ? "sid" : type=="video" ? "vid" : "";
    if(property.empty()) return;
    if(id && std::none_of(snapshot_.tracks.begin(),snapshot_.tracks.end(),[&](auto const& track){return track.type==type && track.id==id;})) return;
    client_->Command({"set",property,id ? std::to_string(id) : "no"});
}
// IINA MainMenuActions.menuCycleTrack delegates order and off-slot to mpv.
void PlayerSession::CycleTrack(std::string type) {
    BeginTrackEditIntent();
    if (type=="sub") { CancelSubtitleReload(); ++subtitleEditIntentRevision_; }
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || playlistEdit_) return;
    if (type != "video" && type != "audio" && type != "sub") return;
    client_->Command({"cycle", std::move(type)});
}
void PlayerSession::SelectChapter(std::size_t index) {
    CancelScrub();
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
        waitingForStart_ || playlistEdit_ || !snapshot_.seekable || index>=snapshot_.chapters.size()) return;
    client_->Command({"seek",std::to_string(snapshot_.chapters[index].time),"absolute"});
    Pause(false);
}
// IINA MainMenuActions.menuNextChapter/menuPreviousChapter use add, not a
// computed chapter index: mpv owns chapter-seek-threshold and end behavior.
void PlayerSession::StepChapter(int direction) {
    CancelScrub();
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !snapshot_.seekable || snapshot_.chapters.empty() || waitingForStart_ || playlistEdit_) return;
    if (direction != -1 && direction != 1) return;
    client_->Command({"add", "chapter", direction > 0 ? "1" : "-1"});
    // Unlike selecting a chapter explicitly, stepping does not force resume.
}
void PlayerSession::ScreenshotToFile(std::string path, ScreenshotCompleted completion) {
    auto reject = [&](std::string error) {
        snapshot_.error=error; Notify();
        if(completion) completion(false,std::move(error));
    };
    if(screenshotRequest_) { reject("A screenshot is already in progress"); return; }
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
       pauseRequest_ || loadRequest_ || waitingForStart_) {
        reject("Screenshots require stable loaded media"); return;
    }
    if(path.empty() || path.find('\0')!=std::string::npos) {
        reject("Invalid screenshot destination"); return;
    }
    if(std::none_of(snapshot_.tracks.begin(),snapshot_.tracks.end(),[](auto const& track) {
        return track.type=="video" && track.selected;
    })) { reject("Screenshots require an active video track"); return; }
    const char* mode=settings_.screenshotMode==ScreenshotMode::Subtitles ? "subtitles" :
        settings_.screenshotMode==ScreenshotMode::Window ? "window" : "video";
    const auto dot = path.find_last_of('.');
    auto suffix = dot == path.npos ? std::string{} : path.substr(dot + 1);
    for (auto& c : suffix) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    auto format = ParseScreenshotFormat(suffix);
    if (!format) { reject("Unsupported screenshot filename extension"); return; }
    // screenshot-to-file derives encoding from the filename, not the global
    // screenshot-format option. JXL alone uses IINA's software HDR workaround.
    // Await the actual setter reply before issuing the image-write command.
    screenshotRequest_ = client_->Command({"set", "screenshot-sw", *format == ScreenshotFormat::Jxl ? "yes" : "no"});
    if (!screenshotRequest_) { reject("Player is closing"); return; }
    screenshotPreparing_ = true;
    screenshotMediaGeneration_ = windowMediaGeneration_;
    screenshotPath_ = std::move(path); screenshotMode_ = mode;
    screenshotCompleted_ = std::move(completion);
}
void PlayerSession::AddSubtitle(std::string path, bool select) {
    BeginTrackEditIntent();
    CancelSubtitleReload();
    ++subtitleEditIntentRevision_;
    if(snapshot_.session!=SessionState::Ready || path.empty() || path.find('\0')!=std::string::npos) return;
    auto found=std::find_if(snapshot_.tracks.begin(),snapshot_.tracks.end(),[&](auto const& t){return t.type=="sub" && t.externalPath==path;});
    if(found!=snapshot_.tracks.end()) client_->Command({"sub-reload",std::to_string(found->id)});
    else client_->Command({"sub-add",std::move(path),select ? "select" : "auto"});
}
// IINA loadExternalAudioFile uses audio-add with the default select flag.
bool PlayerSession::LoadExternalAudio(std::string path) {
    BeginTrackEditIntent();
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
        pauseRequest_ || loadRequest_ || !pendingCommands_.empty() || PlaylistEditBusy() ||
        path.empty() || path.size()>131072 || path.find('\0')!=std::string::npos) return false;
    // Pass argv directly, never interpret a chosen filename as a command string.
    // Preserve upstream behavior: do not silently use cached or deduplicate paths.
    return client_->Command({"audio-add",std::move(path),"select"}) != 0;
}
void PlayerSession::Pause(bool value) {
    CancelScrub();
    if(!value) sleepPauseHeld_=false;
    CancelWindowPausePolicy();
    if(value) resumePending_=false; // A user pause cancels deferred auto-resume.
    if(snapshot_.session==SessionState::Ready && (value || (presentationReady_ && snapshot_.media==MediaState::Loaded))) client_->Command({"set","pause",value ? "yes" : "no"});
}
void PlayerSession::TogglePause() { CancelScrub(); sleepPauseHeld_=false; CancelWindowPausePolicy(); resumePending_=false; if(snapshot_.session==SessionState::Ready && presentationReady_ && snapshot_.media==MediaState::Loaded) client_->Command({"cycle","pause"}); }
void PlayerSession::Seek(double seconds) {
    CancelScrub();
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
       !snapshot_.duration || *snapshot_.duration<=0 || !std::isfinite(seconds)) return;
    client_->Command({"seek",std::to_string(std::clamp(seconds,0.0,*snapshot_.duration)),"absolute+exact"});
}
void PlayerSession::Stop() {
    BeginMediaOriginIntent(false);
    CancelSubtitleReload();
    CancelWatchedProperties();
    CancelFilterEditCallbacks();
    CancelWindowPausePolicy();
    CancelPlaylistEdits();
    pendingCommands_.clear(); pauseRequest_=0; resumePending_=false; loadRequest_=0;
    transitionIssued_=false; waitingForStart_=false; expectedEntry_=-1;
    if(snapshot_.session==SessionState::Ready) client_->Command({"stop"});
    Notify();
}
void PlayerSession::SetVolume(double value) {
    if(snapshot_.session==SessionState::Ready && std::isfinite(value))
        SubmitVolumeCommand({"set","volume",std::to_string(std::clamp(value,0.0,settings_.maxVolume))});
}
// IINA PlayerCore.setVolume remembers constrainedVolume. Windows waits for
// command success and a serialized readback instead of persisting an optimistic
// UI value. All callbacks and this bookkeeping belong to the UI dispatcher.
void PlayerSession::SubmitVolumeCommand(std::vector<std::string> arguments) {
    if (applyingSettingsVolume_) { client_->Command(std::move(arguments)); return; }
    if (volumeCommands_.size() >= 256) {
        snapshot_.error = "Too many pending volume changes; wait before adjusting volume again";
        Notify(); return;
    }
    // Allocate before submission, so allocation failure cannot orphan a command
    // whose result we promised to remember.
    volumeCommands_.reserve(volumeCommands_.size() + 1);
    auto request = client_->Command(std::move(arguments));
    if (!request) {
        snapshot_.error = "Volume change could not be queued"; Notify(); return;
    }
    ++volumeRevision_;
    volumeCommands_.push_back(request);
}
void PlayerSession::PumpVolumeMemoryRead() {
    if (snapshot_.session != SessionState::Ready || !volumeMemoryDirty_ ||
        !volumeCommands_.empty() || volumeReadRequest_) return;
    volumeReadRequest_ = client_->ReadWatchedPropertiesAsync({"volume"});
    if (volumeReadRequest_) {
        volumeReadRevision_ = volumeRevision_;
        volumeMemoryDirty_ = false;
    } else {
        volumeMemoryDirty_ = false;
        snapshot_.error = "Volume changed, but its readback could not be queued";
        Notify();
    }
}
bool PlayerSession::HandleVolumeMemoryEvent(MpvEvent const& event) {
    if (event.kind == MpvEvent::Kind::Failure || event.kind == MpvEvent::Kind::Shutdown) {
        volumeCommands_.clear(); volumeReadRequest_ = 0; volumeMemoryDirty_ = false;
        return false;
    }
    if (event.kind == MpvEvent::Kind::CommandReply) {
        auto found = std::find(volumeCommands_.begin(), volumeCommands_.end(), event.requestId);
        if (found == volumeCommands_.end()) return false;
        volumeCommands_.erase(found);
        if (event.error >= 0) volumeMemoryDirty_ = true;
        PumpVolumeMemoryRead();
        return false; // Preserve the ordinary command error reporting path.
    }
    if (event.kind != MpvEvent::Kind::WatchedPropertiesReply || !volumeReadRequest_ ||
        event.requestId != volumeReadRequest_) return false;
    volumeReadRequest_ = 0;
    if (!volumeCommands_.empty() || volumeReadRevision_ != volumeRevision_) {
        // A newer intent arrived after this read was enqueued. Wait for its
        // reply, then read once more; never roll remembered volume backward.
        volumeMemoryDirty_ = true;
        PumpVolumeMemoryRead();
        return true;
    }
    bool accepted = false;
    if (event.text.empty() && event.watchedProperties.size() == 1) {
        auto const& property = event.watchedProperties.front();
        double value{};
        const auto [end, error] = std::from_chars(property.value.data(),
            property.value.data() + property.value.size(), value);
        if (property.name == "volume" && property.error.empty() && !property.truncated &&
            error == std::errc{} && end == property.value.data() + property.value.size() &&
            std::isfinite(value) && value >= 0 && value <= 1000) {
            snapshot_.rememberedVolume = std::min(value, settings_.maxVolume);
            ++snapshot_.rememberedVolumeRevision;
            accepted = true;
        }
    }
    if (!accepted) snapshot_.error = "Volume changed, but its value could not be read back for the next player";
    Notify();
    PumpVolumeMemoryRead();
    return true;
}
void PlayerSession::SetSpeed(double value) {
    if(snapshot_.session==SessionState::Ready && std::isfinite(value) && value>=0.01 && value<=100)
        client_->Command({"set","speed",std::to_string(value)});
}
// IINA windowScaleChanged/updateWindowParametersForMPV use a cached scale to
// suppress self-echoes. Async mpv needs a reply/readback barrier instead of an
// optimistic cache, plus one coalesced latest UI value during that barrier.
namespace {
bool ValidWindowScale(double value) { return std::isfinite(value) && value > 0 && value <= 16; }
bool SameWindowScale(double a, double b) { return std::abs(a-b) <= 1e-9; }
}
void PlayerSession::SetWindowScaleFromWindow(double scale) {
    if (!ValidWindowScale(scale) || snapshot_.session != SessionState::Ready ||
        snapshot_.media != MediaState::Loaded || !snapshot_.mediaVideoGeometryReady ||
        waitingForStart_) return;
    // Repeated snapshots must not retry a rejected/uncertain target forever.
    // Only a different actual viewport target (or media reset) releases it.
    if (blockedWindowScale_) {
        if (SameWindowScale(scale, *blockedWindowScale_)) return;
        blockedWindowScale_.reset();
    }
    pendingWindowScale_ = scale;
    PumpWindowScale();
}
void PlayerSession::PumpWindowScale() {
    if (windowScaleWrite_ || windowScaleRead_ || !pendingWindowScale_) return;
    const auto scale = *pendingWindowScale_;
    pendingWindowScale_.reset();
    if (blockedWindowScale_ && !SameWindowScale(scale, *blockedWindowScale_))
        blockedWindowScale_.reset(); // Newer target was coalesced before failure arrived.
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !snapshot_.mediaVideoGeometryReady || waitingForStart_ ||
        (blockedWindowScale_ && SameWindowScale(scale, *blockedWindowScale_))) return;
    if (confirmedWindowScale_ && SameWindowScale(scale, *confirmedWindowScale_)) {
        // No write is needed, but the actual viewport has now settled at this
        // confirmed value. Retire the older target's echo guard after the
        // barrier drained, so an external 1 -> 2 -> 1 remains actionable.
        windowScaleEcho_ = scale;
        return;
    }
    char text[64];
    auto [end, error] = std::to_chars(text, text+sizeof(text), scale, std::chars_format::general, 17);
    if (error != std::errc{}) { blockedWindowScale_=scale; return; }
    windowScaleWrite_ = client_->Command({"set", "window-scale", std::string(text,end)});
    if (!windowScaleWrite_) { blockedWindowScale_=scale; return; }
    windowScaleWriteSucceeded_ = false;
    windowScaleEcho_ = scale; // Echo guard only; never an observed value.
    windowScaleGeneration_ = snapshot_.mediaGeneration;
    windowScaleIntent_ = snapshot_.mediaOpenIntentRevision;
}
void PlayerSession::BlockWindowScaleRetry() {
    // A failed older-media write must not block a new generation's target.
    if (windowScaleGeneration_==snapshot_.mediaGeneration &&
        windowScaleIntent_==snapshot_.mediaOpenIntentRevision)
        blockedWindowScale_=windowScaleEcho_;
}
void PlayerSession::PublishExternalWindowScale(double scale) {
    if (!ValidWindowScale(scale)) return;
    const bool changed = !confirmedWindowScale_ || !SameWindowScale(scale, *confirmedWindowScale_);
    confirmedWindowScale_ = scale;
    if (!changed || snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !snapshot_.mediaVideoGeometryReady || waitingForStart_) return;
    snapshot_.externalWindowScale = scale;
    ++snapshot_.externalWindowScaleRevision;
}
bool PlayerSession::HandleWindowScaleEvent(MpvEvent const& event) {
    if (event.kind == MpvEvent::Kind::Failure || event.kind == MpvEvent::Kind::Shutdown) {
        pendingWindowScale_.reset(); windowScaleWrite_=0; windowScaleRead_=0;
        snapshot_.externalWindowScale.reset();
        return false;
    }
    if (event.kind == MpvEvent::Kind::StartFile ||
        (event.kind == MpvEvent::Kind::EndFile && event.entryId == snapshot_.entryId)) {
        // Do not discard in-flight IDs: those writes still execute in mpv and
        // must be drained without publishing their late echoes in a new media.
        pendingWindowScale_.reset(); snapshot_.externalWindowScale.reset();
        if (event.kind == MpvEvent::Kind::StartFile) blockedWindowScale_.reset();
        return false;
    }
    if (event.kind == MpvEvent::Kind::CommandReply && windowScaleWrite_ &&
        event.requestId == windowScaleWrite_) {
        windowScaleWrite_=0;
        windowScaleWriteSucceeded_=event.error>=0;
        if (!windowScaleWriteSucceeded_) BlockWindowScaleRetry();
        // Even failure needs readback: an independent external setter may have
        // changed the value while observer notifications were suppressed.
        windowScaleRead_=client_->GetWindowScaleAsync();
        if (!windowScaleRead_) { BlockWindowScaleRetry(); PumpWindowScale(); }
        return true;
    }
    if (event.kind == MpvEvent::Kind::WindowScaleReply) {
        if (!windowScaleRead_ || event.requestId != windowScaleRead_) return true;
        windowScaleRead_=0;
        const auto revision = snapshot_.externalWindowScaleRevision;
        if (event.error>=0 && event.number && ValidWindowScale(*event.number)) {
            const bool current = windowScaleGeneration_==snapshot_.mediaGeneration &&
                windowScaleIntent_==snapshot_.mediaOpenIntentRevision;
            const bool echo = windowScaleEcho_ && SameWindowScale(*event.number,*windowScaleEcho_);
            // With a known pre-write baseline, even a failed setter can reveal
            // an independent external change. An unchanged old value dedups.
            // Without any baseline, failed-set readback is ambiguous, not proof
            // of an external request to resize the window back.
            if (current && (windowScaleWriteSucceeded_ || confirmedWindowScale_) &&
                !pendingWindowScale_ && !echo)
                PublishExternalWindowScale(*event.number);
            else confirmedWindowScale_=*event.number;
            // A clamped or externally superseded target should not be written
            // again on every unchanged viewport notification either.
            if (!echo) BlockWindowScaleRetry();
        } else BlockWindowScaleRetry();
        // Retain the last local target as a bounded ambiguous-echo guard even
        // after readback: delayed/coalesced equal observations cannot resize
        // the UI. A genuine external write of that same value is conservatively
        // ignored until a different actual local target replaces this guard,
        // including a settled target already equal to confirmed readback.
        PumpWindowScale();
        if (snapshot_.externalWindowScaleRevision != revision) Notify();
        return true;
    }
    if (event.kind == MpvEvent::Kind::Property && event.name=="window-scale") {
        if (windowScaleWrite_ || windowScaleRead_ || !event.number || !ValidWindowScale(*event.number)) return true;
        if (windowScaleEcho_ && SameWindowScale(*event.number,*windowScaleEcho_)) return true;
        const auto revision = snapshot_.externalWindowScaleRevision;
        PublishExternalWindowScale(*event.number);
        if (snapshot_.externalWindowScaleRevision != revision) Notify();
        return true;
    }
    return false;
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
    // Applying startup/preferences is not an explicit user volume action.
    // Even a reduced maximum must not convert the initial value into memory.
    struct RestoreFlag { bool& flag; bool old; ~RestoreFlag() { flag = old; } };
    {
        RestoreFlag restore{applyingSettingsVolume_, applyingSettingsVolume_};
        applyingSettingsVolume_ = true;
        SetMaxVolume(settings.maxVolume);
        SetVolume(settings.volume);
    }
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
bool PlayerSession::ReadWatchedProperties(std::vector<std::string> names, WatchedPropertiesCompleted completion) {
    auto reject = [&](std::string error) { if (completion) completion({}, std::move(error)); return false; };
    if (watchedPropertiesRequest_) return reject("A property refresh is already pending");
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded || waitingForStart_)
        return reject("No active loaded media");
    if (names.empty() || names.size() > MaxWatchedProperties) return reject("Watch list must contain 1 to 32 properties");
    for (auto const& name : names) if (!ValidateWatchedPropertyName(name)) return reject("Invalid property name");
    const auto request = client_->ReadWatchedPropertiesAsync(std::move(names));
    if (!request) return reject("Playback worker rejected the property refresh");
    watchedPropertiesRequest_ = request;
    watchedPropertiesCompleted_ = std::move(completion);
    return true;
}
void PlayerSession::CancelWatchedProperties() {
    watchedPropertiesRequest_ = 0;
    auto completion = std::move(watchedPropertiesCompleted_);
    watchedPropertiesCompleted_ = {};
    if (completion) completion({}, "Property refresh cancelled");
}
void PlayerSession::OnEvent(MpvEvent e) {
    if(snapshot_.session==SessionState::Closing || snapshot_.session==SessionState::Closed) return;
    HandleNetworkOpenEvent(e);
    // A newer user selection owns the outcome; a failed superseded fallback
    // must not replace its error state or imply the user's selection failed.
    if (e.kind==MpvEvent::Kind::CommandReply && audioDeviceRecovery_ &&
        e.requestId==audioDeviceRecovery_ && audioDeviceRecoveryIntent_!=audioDeviceIntent_) {
        e.error=0; e.text.clear();
    }
    // The worker may apply pauseWhenOpen directly on Loaded/EndFile, even
    // before the UI reducer checks an EndFile entry identity. Revoke ownership.
    if(e.kind==MpvEvent::Kind::Loaded || e.kind==MpvEvent::Kind::EndFile) CancelScrub();
    if(e.kind==MpvEvent::Kind::PauseReply) { HandleScrubEvent(e); Notify(); return; }
    if (HandleWindowScaleEvent(e)) return;
    if (e.kind == MpvEvent::Kind::VideoGeometryReply) {
        if (!videoGeometryRequest_ || e.requestId != videoGeometryRequest_) return;
        videoGeometryRequest_ = 0;
        if (e.error < 0 || snapshot_.mediaGeneration != videoGeometryGeneration_ ||
            snapshot_.mediaOpenIntentRevision != videoGeometryIntent_ ||
            snapshot_.media != MediaState::Loaded) return;
        auto number = [&](char const* key) -> std::optional<double> {
            auto found = e.properties.find(key);
            if (found == e.properties.end()) return {};
            double value{};
            auto const& text = found->second;
            auto [end, error] = std::from_chars(text.data(), text.data()+text.size(), value);
            if (error != std::errc{} || end != text.data()+text.size() || !std::isfinite(value)) return {};
            return value;
        };
        auto width = number("dw"), height = number("dh"), rotation = number("rotate");
        if (!width || !height || !rotation || *width <= 0 || *height <= 0 ||
            *width > 65536 || *height > 65536 || *rotation < 0 || *rotation >= 360 ||
            std::fmod(*rotation, 90.0) != 0) return;
        snapshot_.videoOutputParameters = std::move(e.properties);
        ++snapshot_.videoOutputRevision;
        snapshot_.mediaVideoGeometryReady = true;
        Notify();
        return;
    }
    if (e.kind == MpvEvent::Kind::StartFile) {
        videoGeometryRequest_ = 0;
        videoGeometryAttempts_ = 0;
    } else if ((e.kind == MpvEvent::Kind::EndFile && e.entryId == snapshot_.entryId) ||
               e.kind == MpvEvent::Kind::Failure || e.kind == MpvEvent::Kind::Shutdown) {
        videoGeometryRequest_ = 0;
        snapshot_.mediaVideoGeometryReady = false;
    }
    if (HandleVolumeMemoryEvent(e)) return;
    if (e.kind == MpvEvent::Kind::WatchedPropertiesReply) {
        if (watchedPropertiesRequest_ && e.requestId == watchedPropertiesRequest_) {
            watchedPropertiesRequest_ = 0;
            auto completion = std::move(watchedPropertiesCompleted_);
            watchedPropertiesCompleted_ = {};
            if (completion) completion(std::move(e.watchedProperties), std::move(e.text));
        }
        return;
    }
    if (e.kind == MpvEvent::Kind::SeekMeasurement) {
        if (e.requestId == seekMeasurementRequest_) {
            seekMeasurementRequest_ = 0;
            if (e.number) automaticSeekPolicy_.ObserveSeekElapsed(seekMediaGeneration_,*e.number);
        }
        return;
    }
    if (e.kind == MpvEvent::Kind::StartFile) {
        seekMeasurementRequest_ = 0;
        automaticSeekPolicy_.BeginMedia(++seekMediaGeneration_);
    } else if (e.kind == MpvEvent::Kind::EndFile || e.kind == MpvEvent::Kind::Failure || e.kind == MpvEvent::Kind::Shutdown)
        seekMeasurementRequest_ = 0;
    if (e.kind == MpvEvent::Kind::Failure || e.kind == MpvEvent::Kind::Shutdown ||
        e.kind == MpvEvent::Kind::StartFile || e.kind == MpvEvent::Kind::EndFile) CancelWatchedProperties();
    if(e.kind==MpvEvent::Kind::Failure || e.kind==MpvEvent::Kind::Shutdown || e.kind==MpvEvent::Kind::EndFile) CancelWindowPausePolicy();
    if (e.kind == MpvEvent::Kind::Failure || e.kind == MpvEvent::Kind::Shutdown ||
        e.kind == MpvEvent::Kind::StartFile || e.kind == MpvEvent::Kind::EndFile) {
        CancelExternalVideoLoad();
        CancelExternalTrackRemoval();
        CancelSubtitleReload();
    }
    if (e.kind == MpvEvent::Kind::Failure || e.kind == MpvEvent::Kind::Shutdown) {
        externalVideoLoadRequest_ = 0;
        externalVideoLoadStatus_.inFlight = false;
        externalVideoLoadCancelled_.reset();
        externalTrackRemovalRequest_ = 0;
        externalTrackRemovalStatus_.inFlight = false;
        externalTrackRemovalCancelled_.reset();
        subtitleReloadRequest_ = 0;
        subtitleReloadStatus_.inFlight = false;
    }
    if (HandleExternalVideoLoadReply(e)) return;
    if (HandleExternalTrackRemovalReply(e)) return;
    if (HandleSubtitleReloadReply(e)) return;
    HandleWindowPauseReply(e);
    if(e.kind==MpvEvent::Kind::Failure || e.kind==MpvEvent::Kind::Shutdown) CancelPlaylistEdits();
    if(e.kind==MpvEvent::Kind::Failure && screenshotRequest_) {
        screenshotRequest_=0; screenshotPreparing_=false;
        screenshotPath_.clear(); screenshotMode_.clear();
        auto completion=std::move(screenshotCompleted_);
        screenshotCompleted_={};
        // The worker may still be terminating. Wait for its write barrier
        // before the UI callback can dispose the screenshot staging owner.
        if(completion) client_->CloseAsync([dispatch=dispatch_,completion=std::move(completion)] {
            dispatch([completion] { completion(false,"Playback worker failed during screenshot"); });
        });
    }
    if(e.kind==MpvEvent::Kind::CommandReply && screenshotRequest_ && e.requestId==screenshotRequest_) {
        screenshotRequest_=0;
        if (screenshotPreparing_) {
            screenshotPreparing_ = false;
            const bool stable = screenshotMediaGeneration_ == windowMediaGeneration_ &&
                snapshot_.session == SessionState::Ready && snapshot_.media == MediaState::Loaded &&
                !pauseRequest_ && !loadRequest_ && !waitingForStart_;
            if (e.error >= 0 && stable) {
                screenshotRequest_ = client_->Command({"screenshot-to-file", std::move(screenshotPath_), screenshotMode_});
            }
            screenshotPath_.clear(); screenshotMode_.clear();
            if (!screenshotRequest_) {
                auto completion = std::move(screenshotCompleted_); screenshotCompleted_ = {};
                if (completion) completion(false, e.error < 0 ? "mpv rejected the screenshot rendering mode" :
                    "Media changed or player closed before screenshot submission");
            }
        } else {
            auto completion=std::move(screenshotCompleted_);
            screenshotCompleted_={};
            // The reply is the file-write completion barrier. A change of media
            // after this submitted request does not invalidate its captured file.
            if(completion) completion(e.error>=0,e.error>=0 ? std::string{} :
                (e.text.empty() ? "mpv could not save the screenshot (encoder may be unavailable)" : e.text));
        }
    }
    if(HandlePlaylistEditEvent(e)) return;
    if(e.kind==MpvEvent::Kind::CommandReply && e.requestId==pauseRequest_) {
        pauseRequest_=0;
        if(e.error>=0) for(auto& command:pendingCommands_) {
            const bool firstLoad=command.size()>2 && command[0]=="loadfile" && command[2]=="replace";
            const bool playlistSwitch=!command.empty() &&
                (command[0]=="playlist-next" || command[0]=="playlist-prev" || command[0]=="playlist-play-index");
            const auto id=client_->Command(std::move(command));
            if(firstLoad || playlistSwitch) {
                if (firstLoad && snapshot_.mediaOriginPending) {
                    openOriginRequest_ = id;
                    if (!id) snapshot_.mediaOriginPending = false;
                }
                if (firstLoad && snapshot_.networkOpenAttempt.intent==snapshot_.mediaOpenIntentRevision &&
                    snapshot_.networkOpenAttempt.phase==OpenAttemptPhase::Pending) {
                    networkLoadRequest_=id;
                    if (!id) snapshot_.networkOpenAttempt.phase=OpenAttemptPhase::Failed;
                }
                loadRequest_=id;
                transitionIssued_=id!=0;
                if(!id) resumePending_=false;
            }
        }
        else { resumePending_=false; snapshot_.mediaOriginPending=false; }
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
    ObserveMediaOrigin(e);
    if(previousChain && !snapshot_.swapChain) CancelScrub();
    HandleScrubEvent(e);
    UpdateAudioDeviceRecovery(e);
    const bool geometryWake = e.kind == MpvEvent::Kind::Restart ||
        (snapshot_.mediaPlaybackRestarted && (e.kind == MpvEvent::Kind::VideoReconfig ||
            (e.kind == MpvEvent::Kind::Property && e.name == "video-out-params")));
    if (geometryWake && snapshot_.media == MediaState::Loaded &&
        !snapshot_.mediaVideoGeometryReady && !videoGeometryRequest_ && videoGeometryAttempts_ < 3) {
        // Bounded event-driven reads, never polling. A later output/restart may
        // recover an unavailable initial output. Async GET shares START_FILE's
        // queue, so a queued later start invalidates the generation first.
        ++videoGeometryAttempts_;
        videoGeometryGeneration_ = snapshot_.mediaGeneration;
        videoGeometryIntent_ = snapshot_.mediaOpenIntentRevision;
        videoGeometryRequest_ = client_->GetVideoGeometryAsync();
    }
    HandleFilterEditReply(e);
    if(e.kind==MpvEvent::Kind::StartFile) {
        CancelWindowPausePolicy();
        windowPausePolicy_.MediaChanged(++windowMediaGeneration_,snapshot_.paused);
    }
    if(e.kind==MpvEvent::Kind::Property && e.name=="pause" && e.flag) {
        windowPausePolicy_.ObservePaused(*e.flag);
        RefreshWindowPausePolicy();
    }
    if(e.kind==MpvEvent::Kind::Loaded) RefreshWindowPausePolicy();
    if(e.kind==MpvEvent::Kind::Ready) ApplyAdvancedPreferencesOnReady();
    if(e.kind==MpvEvent::Kind::Ready && applySettingsOnReady_) { applySettingsOnReady_=false; ApplySettings(settings_); }
    if(!sleepPauseHeld_ && !pauseRequest_ && !loadRequest_ && !waitingForStart_ &&
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
    pendingAudioDevice_.reset(); audioDeviceWrite_=0; audioDeviceRecovery_=0;
    BeginMediaOriginIntent(false);
    CancelSubtitleReload();
    // Mark closing before cancellation so reentrant UI callbacks cannot enqueue work.
    if (snapshot_.session != SessionState::Closed) snapshot_.session = SessionState::Closing;
    CancelWatchedProperties();
    CancelFilterEditCallbacks();
    CancelWindowPausePolicy();
    CancelPlaylistEdits();
    if(snapshot_.session!=SessionState::Closed) snapshot_.session=SessionState::Closing;
    pendingCommands_.clear(); pauseRequest_=0; loadRequest_=0; transitionIssued_=false; waitingForStart_=false; presentationReady_=false; resumePending_=false; snapshot_.swapChain.reset(); Notify();
    auto weak=weak_from_this();
    auto screenshotCompletion=std::move(screenshotCompleted_);
    screenshotCompleted_={}; screenshotRequest_=0; screenshotPreparing_=false;
    screenshotPath_.clear(); screenshotMode_.clear();
    client_->CloseAsync([weak, dispatch=dispatch_, completion=std::move(completion),
                         screenshotCompletion=std::move(screenshotCompletion)] {
        dispatch([weak,completion,screenshotCompletion] {
            if(auto self=weak.lock()) {
                self->externalVideoLoadRequest_=0; self->externalVideoLoadStatus_.inFlight=false;
                self->externalVideoLoadCancelled_.reset();
                self->externalTrackRemovalRequest_=0; self->externalTrackRemovalStatus_.inFlight=false;
                self->externalTrackRemovalCancelled_.reset();
                self->subtitleReloadRequest_=0; self->subtitleReloadStatus_.inFlight=false;
                self->snapshot_.session=SessionState::Closed; self->Notify();
            }
            if(screenshotCompletion) screenshotCompletion(false,"Player closed before screenshot completion");
            if(completion) completion();
        });
    });
}
}

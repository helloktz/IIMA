// IINA PlaySliderCell.startTracking/stopTracking, with asynchronous ownership.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "PlayerSession.h"
#include <algorithm>
#include <cmath>
namespace iima {
bool PlayerSession::ScrubMediaCurrent() const {
    return scrubToken_ && snapshot_.session==SessionState::Ready && snapshot_.media==MediaState::Loaded &&
        snapshot_.mediaGeneration==scrubGeneration_ && snapshot_.mediaOpenIntentRevision==scrubIntent_ &&
        !snapshot_.mediaOriginPending && snapshot_.seekable && snapshot_.duration &&
        std::isfinite(*snapshot_.duration) && *snapshot_.duration>0;
}
std::uint64_t PlayerSession::BeginScrub() {
    if(scrubToken_ || snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
        !presentationReady_ || !snapshot_.seekable || !snapshot_.duration || !std::isfinite(*snapshot_.duration) ||
        *snapshot_.duration<=0 || snapshot_.mediaOriginPending || pauseRequest_ || loadRequest_ ||
        waitingForStart_ || resumePending_ || sleepPauseHeld_ || windowPauseRequest_ ||
        windowInactive_ || windowMinimized_ || PlaylistEditBusy() || !pendingCommands_.empty()) return 0;
    CancelWindowPausePolicy();
    scrubToken_=++scrubSerial_;scrubGeneration_=snapshot_.mediaGeneration;scrubIntent_=snapshot_.mediaOpenIntentRevision;
    scrubEnding_=false;scrubPausedReady_=false;scrubResumeWanted_=false;
    scrubLatest_.reset();scrubSubmitted_.reset();scrubStage_=ScrubStage::Baseline;
    // A queued user Pause/Toggle may not have reached observed state. A real
    // async property reply behind earlier commands establishes the baseline.
    scrubRequest_=client_->GetPauseAsync();
    if(!scrubRequest_) {FailScrub("Could not read playback state for seeking");return 0;}
    Notify();return scrubToken_;
}
bool PlayerSession::UpdateScrub(std::uint64_t token,double seconds) {
    if(!token || token!=scrubToken_ || scrubEnding_ || !std::isfinite(seconds)) return false;
    if(!ScrubMediaCurrent()) {CancelScrub();return false;}
    const auto target=std::clamp(seconds,0.0,*snapshot_.duration);
    // At most one mpv operation plus one latest target, never 60Hz commands.
    scrubLatest_=target;
    PumpScrub();return token==scrubToken_;
}
void PlayerSession::EndScrub(std::uint64_t token) {
    if(!token || token!=scrubToken_) return;
    scrubEnding_=true;PumpScrub();
}
void PlayerSession::CancelScrub() {
    if(!scrubToken_) return;
    scrubToken_=0;scrubRequest_=0;scrubLatest_.reset();scrubSubmitted_.reset();
    scrubEnding_=false;scrubPausedReady_=false;scrubResumeWanted_=false;
    Notify();
}
void PlayerSession::FailScrub(std::string error) {
    snapshot_.error=std::move(error);
    CancelScrub(); // Conservative Windows cancellation: never auto-resume.
}
void PlayerSession::PumpScrub() {
    if(!scrubToken_) return;
    if(!ScrubMediaCurrent() || !presentationReady_ || sleepPauseHeld_ || windowInactive_ || windowMinimized_) {CancelScrub();return;}
    if(scrubRequest_ || !scrubPausedReady_) return;
    if(scrubLatest_) {
        const auto target=*scrubLatest_;scrubLatest_.reset();
        if(!scrubSubmitted_ || target!=*scrubSubmitted_) {
            scrubSubmitted_=target;scrubStage_=ScrubStage::Seek;
            scrubRequest_=client_->Command({"seek",std::to_string(target),"absolute+exact"});
            if(!scrubRequest_) FailScrub("Could not submit scrub seek; playback was not resumed");
            return;
        }
    }
    if(!scrubEnding_) return;
    if(!scrubResumeWanted_) {CancelScrub();return;}
    scrubStage_=ScrubStage::ResumeCheck;
    scrubRequest_=client_->GetPauseAsync();
    if(!scrubRequest_) FailScrub("Could not confirm scrub pause; playback was not resumed");
}
void PlayerSession::HandleScrubEvent(MpvEvent const& event) {
    if(!scrubToken_) return;
    if(!ScrubMediaCurrent()) {CancelScrub();return;}
    // Once pause ownership is established, observed resume from another source
    // revokes it. Same-value external pauses cannot be identified by libmpv.
    if(event.kind==MpvEvent::Kind::Property && event.name=="pause" && event.flag && !*event.flag &&
        scrubPausedReady_ && scrubStage_!=ScrubStage::Resume) {CancelScrub();return;}
    if(!scrubRequest_ || event.requestId!=scrubRequest_ ||
        (event.kind!=MpvEvent::Kind::CommandReply && event.kind!=MpvEvent::Kind::PauseReply)) return;
    scrubRequest_=0;
    if(event.error<0) {FailScrub("Scrub operation failed; playback was not automatically resumed");return;}
    switch(scrubStage_) {
    case ScrubStage::Baseline:
        if(!event.flag) {FailScrub("Playback pause state is unavailable");return;}
        scrubResumeWanted_=!*event.flag;
        if(*event.flag) scrubPausedReady_=true;
        else {
            scrubStage_=ScrubStage::Pause;
            scrubRequest_=client_->Command({"set","pause","yes"});
            if(!scrubRequest_) {FailScrub("Could not pause for seeking");return;}
        }
        break;
    case ScrubStage::Pause:
        scrubStage_=ScrubStage::PauseCheck;scrubRequest_=client_->GetPauseAsync();
        if(!scrubRequest_) {FailScrub("Could not confirm pause for seeking");return;}
        break;
    case ScrubStage::PauseCheck:
        if(!event.flag || !*event.flag) {FailScrub("Playback did not remain paused for seeking");return;}
        scrubPausedReady_=true;break;
    case ScrubStage::Seek: break; // Command completion, not proof the target frame has been presented.
    case ScrubStage::ResumeCheck:
        if(!event.flag || !*event.flag) {CancelScrub();return;}
        scrubStage_=ScrubStage::Resume;
        scrubRequest_=client_->Command({"set","pause","no"});
        if(!scrubRequest_) {FailScrub("Could not resume after seeking");return;}
        break;
    case ScrubStage::Resume:
        CancelScrub();return;
    }
    PumpScrub();
}
}

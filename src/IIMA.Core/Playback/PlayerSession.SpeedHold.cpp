// IINA MainWindowController.leftButtonAction/rightButtonAction, ordinary presses.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Windows does not emulate Force Touch or resume paused playback implicitly.
#include "PlayerSession.h"
#include "SpeedHoldPolicy.h"
namespace iima {
bool PlayerSession::SpeedHoldContextValid() const {
    return speedHoldToken_ && snapshot_.session==SessionState::Ready && snapshot_.media==MediaState::Loaded &&
        !waitingForStart_ && !snapshot_.mediaOriginPending && !playlistEdit_ &&
        snapshot_.mediaGeneration==speedHoldGeneration_ && snapshot_.mediaOpenIntentRevision==speedHoldOpenIntent_ &&
        snapshot_.entryId==speedHoldEntry_ && snapshot_.path==speedHoldMediaPath_ && speedIntentRevision_==speedHoldIntent_;
}
void PlayerSession::CancelSpeedHold() {
    if(speedHoldCancelled_) speedHoldCancelled_->store(true);
    if(!speedHoldToken_) return;
    speedHoldToken_=0;
    if(!speedHoldRequest_) speedHoldCancelled_.reset();
    // Keep a submitted request until its own reply drains. Cancellation does
    // not restore speed, nor promise to recall a command already handed to mpv.
    Notify();
}
bool PlayerSession::SubmitSpeedHold(double expected,double target) {
    try {speedHoldRequest_=client_->CompareSetSpeedAsync(expected,target,speedHoldEntry_,speedHoldMediaPath_,speedHoldCancelled_);}
    catch(...) {speedHoldRequest_=0;}
    if(!speedHoldRequest_) {
        speedHoldToken_=0;speedHoldCancelled_.reset();
        snapshot_.error="Speed adjustment could not be queued; no automatic retry or restoration";
    }
    return speedHoldRequest_!=0;
}
std::uint64_t PlayerSession::BeginSpeedHold(int direction) {
    if(!snapshot_.speedObserved || SpeedHoldBusy() || snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
        waitingForStart_ || snapshot_.mediaOriginPending || pauseRequest_ || loadRequest_ ||
        !pendingCommands_.empty() || playlistEdit_ || snapshot_.entryId<0 || snapshot_.path.empty() ||
        snapshot_.path.size()>131072 || windowInactive_ || windowMinimized_) return 0;
    auto plan=PlanSpeedHold(snapshot_.speed,direction);
    if(!plan) return 0;
    speedHoldToken_=++speedHoldSerial_;speedHoldIntent_=++speedIntentRevision_;
    speedHoldGeneration_=snapshot_.mediaGeneration;speedHoldOpenIntent_=snapshot_.mediaOpenIntentRevision;
    speedHoldEntry_=snapshot_.entryId;speedHoldMediaPath_=snapshot_.path;
    speedHoldTarget_=plan->target;speedHoldRestoreEligible_=plan->restoreOnLongRelease;
    speedHoldEnding_=false;speedHoldConfirmed_=false;speedHoldRestoring_=false;speedHoldRestoreWanted_=false;
    speedHoldStarted_=std::chrono::steady_clock::now();
    try {speedHoldCancelled_=std::make_shared<std::atomic_bool>(false);}
    catch(...) {speedHoldToken_=0;snapshot_.error="Could not allocate speed adjustment request";Notify();return 0;}
    SubmitSpeedHold(snapshot_.speed,speedHoldTarget_);
    Notify();return speedHoldToken_;
}
void PlayerSession::EndSpeedHold(std::uint64_t token) {
    if(!token || token!=speedHoldToken_ || speedHoldEnding_) return;
    if(!SpeedHoldContextValid()) {CancelSpeedHold();return;}
    speedHoldEnding_=true;
    speedHoldRestoreWanted_=speedHoldRestoreEligible_ &&
        std::chrono::steady_clock::now()-speedHoldStarted_>=std::chrono::milliseconds(500);
    FinishSpeedHoldRelease();Notify();
}
void PlayerSession::FinishSpeedHoldRelease() {
    if(!speedHoldEnding_ || speedHoldRequest_ || !speedHoldConfirmed_ || !SpeedHoldContextValid()) return;
    if(speedHoldRestoreWanted_) {
        speedHoldRestoring_=true;
        // Worker compares fresh current speed with the confirmed held value;
        // another setter's change must not be overwritten by mouse-up.
        SubmitSpeedHold(speedHoldTarget_,1.0);
    } else {speedHoldToken_=0;speedHoldCancelled_.reset();}
}
bool PlayerSession::HandleSpeedHoldReply(MpvEvent const& event) {
    if(event.kind!=MpvEvent::Kind::SpeedEditReply) return false;
    if(!speedHoldRequest_ || event.requestId!=speedHoldRequest_) return true;
    speedHoldRequest_=0;
    if(!SpeedHoldContextValid() || (speedHoldCancelled_ && speedHoldCancelled_->load())) {
        speedHoldToken_=0;speedHoldCancelled_.reset();Notify();return true;
    }
    if(event.error<0 || !event.speedEditCommandSucceeded || !event.speedEditConfirmed) {
        speedHoldToken_=0;speedHoldCancelled_.reset();
        snapshot_.error=event.speedEditConfirmed ?
            "Speed was confirmed but the command reported an error; no further restoration was attempted" :
            event.speedEditSubmitted ? "Speed adjustment was submitted but could not be confirmed; no automatic retry or restoration" :
            "Speed or media changed before adjustment; no speed command was submitted";
    } else if(speedHoldRestoring_) {
        speedHoldToken_=0;speedHoldCancelled_.reset();
    } else {
        speedHoldConfirmed_=true;
        FinishSpeedHoldRelease();
    }
    // Only the normal speed property observation updates playback/UI truth.
    Notify();return true;
}
}

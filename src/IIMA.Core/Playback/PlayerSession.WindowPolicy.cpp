// IINA MainWindowController activation/miniaturization policy, integrated with
// asynchronous mpv commands rather than Cocoa synchronous delegates. GPL-3.0.
#include "PlayerSession.h"

namespace iima {
void PlayerSession::SystemWillSuspend() {
    // PlayerWindowController.willSleepNotification -> PlayerCore.pause.
    // See upstream issue #4520: avoid producing idle rendering work during sleep.
    if(snapshot_.session!=SessionState::Ready ||
       (snapshot_.media!=MediaState::Loading && snapshot_.media!=MediaState::Loaded)) return;
    // Even if already paused, revoke earlier focus/minimize restoration and the
    // current load's deferred auto-resume. Waking must not undo the sleep pause.
    sleepPauseHeld_=true;
    Pause(true);
}
void PlayerSession::WindowLifecycleChanged(bool inactive,bool minimized) {
    windowInactive_=inactive;
    windowMinimized_=minimized;
    RefreshWindowPausePolicy();
}
void PlayerSession::CancelWindowPausePolicy() {
    windowPausePolicy_.CancelAutomaticResume();
    windowPauseRequest_=0;
    windowPauseDirective_.reset();
}
void PlayerSession::RefreshWindowPausePolicy() {
    if(sleepPauseHeld_ || snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
        pauseRequest_ || loadRequest_ || waitingForStart_) return;
    SubmitWindowPauseDirective(windowPausePolicy_.SetWindowState(windowInactive_,windowMinimized_,advancedPreferences_));
}
void PlayerSession::SubmitWindowPauseDirective(std::optional<settings::WindowPauseDirective> directive) {
    if(!directive) return;
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
       (!directive->paused && !presentationReady_)) {
        // A failed resume retains ownership and can be retried when its current
        // swapchain is bound; an unsupported pause grants no restore authority.
        windowPausePolicy_.Complete(*directive,false);
        return;
    }
    const auto id=client_->Command({"set","pause",directive->paused?"yes":"no"});
    if(!id) {
        windowPausePolicy_.Complete(*directive,false);
        return;
    }
    windowPauseRequest_=id;
    windowPauseDirective_=*directive;
}
void PlayerSession::HandleWindowPauseReply(MpvEvent const& event) {
    if(event.kind!=MpvEvent::Kind::CommandReply || !windowPauseRequest_ ||
       event.requestId!=windowPauseRequest_ || !windowPauseDirective_) return;
    const auto directive=*windowPauseDirective_;
    windowPauseRequest_=0;
    windowPauseDirective_.reset();
    auto next=windowPausePolicy_.Complete(directive,event.error>=0);
    // Only successful pause completion can grant restoration ownership. Errors
    // still flow into the common playback reducer and visible status message.
    SubmitWindowPauseDirective(next);
}
}

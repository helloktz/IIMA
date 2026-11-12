// Derived from IINA MainWindowController.swift. GPL-3.0; references/iina/LICENSE.
#include "WindowPlaybackPolicy.h"
#include <limits>
#include <stdexcept>

namespace iima::settings {
void WindowPlaybackPolicy::MediaChanged(std::uint64_t generation, bool paused) {
    generation_=generation;
    observedPaused_=paused;
    ownsPause_=false;
    pending_.reset();
    reasons_=0;
    cancelled_=false;
}
void WindowPlaybackPolicy::CancelAutomaticResume() noexcept {
    ownsPause_=false;
    pending_.reset();
    cancelled_=true;
}
std::optional<WindowPauseDirective> WindowPlaybackPolicy::Issue(bool paused) {
    if(serial_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Window playback policy token exhausted");
    pending_=WindowPauseDirective{++serial_,generation_,paused};
    return pending_;
}
std::optional<WindowPauseDirective> WindowPlaybackPolicy::SetReason(
    WindowPauseReason reason, bool active, PreferenceStore const& preferences) {
    const auto mask=static_cast<unsigned>(reason);
    const bool enabled=reason==WindowPauseReason::Inactive
        ?preferences.Boolean("pauseWhenInactive"):preferences.Boolean("pauseWhenMinimized");
    const auto previous=reasons_;
    if(active && enabled) reasons_|=mask;
    else reasons_&=~mask;
    // Explicit user intent suppresses automatic actions for the current absence.
    // Only a completely new absence after restoration can acquire ownership.
    if(!previous && reasons_) cancelled_=false;
    return Reconcile();
}
std::optional<WindowPauseDirective> WindowPlaybackPolicy::Reconcile() {
    if(cancelled_ || pending_) return {};
    if(reasons_) {
        // Already-paused media must not become ours merely because its window
        // was hidden. Restoring that window must leave the manual pause intact.
        if(!ownsPause_ && !observedPaused_) return Issue(true);
        return {};
    }
    if(ownsPause_) return Issue(false);
    return {};
}
std::optional<WindowPauseDirective> WindowPlaybackPolicy::Complete(
    WindowPauseDirective const& reply, bool success) {
    if(!pending_ || reply.token!=pending_->token || reply.mediaGeneration!=generation_ ||
        reply.paused!=pending_->paused) return {};
    pending_.reset();
    if(!success) {
        // Do not loop forever when mpv rejects a property. A fresh user/window
        // event is needed to retry; a rejected pause never grants ownership.
        if(reply.paused) ownsPause_=false;
        return {};
    }
    ownsPause_=reply.paused;
    // Successful serialized setter establishes ordering for an inverse setter;
    // it does not overwrite the observed state that the host projects to UI.
    if(reply.paused && !reasons_) return Issue(false);
    // If the window became inactive while resume was pending, a new pause is
    // required even if the property observation has not caught up yet.
    if(!reply.paused && reasons_ && !cancelled_) return Issue(true);
    return {};
}
std::optional<bool> FullscreenPauseAction(bool entering, bool currentlyPlaying,
                                         PreferenceStore const& preferences) {
    if(entering && preferences.Boolean("playWhenEnteringFullScreen")) return false;
    if(!entering && currentlyPlaying && preferences.Boolean("pauseWhenLeavingFullScreen")) return true;
    return {};
}
}

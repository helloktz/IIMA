#pragma once
// IINA MainWindowController windowDidResignKey/windowDidBecomeKey,
// windowWillMiniaturize/windowDidDeminiaturize and fullscreen transitions.
// Copyright © 2016 lhc and IINA contributors. GPL-3.0.
#include "PreferenceSchema.h"
#include <cstdint>
#include <optional>

namespace iima::settings {
enum class WindowPauseReason : unsigned { Inactive=1, Minimized=2 };
struct WindowPauseDirective {
    std::uint64_t token;
    std::uint64_t mediaGeneration;
    bool paused;
};
// UI-thread-owned policy. The host routes the returned directive through its
// serialized PlayerSession and calls Complete only for that command's reply.
// This model never interprets a requested value as an observed mpv property.
class WindowPlaybackPolicy {
public:
    void MediaChanged(std::uint64_t generation, bool paused);
    void ObservePaused(bool paused) noexcept { observedPaused_=paused; }
    std::optional<WindowPauseDirective> SetWindowState(bool inactive, bool minimized,
        PreferenceStore const& preferences);
    std::optional<WindowPauseDirective> SetReason(WindowPauseReason reason,
        bool active, PreferenceStore const& preferences);
    std::optional<WindowPauseDirective> Complete(WindowPauseDirective const&, bool success);
    // Call before a user/SMTC/shortcut pause intent, Stop or shutdown. An old
    // pause reply must not authorize an automatic resume after explicit intent.
    void CancelAutomaticResume() noexcept;
    bool HasAutomaticPause() const noexcept { return ownsPause_; }
    bool HasPendingCommand() const noexcept { return pending_.has_value(); }
private:
    std::optional<WindowPauseDirective> Reconcile();
    std::optional<WindowPauseDirective> Issue(bool paused);
    unsigned reasons_{};
    bool observedPaused_{};
    bool ownsPause_{};
    bool cancelled_{};
    std::uint64_t generation_{};
    std::uint64_t serial_{};
    std::optional<WindowPauseDirective> pending_;
};
// Fullscreen transitions intentionally are actions rather than auto-resume
// ownership, as in the corresponding IINA window delegate callbacks.
std::optional<bool> FullscreenPauseAction(bool entering, bool currentlyPlaying,
                                         PreferenceStore const& preferences);
}

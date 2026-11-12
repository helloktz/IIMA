// IINA PlayerCore seek, frameStep and abLoop action integration.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "PlayerSession.h"
#include "PlaybackActions.h"
#include <cmath>
namespace iima {
void PlayerSession::SeekRelative(double seconds, bool exact) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !std::isfinite(seconds)) return;
    AutomaticSeekPolicy policy;
    auto plan = policy.Prepare(seconds, exact ? RelativeSeekMode::Exact : RelativeSeekMode::Relative);
    if (plan) client_->Command(std::move(plan->command));
}
void PlayerSession::StepFrame(bool backwards) {
    CancelWindowPausePolicy();
    if (snapshot_.session == SessionState::Ready && snapshot_.media == MediaState::Loaded && presentationReady_)
        client_->Command(PlaybackActions::FrameStep(backwards));
}
void PlayerSession::CycleABLoop() {
    if (snapshot_.session == SessionState::Ready && snapshot_.media == MediaState::Loaded)
        client_->Command(ABLoopState::Cycle());
}
bool PlayerSession::EditABLoopPoint(bool pointB, double seconds,
    std::optional<double> expectedA, std::optional<double> expectedB) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !snapshot_.seekable || !snapshot_.duration || !std::isfinite(*snapshot_.duration) ||
        !std::isfinite(seconds) || seconds < 0 || seconds > *snapshot_.duration ||
        *snapshot_.duration < ABLoopState::MinimumEditedPoint ||
        snapshot_.abLoopA != expectedA || snapshot_.abLoopB != expectedB ||
        !(pointB ? snapshot_.abLoopB : snapshot_.abLoopA)) return false;
    ABLoopState loop;
    loop.Reset(0);
    if (!loop.Observe(0, snapshot_.abLoopA, snapshot_.abLoopB, snapshot_.abLoopCount)) return false;
    auto command = loop.SetPoint(pointB ? ABLoopPoint::B : ABLoopPoint::A, seconds);
    return command && client_->Command(std::move(*command)) != 0;
}
void PlayerSession::ClearABLoop() {
    if (snapshot_.session != SessionState::Ready) return;
    for (auto const& command : ABLoopState::Clear()) client_->Command(command);
}
}

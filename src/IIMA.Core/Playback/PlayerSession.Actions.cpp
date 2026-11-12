// IINA PlayerCore seek, frameStep and abLoop action integration.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "PlayerSession.h"
#include "PlaybackActions.h"
#include <cmath>
namespace iima {
void PlayerSession::SeekRelative(double seconds, bool exact) {
    SeekRelativeWithMode(seconds,exact ? RelativeSeekMode::Exact : RelativeSeekMode::Relative);
}
void PlayerSession::SeekRelativeWithMode(double seconds, RelativeSeekMode mode) {
    CancelScrub();
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !snapshot_.seekable || !std::isfinite(seconds) || seconds == 0) return;
    auto plan = automaticSeekPolicy_.Prepare(seconds,mode);
    if (!plan) return;
    auto request = client_->Command(plan->command,plan->recordLatency);
    automaticSeekPolicy_.CommandSubmitted(*plan,request != 0);
    if (request && plan->recordLatency) seekMeasurementRequest_ = request;
}
void PlayerSession::AdjustVolumeByWheel(double delta) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !std::isfinite(delta) || delta == 0 || std::abs(delta) > 64) return;
    SubmitVolumeCommand({"add", "volume", std::to_string(delta)});
}
void PlayerSession::StepFrame(bool backwards) {
    CancelScrub();
    CancelWindowPausePolicy();
    if (snapshot_.session == SessionState::Ready && snapshot_.media == MediaState::Loaded && presentationReady_)
        client_->Command(PlaybackActions::FrameStep(backwards));
}
void PlayerSession::CycleABLoop() {
    CancelScrub();
    if (snapshot_.session == SessionState::Ready && snapshot_.media == MediaState::Loaded)
        client_->Command(ABLoopState::Cycle());
}
bool PlayerSession::EditABLoopPoint(bool pointB, double seconds,
    std::optional<double> expectedA, std::optional<double> expectedB) {
    CancelScrub();
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
    CancelScrub();
    if (snapshot_.session != SessionState::Ready) return;
    for (auto const& command : ABLoopState::Clear()) client_->Command(command);
}
}

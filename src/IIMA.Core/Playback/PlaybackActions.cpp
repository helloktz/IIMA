// Static port of IINA PlayerCore.swift, PlaybackInfo.swift and AppData.swift.
// Copyright (c) 2016-2026 lhc and IINA contributors.
// Source snapshot: references/iina-source.txt. License: references/iina/LICENSE (GPL v3).
#include "PlaybackActions.h"
#include "../Mpv/Catalog/MPVCommand.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <utility>

namespace iima {
namespace {
std::string Number(double number) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    return result.ec == std::errc{} ? std::string(buffer.data(), result.ptr) : std::string{};
}

bool ValidPoint(const std::optional<double>& point) noexcept {
    return !point || std::isfinite(*point);
}
} // namespace

void ABLoopState::Reset(std::uint64_t generation) {
    generation_ = generation;
    a_.reset();
    b_.reset();
    count_ = "inf";
}

bool ABLoopState::Observe(std::uint64_t generation, std::optional<double> a,
                          std::optional<double> b, std::string count) {
    if (generation != generation_ || !ValidPoint(a) || !ValidPoint(b)) return false;
    a_ = a;
    b_ = b;
    count_ = std::move(count);
    return true;
}

ABLoopStatus ABLoopState::Status() const noexcept {
    if (!a_) return b_ ? ABLoopStatus::ASet : ABLoopStatus::Cleared;
    return b_ ? ABLoopStatus::BSet : ABLoopStatus::ASet;
}

bool ABLoopState::HasUnexpectedBOnlyState() const noexcept { return !a_ && b_.has_value(); }
std::optional<double> ABLoopState::PointA() const noexcept { return a_; }
std::optional<double> ABLoopState::PointB() const noexcept { return b_; }

bool ABLoopState::IsActive() const noexcept {
    // Equal endpoints are disabled by mpv/player/misc.c get_ab_loop_times.
    return a_ && b_ && *a_ != *b_ && count_ != "0";
}

std::optional<ABLoopInterval> ABLoopState::EffectiveInterval(bool backwards) const noexcept {
    if (!IsActive()) return std::nullopt;
    const auto lower = std::min(*a_, *b_);
    const auto upper = std::max(*a_, *b_);
    // Preserve endpoint identity; order only the effective playback interval.
    // mpv orders by play direction, rather than rewriting observed A and B.
    return backwards ? ABLoopInterval{upper, lower} : ABLoopInterval{lower, upper};
}

std::optional<PlaybackCommand> ABLoopState::SetPoint(ABLoopPoint point, double seconds) const {
    if (!std::isfinite(seconds)) return std::nullopt;
    if (point == ABLoopPoint::A && Status() == ABLoopStatus::Cleared) return std::nullopt;
    if (point == ABLoopPoint::B && Status() != ABLoopStatus::BSet) return std::nullopt;
    return PlaybackCommand{"set", point == ABLoopPoint::A ? "ab-loop-a" : "ab-loop-b",
                           Number(std::max(MinimumEditedPoint, seconds))};
}

PlaybackCommand ABLoopState::Cycle() { return {std::string(mpv_catalog::MPVCommand::abLoop)}; }

std::array<PlaybackCommand, 2> ABLoopState::Clear() {
    // Submission can partially fail. The caller must observe both properties,
    // not claim a cleared state merely because these commands were enqueued.
    return {PlaybackCommand{"set", "ab-loop-a", "no"}, PlaybackCommand{"set", "ab-loop-b", "no"}};
}

void AutomaticSeekPolicy::BeginMedia(std::uint64_t generation) noexcept {
    generation_ = generation;
    tried_ = false;
    awaitingMeasurement_ = false;
}

std::optional<SeekPlan> AutomaticSeekPolicy::Prepare(double seconds, RelativeSeekMode mode) const {
    if (!std::isfinite(seconds)) return std::nullopt;
    const bool automatic = mode == RelativeSeekMode::Automatic;
    const bool exact = mode == RelativeSeekMode::Exact || (automatic && useExact_);
    return SeekPlan{{"seek", Number(seconds), exact ? "relative+exact" : "relative"},
                    automatic && !tried_, generation_};
}

void AutomaticSeekPolicy::CommandSubmitted(const SeekPlan& plan, bool accepted) noexcept {
    // Rejected commands must not consume the first-seek trial opportunity.
    if (accepted && plan.mediaGeneration == generation_ && plan.recordLatency && !tried_) {
        tried_ = true;
        awaitingMeasurement_ = true;
    }
}

void AutomaticSeekPolicy::ObserveSeekElapsed(std::uint64_t generation, double elapsed) noexcept {
    if (generation != generation_ || !awaitingMeasurement_ || !std::isfinite(elapsed) || elapsed < 0) return;
    useExact_ = elapsed < 0.05;
    awaitingMeasurement_ = false;
}

bool AutomaticSeekPolicy::UsesExactSeek() const noexcept { return useExact_; }

std::optional<PlaybackCommand> PlaybackActions::SeekPercent(double percent,
    std::optional<double> duration, bool exactPreference, bool forceExact) {
    if (!std::isfinite(percent)) return std::nullopt;
    if (duration && std::isfinite(*duration) && *duration > 0)
        percent = std::clamp(percent, 0.0, std::nextafter(100.0, 0.0));
    return PlaybackCommand{"seek", Number(percent),
        forceExact || exactPreference ? "absolute-percent+exact" : "absolute-percent"};
}

std::optional<PlaybackCommand> PlaybackActions::SeekAbsolute(double seconds) {
    if (!std::isfinite(seconds)) return std::nullopt;
    // mpv interprets negative absolute timestamps relative to file end.
    return PlaybackCommand{"seek", Number(seconds), "absolute+exact"};
}

PlaybackCommand PlaybackActions::FrameStep(bool backwards) {
    // IINA activates its CVDisplayLink here. The Windows gpu-next presenter
    // has no CVDisplayLink and must arrange rendering separately if needed.
    return {std::string(backwards ? mpv_catalog::MPVCommand::frameBackStep : mpv_catalog::MPVCommand::frameStep)};
}

std::optional<VolumePlan> PlaybackActions::Volume(double requested, double maximum,
                                                double observed, bool constrain) {
    if (!std::isfinite(requested) || !std::isfinite(maximum) || maximum < 0) return std::nullopt;
    const auto bounded = std::clamp(requested, 0.0, maximum);
    const auto applied = constrain ? bounded : requested;
    return VolumePlan{{"set", "volume", Number(applied)}, applied, bounded,
                      constrain && requested != bounded && observed == applied};
}

std::optional<PlaybackCommand> PlaybackActions::Speed(double requested) {
    if (!std::isfinite(requested)) return std::nullopt;
    return PlaybackCommand{"set", "speed", Number(std::max(MinimumPlaybackSpeed, requested))};
}

ScreenshotPlan PlaybackActions::Screenshot(const ScreenshotRequest& request) {
    ScreenshotPlan result;
    result.keepFile = request.saveToFile;
    result.copyToClipboard = request.copyToClipboard;
    if (!request.saveToFile && !request.copyToClipboard) return result;
    if (!request.selectedVideoTrack || *request.selectedVideoTrack <= 0) {
        result.route = ScreenshotRoute::NoVideo;
        return result;
    }
    std::vector<std::string> flags;
    if (request.keyBindingTokens) {
        const auto& tokens = *request.keyBindingTokens;
        if (tokens.empty() || tokens.front() != "screenshot") {
            result.route = ScreenshotRoute::InvalidBinding;
            return result;
        }
        if (tokens.size() > 1) {
            std::string_view remaining = tokens[1];
            while (!remaining.empty()) {
                const auto plus = remaining.find('+');
                const auto flag = remaining.substr(0, plus);
                if (!flag.empty()) flags.emplace_back(flag);
                if (plus == std::string_view::npos) break;
                remaining.remove_prefix(plus + 1);
            }
        }
        const bool passthrough = std::any_of(flags.begin(), flags.end(), [](const auto& flag) {
            return flag != "window" && flag != "subtitles" && flag != "video";
        });
        if (passthrough) {
            result.route = ScreenshotRoute::MpvPassthrough;
            result.rawCommand = request.rawKeyBinding;
            result.command = tokens;
            return result;
        }
    }
    if (flags.empty()) flags.emplace_back(request.includeSubtitles ? "subtitles" : "video");
    // The pinned mpv syntax accepts combined flags in one argument. IINA passes
    // its split list as separate arguments, which can accidentally invoke the
    // legacy second-argument mode; normalize to the documented '+' syntax.
    std::string combined;
    for (const auto& flag : flags) {
        if (!combined.empty()) combined += '+';
        combined += flag;
    }
    result.command = {"screenshot", std::move(combined)};
    result.route = ScreenshotRoute::Managed;
    return result;
}

} // namespace iima

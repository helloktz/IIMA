// Static port of IINA PlayerCore.swift, PlaybackInfo.swift and AppData.swift.
// Copyright (c) 2016-2026 lhc and IINA contributors.
// Source snapshot: references/iina-source.txt. License: references/iina/LICENSE (GPL v3).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iima {

using PlaybackCommand = std::vector<std::string>;

enum class ABLoopStatus { Cleared, ASet, BSet };
enum class ABLoopPoint { A, B };
struct ABLoopInterval { double start{}, end{}; };

// Confirmed mpv properties only. Creating a command never advances this state.
// Missing endpoints correspond to mpv "no", not zero: 0 seconds is a valid
// endpoint in the pinned mpv API even though IINA's getDouble loses that detail.
class ABLoopState final {
public:
    static constexpr double MinimumEditedPoint = 0.000001;
    void Reset(std::uint64_t mediaGeneration);
    bool Observe(std::uint64_t mediaGeneration, std::optional<double> a,
                 std::optional<double> b, std::string count = "inf");
    [[nodiscard]] ABLoopStatus Status() const noexcept;
    [[nodiscard]] bool HasUnexpectedBOnlyState() const noexcept;
    [[nodiscard]] std::optional<double> PointA() const noexcept;
    [[nodiscard]] std::optional<double> PointB() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] std::optional<ABLoopInterval> EffectiveInterval(bool backwards = false) const noexcept;
    [[nodiscard]] std::optional<PlaybackCommand> SetPoint(ABLoopPoint point, double seconds) const;
    static PlaybackCommand Cycle();
    static std::array<PlaybackCommand, 2> Clear();

private:
    std::uint64_t generation_{};
    std::optional<double> a_, b_;
    std::string count_{"inf"};
};

enum class RelativeSeekMode { Relative, Exact, Automatic };
struct SeekPlan {
    PlaybackCommand command;
    bool recordLatency{};
    std::uint64_t mediaGeneration{};
};

class AutomaticSeekPolicy final {
public:
    // IINA resets the trial flag on file load but keeps its prior exact/fast
    // decision until the first new measurement. Retain that behavior here.
    void BeginMedia(std::uint64_t generation) noexcept;
    [[nodiscard]] std::optional<SeekPlan> Prepare(double seconds, RelativeSeekMode mode) const;
    void CommandSubmitted(const SeekPlan& plan, bool accepted) noexcept;
    // Invoke only for the accepted trial seek, not unrelated seek completions.
    void ObserveSeekElapsed(std::uint64_t generation, double elapsedSeconds) noexcept;
    [[nodiscard]] bool UsesExactSeek() const noexcept;
private:
    std::uint64_t generation_{};
    bool tried_{};
    bool awaitingMeasurement_{};
    bool useExact_{true};
};

struct VolumePlan {
    PlaybackCommand command;
    double appliedVolume{};
    double storedSoftVolume{};
    bool showBoundaryOSD{};
};

struct ScreenshotRequest {
    bool saveToFile{true};
    bool copyToClipboard{};
    bool includeSubtitles{true};
    std::optional<std::int64_t> selectedVideoTrack;
    // If present, tokens and raw text originate from a parsed KeyMapping.
    std::optional<PlaybackCommand> keyBindingTokens;
    std::string rawKeyBinding;
};

enum class ScreenshotRoute { Disabled, NoVideo, InvalidBinding, Managed, MpvPassthrough };
struct ScreenshotPlan {
    ScreenshotRoute route{ScreenshotRoute::Disabled};
    PlaybackCommand command;
    std::string rawCommand;
    bool keepFile{};
    bool copyToClipboard{};
};

class PlaybackActions final {
public:
    static constexpr double MinimumPlaybackSpeed = 0.01;
    static constexpr double SliderMinimumSpeed = 0.25;
    static constexpr double SliderMaximumSpeed = 16.0;
    static constexpr std::array<double, 11> SpeedSteps{
        0.03125, 0.0625, 0.125, 0.25, 0.5, 1, 2, 4, 8, 16, 32
    };
    static constexpr std::array<double, 5> SeekGestureAmounts{0, 0.05, 0.1, 0.25, 0.5};
    static constexpr std::array<double, 5> SeekMouseAmounts{0, 0.5, 1, 2, 4};
    static constexpr std::array<double, 5> VolumeGestureAmounts{0, 0.25, 0.5, 0.75, 1};
    static constexpr std::array<double, 5> SpeedGestureAmounts{0, 0.001, 0.002, 0.005, 0.01};
    static std::optional<PlaybackCommand> SeekPercent(double percent,
        std::optional<double> durationSeconds, bool exactPreference, bool forceExact = false);
    static std::optional<PlaybackCommand> SeekAbsolute(double seconds);
    static PlaybackCommand FrameStep(bool backwards);
    static std::optional<VolumePlan> Volume(double requested, double maximum,
                                            double observedVolume, bool constrain = true);
    static std::optional<PlaybackCommand> Speed(double requested);
    static ScreenshotPlan Screenshot(const ScreenshotRequest& request);
};

} // namespace iima

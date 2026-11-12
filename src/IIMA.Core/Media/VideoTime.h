// Ported from IINA VideoTime.swift and Extensions.swift.
// Copyright (c) 2016 lhc and IINA contributors.
// Source: references/iina, commit recorded in references/iina-source.txt.
// License: GNU GPL v3, see references/iina/LICENSE.
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace iima {

// A media duration, not a wall-clock time. Fractional seconds are preserved;
// IINA equality and ordering intentionally discard sub-millisecond precision.
class VideoTime final {
public:
    explicit VideoTime(double seconds = 0.0) noexcept;
    VideoTime(std::int64_t hours, std::int64_t minutes, double seconds) noexcept;

    // IINA's sentinel means an open-ended interval, not IEEE infinity.
    static VideoTime Infinite() noexcept;
    static VideoTime Zero() noexcept;

    // Matches IINA's permissive colon parser: empty components are omitted,
    // only the rightmost three components participate, invalid components
    // become zero, and parsing fails only if all three components are invalid.
    static std::optional<VideoTime> Parse(std::string_view text) noexcept;

    [[nodiscard]] double Seconds() const noexcept;
    void SetSeconds(double seconds) noexcept;

    // Precisions 1..3 display fractional seconds. All other values select
    // integral seconds rounded half down, as in IINA's Now Playing display.
    // A nonfinite or out-of-range value renders as "--:--" rather than invoking
    // Swift's trapping integer conversion or undefined C++ numeric conversion.
    [[nodiscard]] std::string StringRepresentation(unsigned precision = 0) const;
    [[nodiscard]] bool Between(VideoTime minimum, VideoTime maximum) const noexcept;

    friend bool operator==(VideoTime left, VideoTime right) noexcept;
    friend std::partial_ordering operator<=>(VideoTime left, VideoTime right) noexcept;
    friend VideoTime operator*(VideoTime left, double multiplier) noexcept;
    friend VideoTime operator-(VideoTime left, VideoTime right) noexcept;

    // Missing operands propagate absence. Zero division retains IEEE floating
    // point semantics, matching Swift, and is not silently replaced with zero.
    static std::optional<double> Divide(std::optional<VideoTime> numerator,
                                       std::optional<VideoTime> denominator) noexcept;

private:
    double seconds_;
};

} // namespace iima

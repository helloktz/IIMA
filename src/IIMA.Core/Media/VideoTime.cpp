// Ported from IINA VideoTime.swift and Extensions.swift.
// Copyright (c) 2016 lhc and IINA contributors.
// Source: references/iina, commit recorded in references/iina-source.txt.
// License: GNU GPL v3, see references/iina/LICENSE.
#include "VideoTime.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace iima {
namespace {

// from_chars is locale independent. A leading '+' is accepted by Swift's
// numeric initializers but not C++ from_chars, so handle it explicitly.
template<class T>
std::optional<T> ParseNumber(std::string_view component) noexcept {
    if (component.empty()) return std::nullopt;
    if (component.front() == '+') {
        component.remove_prefix(1);
        if (component.empty() || component.front() == '+' || component.front() == '-')
            return std::nullopt;
    }
    T value{};
    const auto result = std::from_chars(component.data(), component.data() + component.size(), value);
    if (result.ec != std::errc{} || result.ptr != component.data() + component.size())
        return std::nullopt;
    return value;
}

double RoundedHalfDown(double value) noexcept {
    const auto lower = std::floor(value);
    return value <= lower + 0.5 ? lower : std::ceil(value);
}

std::string TwoDigits(std::int64_t value) {
    // Preserve upstream's decimal formatting, including its treatment of
    // negative component values. Callers normally display positive durations.
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

std::string ClockPrefix(std::int64_t seconds) {
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    return (hours > 0 ? std::to_string(hours) + ":" : "") + TwoDigits(minutes) + ":";
}

std::string FractionalSeconds(double seconds, unsigned precision) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(static_cast<int>(precision))
        << std::internal << std::setw(static_cast<int>(precision + 3)) << std::setfill('0') << seconds;
    return out.str();
}

// long double avoids the integer overflow that a literal translation of
// Int(seconds * 1000) would cause for invalid media metadata. Multiplication
// remains in double to preserve upstream rounding at millisecond boundaries.
long double Milliseconds(double seconds) noexcept {
    return std::trunc(static_cast<long double>(seconds * 1000.0));
}

} // namespace

VideoTime::VideoTime(double seconds) noexcept : seconds_(seconds) {}

VideoTime::VideoTime(std::int64_t hours, std::int64_t minutes, double seconds) noexcept
    : seconds_(static_cast<double>(hours) * 3600.0 +
               static_cast<double>(minutes) * 60.0 + seconds) {}

VideoTime VideoTime::Infinite() noexcept { return VideoTime(999, 0, 0); }
VideoTime VideoTime::Zero() noexcept { return VideoTime(0.0); }

double VideoTime::Seconds() const noexcept { return seconds_; }
void VideoTime::SetSeconds(double seconds) noexcept { seconds_ = seconds; }

std::optional<VideoTime> VideoTime::Parse(std::string_view text) noexcept {
    std::array<std::string_view, 3> reversed{};
    std::size_t count = 0;
    while (!text.empty() && count < reversed.size()) {
        const auto colon = text.rfind(':');
        const auto component = colon == std::string_view::npos ? text : text.substr(colon + 1);
        if (!component.empty()) reversed[count++] = component;
        if (colon == std::string_view::npos) break;
        text = text.substr(0, colon);
    }
    const auto seconds = count > 0 ? ParseNumber<double>(reversed[0]) : std::nullopt;
    const auto minutes = count > 1 ? ParseNumber<std::int64_t>(reversed[1]) : std::nullopt;
    const auto hours = count > 2 ? ParseNumber<std::int64_t>(reversed[2]) : std::nullopt;
    if (!seconds && !minutes && !hours) return std::nullopt;
    return VideoTime(static_cast<double>(hours.value_or(0)) * 3600.0 +
                     static_cast<double>(minutes.value_or(0)) * 60.0 + seconds.value_or(0.0));
}

std::string VideoTime::StringRepresentation(unsigned precision) const {
    if (*this == Infinite()) return "End";
    if (!std::isfinite(seconds_)) return "--:--";
    const bool precise = precision >= 1 && precision <= 3;
    const double rounded = precise ? std::trunc(seconds_) : RoundedHalfDown(seconds_);
    // Bounds leave space for the fractional carry, and avoid conversion of
    // double(INT64_MAX), which rounds upward to an unrepresentable integer.
    constexpr double limit = 9.0e18;
    if (rounded < -limit || rounded > limit) return "--:--";
    const auto whole = static_cast<std::int64_t>(rounded);
    if (!precise) return ClockPrefix(whole) + TwoDigits(whole % 60);
    const auto secondsField = FractionalSeconds(std::fmod(seconds_, 60.0), precision);
    if (secondsField.starts_with("60")) {
        return ClockPrefix(whole + 60) + FractionalSeconds(0.0, precision);
    }
    return ClockPrefix(whole) + secondsField;
}

bool VideoTime::Between(VideoTime minimum, VideoTime maximum) const noexcept {
    return *this >= minimum && *this < maximum;
}

bool operator==(VideoTime left, VideoTime right) noexcept {
    return Milliseconds(left.seconds_) == Milliseconds(right.seconds_);
}

std::partial_ordering operator<=>(VideoTime left, VideoTime right) noexcept {
    return Milliseconds(left.seconds_) <=> Milliseconds(right.seconds_);
}

VideoTime operator*(VideoTime left, double multiplier) noexcept {
    return VideoTime(left.seconds_ * multiplier);
}

VideoTime operator-(VideoTime left, VideoTime right) noexcept {
    return VideoTime(left.seconds_ - right.seconds_);
}

std::optional<double> VideoTime::Divide(std::optional<VideoTime> numerator,
                                      std::optional<VideoTime> denominator) noexcept {
    if (!numerator || !denominator) return std::nullopt;
    return numerator->seconds_ / denominator->seconds_;
}

} // namespace iima

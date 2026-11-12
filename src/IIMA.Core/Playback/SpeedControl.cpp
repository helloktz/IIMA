// Adapted from IINA SidebarVideoPane.SpeedView / Extensions.roundedTo2Decimals.
// Copyright (c) 2026 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "SpeedControl.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <system_error>

namespace iima {
namespace {
bool ValidSpeed(double value) noexcept {
    return std::isfinite(value) && value >= MinimumPlaybackSpeed && value <= MaximumPlaybackSpeed;
}

// Work in decimal digits instead of adding a floating epsilon around .5.
// An epsilon would incorrectly classify real values on either side of a tie.
// A shortest decimal spelling makes an entered 1.2345675 a half-down tie,
// instead of amplifying its binary floating-point representation error.
std::optional<std::uint64_t> DecimalMillionths(double value) noexcept {
    std::array<char, 64> text{};
    auto converted = std::to_chars(text.data(), text.data() + text.size(), value,
                                   std::chars_format::general);
    if (converted.ec != std::errc{}) return {};
    std::array<unsigned char, 32> digits{};
    std::size_t size{};
    int point{};
    bool fraction{};
    auto cursor = text.data();
    while (cursor != converted.ptr && *cursor != 'e' && *cursor != 'E') {
        if (*cursor == '.' && !fraction) {
            fraction = true;
        } else if (*cursor >= '0' && *cursor <= '9' && size < digits.size()) {
            digits[size++] = static_cast<unsigned char>(*cursor - '0');
            if (!fraction) ++point;
        } else return {};
        ++cursor;
    }
    if (!size) return {};
    if (cursor != converted.ptr) {
        ++cursor; // exponent marker
        bool negative{};
        if (cursor != converted.ptr && (*cursor == '+' || *cursor == '-')) {
            negative = *cursor == '-';
            ++cursor;
        }
        if (cursor == converted.ptr) return {};
        int exponent{};
        for (; cursor != converted.ptr; ++cursor) {
            if (*cursor < '0' || *cursor > '9' || exponent > 100) return {};
            exponent = exponent * 10 + *cursor - '0';
        }
        point += negative ? -exponent : exponent;
    }
    // Legal speed range limits the number of retained digits to at most nine.
    const auto retained = point + 6;
    if (retained <= 0 || retained > 9) return {};
    std::uint64_t millionths{};
    for (int i = 0; i < retained; ++i) {
        millionths = millionths * 10 +
            (static_cast<std::size_t>(i) < size ? digits[static_cast<std::size_t>(i)] : 0);
    }
    if (static_cast<std::size_t>(retained) < size) {
        const auto firstDiscarded = digits[static_cast<std::size_t>(retained)];
        bool remainder{};
        for (std::size_t i = static_cast<std::size_t>(retained) + 1; i < size; ++i)
            remainder = remainder || digits[i] != 0;
        // Positive half-down: exact ...5000 tails round toward zero, whereas
        // ...5001 and all larger discarded fractions round upward.
        if (firstDiscarded > 5 || (firstDiscarded == 5 && remainder)) ++millionths;
    }
    return millionths;
}
}
std::optional<double> NormalizePlaybackSpeed(double value) noexcept {
    if (!ValidSpeed(value)) return {};
    auto millionths = DecimalMillionths(value);
    if (!millionths) return {};
    const auto normalized = static_cast<double>(*millionths) / 1000000.0;
    if (!ValidSpeed(normalized)) return {};
    return normalized;
}
std::optional<double> SpeedFromSlider(double position) noexcept {
    if (!std::isfinite(position) || position < 0 || position > SpeedSliderSteps) return {};
    const auto speed = MinimumSliderSpeed *
        std::pow(MaximumSliderSpeed / MinimumSliderSpeed, position / SpeedSliderSteps);
    // Swift .rounded() defaults to nearest, ties away from zero.
    return NormalizePlaybackSpeed(std::round(speed * 100.0) / 100.0);
}
std::optional<double> SpeedToSliderPosition(double speed) noexcept {
    if (!ValidSpeed(speed)) return {};
    const auto position = std::log(speed / MinimumSliderSpeed) /
        std::log(MaximumSliderSpeed / MinimumSliderSpeed) * SpeedSliderSteps;
    return std::clamp(position, 0.0, SpeedSliderSteps);
}
}

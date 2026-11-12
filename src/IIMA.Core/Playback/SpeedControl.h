// Adapted from IINA SidebarVideoPane.swift SpeedView and AppData.swift.
// Copyright (c) 2026 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <optional>

namespace iima {
inline constexpr double MinimumPlaybackSpeed = 0.01;
inline constexpr double MaximumPlaybackSpeed = 100.0; // IIMA PlayerSession limit.
inline constexpr double MinimumSliderSpeed = 0.25;
inline constexpr double MaximumSliderSpeed = 16.0;
inline constexpr double SpeedSliderSteps = 24.0;

// Positive, finite input only; rejects out-of-session-range values rather than
// silently restoring 1x. Quantizes the shortest decimal spelling of the input
// to six fractional places, round-half-down (ties toward zero), as IINA's
// NumberFormatter intends. Locale parsing belongs to the native input control.
std::optional<double> NormalizePlaybackSpeed(double value) noexcept;

// IINA's slider action: 0.25 * 64^(position/24), rounded to two fractional
// places using Swift's nearest/away-from-zero tie rule. The caller decides
// whether dragging snaps to whole ticks; continuous positions are also valid.
std::optional<double> SpeedFromSlider(double position) noexcept;

// A text-entered speed can be outside the slider's 0.25..16 range. The thumb
// clamps to an endpoint without changing the actual speed or numeric editor.
// Invalid/non-finite speeds do not become a misleading endpoint position.
std::optional<double> SpeedToSliderPosition(double speed) noexcept;
}

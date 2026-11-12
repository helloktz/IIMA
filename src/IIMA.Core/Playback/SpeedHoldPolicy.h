#pragma once
#include <optional>
namespace iima {
struct SpeedHoldPlan { double target{}; bool restoreOnLongRelease{}; };
// IINA OSC speed ladder; custom speeds choose the strict directional neighbour.
std::optional<SpeedHoldPlan> PlanSpeedHold(double observed, int direction) noexcept;
}

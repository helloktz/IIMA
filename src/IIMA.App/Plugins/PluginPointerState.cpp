// IINA NSEvent clickCount adaptation to a local Windows pointer sequence.
// Copyright (c) 2023 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginPointerState.h"
#include <cmath>
#include <stdexcept>
namespace iima::plugins {
unsigned PluginPointerState::Press(unsigned button, std::uint32_t pointer, std::uint64_t now,
    double x, double y, unsigned interval, double width, double height) {
    if (button >= buttons_.size() || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0)
        throw std::invalid_argument("Invalid pointer sequence.");
    auto& state = buttons_[button];
    const bool next = state.previous && !state.active && state.pointer == pointer &&
        now >= state.pressedAt && now - state.pressedAt <= interval &&
        std::abs(x - state.x) <= width / 2 && std::abs(y - state.y) <= height / 2;
    // Win32 double-click convention pairs clicks; a third starts a new pair.
    state.count = next && state.count == 1 ? 2 : 1;
    state.active = state.previous = true;
    state.pointer = pointer; state.pressedAt = state.draggedAt = now;
    state.x = x; state.y = y;
    return state.count;
}
std::optional<unsigned> PluginPointerState::Release(unsigned button, std::uint32_t pointer) {
    if (button >= buttons_.size()) return {};
    auto& state = buttons_[button];
    if (!state.active || state.pointer != pointer) return {};
    state.active = false;
    return state.count;
}
std::optional<unsigned> PluginPointerState::Drag(unsigned button, std::uint32_t pointer, std::uint64_t now) {
    if (button >= buttons_.size()) return {};
    auto& state = buttons_[button];
    if (!state.active || state.pointer != pointer || now < state.draggedAt || now - state.draggedAt < 33) return {};
    state.draggedAt = now;
    state.previous = false; // A drag cannot begin a later double-click sequence.
    return state.count;
}
void PluginPointerState::Reset() noexcept { buttons_ = {}; }
}

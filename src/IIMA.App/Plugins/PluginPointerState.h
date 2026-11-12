// IINA PlayerWindowController mouseEventArgs, native input adaptation.
// Copyright (c) 2023 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace iima::plugins {
// UI-thread state only; never passed to the JS worker. No pointer capture or
// global mouse hook. Coordinates/timestamps must share the current root space.
class PluginPointerState final {
public:
    unsigned Press(unsigned button, std::uint32_t pointer, std::uint64_t milliseconds,
                   double x, double y, unsigned doubleClickMs, double width, double height);
    std::optional<unsigned> Release(unsigned button, std::uint32_t pointer);
    std::optional<unsigned> Drag(unsigned button, std::uint32_t pointer,
                                 std::uint64_t milliseconds);
    void Reset() noexcept;
private:
    struct Button {
        bool active{}, previous{};
        std::uint32_t pointer{};
        std::uint64_t pressedAt{}, draggedAt{};
        double x{}, y{};
        unsigned count{};
    };
    std::array<Button, 3> buttons_{};
};
}

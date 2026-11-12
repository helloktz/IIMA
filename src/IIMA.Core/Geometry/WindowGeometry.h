// Adapted from IINA GeometryDef.swift / MainWindowController.windowFrameFromGeometry.
// Copyright © 2017 lhc. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string_view>
namespace iima {
struct GeometryLength final {
    double value{};
    bool percentage{};
    double Resolve(double available) const noexcept;
};
struct GeometryRect final { double x{}, y{}, width{}, height{}; };
struct GeometrySize final { double width{}, height{}; };
struct WindowGeometry final {
    std::optional<GeometryLength> width, height, x, y;
    bool right{}, bottom{};
    static std::optional<WindowGeometry> Parse(std::string_view text);
    // Inputs are physical pixels in Windows top-left coordinates. Width takes
    // precedence over height, preserving the existing video's aspect ratio.
    std::optional<GeometryRect> Resolve(GeometryRect current, GeometryRect workArea,
        GeometrySize minimum) const noexcept;
};
}

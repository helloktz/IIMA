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
    std::optional<GeometryRect> ResolveVisible(GeometryRect current, GeometryRect workArea,
        GeometrySize minimum) const noexcept;
    std::optional<GeometryRect> Resolve(GeometryRect current, GeometryRect workArea,
        GeometrySize minimum) const noexcept;
};
}

namespace iima {
// MainWindowController.setWindowScale: scale the video, not the sidebar/frame.
// All geometry is physical pixels. video is aspect-corrected, rotation-adjusted.
// scale=null selects the largest aspect-preserving video fitting the work area.
std::optional<GeometryRect> ScaledVideoWindow(GeometrySize video, GeometryRect current,
    GeometryRect workArea, GeometrySize chrome, GeometrySize minimum,
    std::optional<double> scale, bool physicalResolution, double dpiScale) noexcept;
}

namespace iima {
// IINA NSRect.areaPreservingResized, adapted to preserve the actual video
// viewport's area, excluding fixed WinUI/Win32 chrome. All units are pixels.
std::optional<GeometryRect> AreaPreservingVideoWindow(GeometrySize video, GeometryRect current,
    GeometryRect workArea, GeometrySize chrome, GeometrySize minimum) noexcept;
// Initial width/height are outer-frame dimensions (width wins). Derive the
// other dimension from video aspect, not the outer frame's aspect. Positions
// retain WindowGeometry's percentage-center and right/bottom edge semantics.
std::optional<GeometryRect> InitialVideoWindow(WindowGeometry const& geometry, GeometrySize video,
    GeometryRect sized, GeometryRect workArea, GeometrySize chrome, GeometrySize minimum) noexcept;
}

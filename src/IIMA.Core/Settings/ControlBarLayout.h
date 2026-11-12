// IINA OSCFloatingView.initPosition/updatePosition/mouseDragged/mouseUp.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "../Geometry/WindowGeometry.h"
namespace iima::settings {
struct FloatingControlBarPosition { double horizontal{0.5},vertical{0.1}; };
// Windows physical left-to-right X and top-down Y, all in XAML DIPs, not pixels.
// horizontal denotes bar center / viewport width; vertical is bottom gap /
// viewport height. No RTL mirroring of saved X. Caller supplies its safe overlay
// viewport (excluding any occupied top strip) and applies the returned size caps.
// Oversized bars shrink to viewport, margins reduce for narrow/small viewports.
// Projection never writes the saved fractions, including on resize/DPI changes.
std::optional<GeometryRect> ProjectFloatingControlBar(GeometrySize viewport, GeometrySize bar,
    FloatingControlBarPosition position) noexcept;
// delta is a translation from current in the same DIP coordinate space. The
// returned position is suitable for persistence only on completed pointer-up.
std::optional<FloatingControlBarPosition> MoveFloatingControlBar(GeometrySize viewport,
    GeometrySize bar, GeometryRect current, GeometrySize delta, bool snapToCenter) noexcept;
}

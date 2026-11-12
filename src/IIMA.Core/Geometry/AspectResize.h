// IINA MainWindowController.setWindowAspectRatio, adapted to Win32 sizing edges.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "WindowGeometry.h"

namespace iima {
// Values intentionally match WMSZ_* without importing Windows into pure geometry.
enum class SizingEdge { Left=1, Right, Top, TopLeft, TopRight, Bottom, BottomLeft, BottomRight };
struct WindowAspectConstraint {
    GeometrySize video; // Aspect-corrected, rotation-adjusted display dimensions.
    GeometrySize chrome; // Fixed physical outer frame minus video viewport.
    GeometrySize minimum; // Preferred minimum OUTER dimensions in physical pixels.
    GeometrySize projectedOuter; // Outer size used to measure this chrome.
    unsigned dpi{}; // Native adapter refuses stale-DPI layout projections.
};
// Pure WM_SIZING geometry. Opposite dragged edges remain anchored where the
// work area permits; a one-axis drag adjusts the other axis about its center.
// Work-area visibility wins over an impossible minimum. No window operations.
std::optional<GeometryRect> ConstrainVideoResize(GeometryRect proposed, GeometryRect work,
    SizingEdge edge, WindowAspectConstraint const& constraint) noexcept;
}

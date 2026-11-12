#pragma once
// Adapted from IINA MainWindowController.windowFrameFromGeometry,
// determineScreenToUse and MainWindowLastPosition. Copyright (c) 2017 lhc.
// GPL-3.0; references/iina/LICENSE and references/iina-source.txt.
#include "../../IIMA.Core/Geometry/WindowGeometry.h"
#include <cstdint>
#include <optional>

namespace iima {
enum class WindowPlacementMode { Normal, Maximized, Minimized, Fullscreen, CompactOverlay, Music };
struct WindowPlacementSnapshot {
    GeometryRect bounds;   // Outer window physical pixels, virtual-desktop top-left origin.
    GeometryRect workArea; // Physical pixels on the monitor at capture time.
    double dpi{96};
    // In-process ordering only. Not serialized, and zero for disk-loaded snapshots.
    std::uint64_t captureOrder{};
};
// UI caller supplies its actual presenter/state and physical bounds; never call
// this with fullscreen/compact bounds falsely labelled Normal. The model never
// queries a HWND and doesn't treat a maximized rectangle as a normal restore size.
std::optional<WindowPlacementSnapshot> CaptureWindowPlacement(GeometryRect bounds,
    GeometryRect workArea, double dpi, WindowPlacementMode mode) noexcept;
// Caller selects a presently available monitor (DisplayArea or MonitorFromRect).
// No monitor handle/name is persisted. Preserve logical size across DPI changes,
// then constrain to the selected work area, even if it is smaller than minimum.
std::optional<GeometryRect> RestoreWindowPlacement(WindowPlacementSnapshot const&,
    GeometryRect currentWorkArea, double currentDpi, GeometrySize minimumPixels) noexcept;

class WindowPlacementStore final {
public:
    // Blocking local IO: execute on a background MTA thread. Missing file -> null;
    // malformed/oversized/newer schema -> throw without touching the original.
    static std::optional<WindowPlacementSnapshot> Load();
    // Returns false for a stale capture or a loaded snapshot (captureOrder=0).
    // Last accepted capture wins across windows, regardless of worker completion
    // order. Caller should debounce events, and await its pending write at exit.
    static bool Save(WindowPlacementSnapshot const&);
};
}

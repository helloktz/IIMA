// Adapted from IINA InteractiveMode.swift (InteractiveModeController/CropBoxView)
// and PlayerCore.setCrop. Copyright (c) 2026 lhc and contributors, GPL-3.0.
// See references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace iima {
struct CropPoint { double x{}, y{}; };
struct CropRect {
    double x{}, y{}, width{}, height{};
    double Right() const noexcept { return x + width; }
    double Bottom() const noexcept { return y + height; }
};
struct CropPixels {
    int x{}, y{}, width{}, height{};
    bool operator==(CropPixels const&) const = default;
};
// All display coordinates use the SAME units (WinUI DIPs or physical pixels),
// with a top-left origin. Source dimensions are unrotated crop-input pixels.
// The only supported transform order is source mirror/flip -> clockwise
// quarter-turn -> positive axis scale/translation. The caller must prove this
// from the actual presentation pipeline, not guess from mpv option names.
struct CropMapping {
    int sourceWidth{}, sourceHeight{};
    CropRect viewport;
    CropRect image; // entire transformed image, may exceed viewport when zoomed
    int clockwiseRotation{}; // exactly 0,90,180,270
    bool mirror{}, flip{}; // in SOURCE axes, before the rotation
    bool mappingConfirmed{};
    bool hasOtherSpatialFilters{}; // crop/expand/skew/perspective/etc: reject
    std::uint64_t mediaGeneration{};
};
// Convenience only when presentation is known to use this exact fit model.
// Zoom 1 fits the display-aspect corrected image in viewport; pan is DIPs, not
// an mpv video-pan coefficient. Returns null for invalid or unbounded inputs.
std::optional<CropMapping> MakeCropMapping(int sourceWidth, int sourceHeight,
    CropRect viewport, double pixelAspect, int clockwiseRotation,
    bool mirror, bool flip, double zoom, CropPoint pan,
    std::uint64_t generation, bool mappingConfirmed) noexcept;
bool IsCropMappingValid(CropMapping const&) noexcept;
// With clampToImage=false, black bars and points outside viewport are rejected.
// With true, drag coordinates are clamped to visible image intersection.
std::optional<CropPoint> DisplayToCropPixels(CropMapping const&, CropPoint,
    bool clampToImage = false) noexcept;
std::optional<CropPoint> CropPixelsToDisplay(CropMapping const&, CropPoint) noexcept;
std::optional<CropRect> CropRectToDisplay(CropMapping const&, CropRect) noexcept;
// Normalized pixel rect conversion: snap at most 4 source pixels from an edge,
// then outward rounding so no selected source pixel is accidentally excluded.
// Explicit minimum size 1 replaces upstream's possible negative/zero rectangle.
std::optional<CropPixels> QuantizeCropRect(CropRect, int width, int height,
    double edgeSnapPixels = 4) noexcept;

// PlayerCore.setCrop + NSSize.crop: truncate size, center in source pixels.
// Pixel aspect/rotation are intentionally not folded into a source crop.
std::optional<CropPixels> CenteredAspectCrop(int width, int height,
    double aspectWidth, double aspectHeight) noexcept;

enum class CropHandle { None, NewSelection, Move, North, NorthEast, East,
    SouthEast, South, SouthWest, West, NorthWest };
struct CropCommit {
    std::uint64_t mediaGeneration{};
    CropPixels pixels;
    bool removeManagedCrop{};
    // Empty when removeManagedCrop is true; use vf remove @iina_crop instead.
    // Nonempty value is one MPVFilter expression, not a shell command.
    std::string filterExpression;
};
class CropSelection final {
public:
    // Caller owns pause/restore policy. Begin/Cancel never submit mpv commands.
    // Default selects whole video; optional initial rect must be source pixels
    // with any previous managed crop temporarily disabled before editing.
    bool Begin(CropMapping mapping, std::optional<CropPixels> initial = {});
    bool Active() const noexcept { return active_; }
    bool Dragging() const noexcept { return dragging_; }
    CropRect Selection() const noexcept { return selection_; }
    std::optional<CropRect> DisplayRect() const noexcept;
    // Cancel in-progress drag on resize/DPI mapping change. Media/source changes
    // cancel the whole interaction; they cannot retag a selection as new media.
    bool UpdateMapping(CropMapping mapping);
    CropHandle HitTest(CropPoint display, double handleRadius = 6) const noexcept;
    bool BeginDrag(CropHandle, CropPoint display);
    bool DragTo(CropPoint display);
    void EndDrag() noexcept;
    void CancelDrag() noexcept;
    bool SetPixels(CropPixels);
    // Aspect in source-pixel axes, matching IINA predefined crop presets.
    bool SelectCenteredAspect(double width, double height);
    std::optional<CropCommit> Confirm();
    void Cancel() noexcept;
private:
    CropMapping mapping_;
    CropRect selection_;
    CropRect dragSelection_;
    CropRect dragDisplay_;
    CropPoint dragAnchor_;
    CropHandle handle_{CropHandle::None};
    bool active_{}, dragging_{};
};
}

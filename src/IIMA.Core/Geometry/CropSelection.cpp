// Adapted from IINA InteractiveMode.swift / PlayerCore.setCrop.
// Copyright (c) 2026 lhc and contributors, GPL-3.0.
#include "CropSelection.h"
#include "../Filters/MPVFilter.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace iima {
namespace {
bool Finite(double value) noexcept { return std::isfinite(value) && std::abs(value) <= 1.0e12; }
bool PointValid(CropPoint point) noexcept { return Finite(point.x) && Finite(point.y); }
bool RectValid(CropRect rect) noexcept {
    return Finite(rect.x) && Finite(rect.y) && Finite(rect.width) && Finite(rect.height) &&
        rect.width > 0 && rect.height > 0 && Finite(rect.Right()) && Finite(rect.Bottom());
}
bool Contains(CropRect rect, CropPoint point) noexcept {
    return point.x >= rect.x && point.x <= rect.Right() && point.y >= rect.y && point.y <= rect.Bottom();
}
CropRect Intersection(CropRect a, CropRect b) noexcept {
    const auto left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    return {left, top, std::min(a.Right(), b.Right()) - left, std::min(a.Bottom(), b.Bottom()) - top};
}
CropPoint Clamp(CropPoint p, CropRect rect) noexcept {
    return {std::clamp(p.x, rect.x, rect.Right()), std::clamp(p.y, rect.y, rect.Bottom())};
}
bool QuarterTurn(int degrees) noexcept { return degrees == 90 || degrees == 270; }
CropPoint ToSource(CropMapping const& mapping, CropPoint display) noexcept {
    const auto u = (display.x - mapping.image.x) / mapping.image.width;
    const auto v = (display.y - mapping.image.y) / mapping.image.height;
    CropPoint normalized{u, v};
    switch (mapping.clockwiseRotation) {
    case 90: normalized = {v, 1 - u}; break;
    case 180: normalized = {1 - u, 1 - v}; break;
    case 270: normalized = {1 - v, u}; break;
    }
    if (mapping.mirror) normalized.x = 1 - normalized.x;
    if (mapping.flip) normalized.y = 1 - normalized.y;
    return {std::clamp(normalized.x, 0.0, 1.0) * mapping.sourceWidth,
        std::clamp(normalized.y, 0.0, 1.0) * mapping.sourceHeight};
}
CropPoint ToDisplay(CropMapping const& mapping, CropPoint source) noexcept {
    auto x = source.x / mapping.sourceWidth;
    auto y = source.y / mapping.sourceHeight;
    if (mapping.mirror) x = 1 - x;
    if (mapping.flip) y = 1 - y;
    CropPoint normalized{x, y};
    switch (mapping.clockwiseRotation) {
    case 90: normalized = {1 - y, x}; break;
    case 180: normalized = {1 - x, 1 - y}; break;
    case 270: normalized = {y, 1 - x}; break;
    }
    return {mapping.image.x + normalized.x * mapping.image.width,
        mapping.image.y + normalized.y * mapping.image.height};
}
template<class Transform> CropRect TransformRect(CropRect rect, Transform transform) noexcept {
    const std::array points{transform(CropPoint{rect.x, rect.y}), transform(CropPoint{rect.Right(), rect.y}),
        transform(CropPoint{rect.x, rect.Bottom()}), transform(CropPoint{rect.Right(), rect.Bottom()})};
    double left = points[0].x, right = left, top = points[0].y, bottom = top;
    for (auto const& point : points) {
        left = std::min(left, point.x); right = std::max(right, point.x);
        top = std::min(top, point.y); bottom = std::max(bottom, point.y);
    }
    return {left, top, right - left, bottom - top};
}
bool InSource(CropRect rect, CropMapping const& mapping) noexcept {
    return RectValid(rect) && rect.x >= 0 && rect.y >= 0 &&
        rect.Right() <= mapping.sourceWidth && rect.Bottom() <= mapping.sourceHeight;
}
CropRect Rect(CropPixels pixels) noexcept {
    return {static_cast<double>(pixels.x), static_cast<double>(pixels.y),
        static_cast<double>(pixels.width), static_cast<double>(pixels.height)};
}
}
bool IsCropMappingValid(CropMapping const& mapping) noexcept {
    if (!mapping.mappingConfirmed || mapping.hasOtherSpatialFilters || mapping.sourceWidth <= 0 ||
        mapping.sourceHeight <= 0 || mapping.sourceWidth > 1048576 || mapping.sourceHeight > 1048576 ||
        !RectValid(mapping.viewport) || !RectValid(mapping.image) ||
        !RectValid(Intersection(mapping.viewport, mapping.image))) return false;
    return mapping.clockwiseRotation == 0 || mapping.clockwiseRotation == 90 ||
        mapping.clockwiseRotation == 180 || mapping.clockwiseRotation == 270;
}
std::optional<CropMapping> MakeCropMapping(int width, int height, CropRect viewport,
    double pixelAspect, int rotation, bool mirror, bool flip, double zoom,
    CropPoint pan, std::uint64_t generation, bool confirmed) noexcept {
    if (width <= 0 || height <= 0 || !RectValid(viewport) || !PointValid(pan) ||
        !Finite(pixelAspect) || pixelAspect <= 0 || pixelAspect > 1000 ||
        !Finite(zoom) || zoom <= 0 || zoom > 1000) return {};
    double displayWidth = width * pixelAspect, displayHeight = height;
    if (QuarterTurn(rotation)) std::swap(displayWidth, displayHeight);
    const auto scale = std::min(viewport.width / displayWidth, viewport.height / displayHeight) * zoom;
    displayWidth *= scale; displayHeight *= scale;
    CropMapping mapping{width, height, viewport,
        {viewport.x + (viewport.width - displayWidth) / 2 + pan.x,
         viewport.y + (viewport.height - displayHeight) / 2 + pan.y, displayWidth, displayHeight},
        rotation, mirror, flip, confirmed, false, generation};
    if (!IsCropMappingValid(mapping)) return {};
    return mapping;
}
std::optional<CropPoint> DisplayToCropPixels(CropMapping const& mapping, CropPoint point, bool clamp) noexcept {
    if (!IsCropMappingValid(mapping) || !PointValid(point)) return {};
    const auto visible = Intersection(mapping.image, mapping.viewport);
    if (clamp) point = Clamp(point, visible);
    else if (!Contains(visible, point)) return {};
    return ToSource(mapping, point);
}
std::optional<CropPoint> CropPixelsToDisplay(CropMapping const& mapping, CropPoint point) noexcept {
    if (!IsCropMappingValid(mapping) || !PointValid(point) || point.x < 0 || point.y < 0 ||
        point.x > mapping.sourceWidth || point.y > mapping.sourceHeight) return {};
    return ToDisplay(mapping, point);
}
std::optional<CropRect> CropRectToDisplay(CropMapping const& mapping, CropRect rect) noexcept {
    if (!IsCropMappingValid(mapping) || !InSource(rect, mapping)) return {};
    return TransformRect(rect, [&](CropPoint point) { return ToDisplay(mapping, point); });
}
std::optional<CropPixels> QuantizeCropRect(CropRect rect, int width, int height, double snap) noexcept {
    if (width <= 0 || height <= 0 || width > 1048576 || height > 1048576 ||
        !RectValid(rect) || rect.x < 0 || rect.y < 0 || rect.Right() > width || rect.Bottom() > height ||
        !Finite(snap) || snap < 0 || snap > 4) return {};
    double left = rect.x, top = rect.y, right = rect.Right(), bottom = rect.Bottom();
    if (left <= snap) left = 0;
    if (top <= snap) top = 0;
    if (width - right <= snap) right = width;
    if (height - bottom <= snap) bottom = height;
    const int x = std::clamp(static_cast<int>(std::floor(left)), 0, width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(top)), 0, height - 1);
    const int endX = std::clamp(static_cast<int>(std::ceil(right)), x + 1, width);
    const int endY = std::clamp(static_cast<int>(std::ceil(bottom)), y + 1, height);
    return CropPixels{x, y, endX - x, endY - y};
}
bool CropSelection::Begin(CropMapping mapping, std::optional<CropPixels> initial) {
    Cancel();
    if (!IsCropMappingValid(mapping)) return false;
    mapping_ = mapping;
    selection_ = initial ? Rect(*initial) : CropRect{0, 0, static_cast<double>(mapping.sourceWidth), static_cast<double>(mapping.sourceHeight)};
    if (!InSource(selection_, mapping_)) return false;
    active_ = true;
    return true;
}
std::optional<CropRect> CropSelection::DisplayRect() const noexcept {
    if (!active_) return {};
    return CropRectToDisplay(mapping_, selection_);
}
bool CropSelection::UpdateMapping(CropMapping mapping) {
    if (!active_) return false;
    if (!IsCropMappingValid(mapping) || mapping.mediaGeneration != mapping_.mediaGeneration ||
        mapping.sourceWidth != mapping_.sourceWidth || mapping.sourceHeight != mapping_.sourceHeight) {
        Cancel(); return false;
    }
    CancelDrag(); mapping_ = mapping; return true;
}
CropHandle CropSelection::HitTest(CropPoint point, double radius) const noexcept {
    if (!active_ || !Finite(radius) || radius < 0 || radius > 64 || !DisplayToCropPixels(mapping_, point)) return CropHandle::None;
    const auto display = DisplayRect();
    if (!display) return CropHandle::None;
    const auto rect = *display;
    if (point.x >= rect.x - radius && point.x <= rect.Right() + radius &&
        point.y >= rect.y - radius && point.y <= rect.Bottom() + radius) {
        const auto leftDistance = std::abs(point.x - rect.x), rightDistance = std::abs(point.x - rect.Right());
        const auto topDistance = std::abs(point.y - rect.y), bottomDistance = std::abs(point.y - rect.Bottom());
        const bool left = leftDistance <= radius && leftDistance <= rightDistance;
        const bool right = rightDistance <= radius && rightDistance < leftDistance;
        const bool top = topDistance <= radius && topDistance <= bottomDistance;
        const bool bottom = bottomDistance <= radius && bottomDistance < topDistance;
        if (top && left) return CropHandle::NorthWest;
        if (top && right) return CropHandle::NorthEast;
        if (bottom && left) return CropHandle::SouthWest;
        if (bottom && right) return CropHandle::SouthEast;
        if (left) return CropHandle::West;
        if (right) return CropHandle::East;
        if (top) return CropHandle::North;
        if (bottom) return CropHandle::South;
        if (Contains(rect, point)) return CropHandle::Move;
    }
    return CropHandle::NewSelection;
}
bool CropSelection::BeginDrag(CropHandle handle, CropPoint display) {
    if (!active_ || dragging_ || !DisplayToCropPixels(mapping_, display)) return false;
    switch (handle) {
    case CropHandle::NewSelection: case CropHandle::Move:
    case CropHandle::North: case CropHandle::NorthEast: case CropHandle::East:
    case CropHandle::SouthEast: case CropHandle::South: case CropHandle::SouthWest:
    case CropHandle::West: case CropHandle::NorthWest: break;
    default: return false;
    }
    const auto rect = DisplayRect();
    if (!rect) return false;
    // Explicit handles permit keyboard/accessible controls to select an edge.
    // The UI normally passes HitTest's result, including corner handles.
    dragging_ = true; handle_ = handle; dragAnchor_ = display;
    dragSelection_ = selection_; dragDisplay_ = *rect;
    return true;
}
bool CropSelection::DragTo(CropPoint display) {
    if (!active_ || !dragging_ || !PointValid(display)) return false;
    display = Clamp(display, Intersection(mapping_.viewport, mapping_.image));
    const double dx = display.x - dragAnchor_.x, dy = display.y - dragAnchor_.y;
    auto left = dragDisplay_.x, right = dragDisplay_.Right();
    auto top = dragDisplay_.y, bottom = dragDisplay_.Bottom();
    const auto bounds = mapping_.image;
    const auto minWidth = bounds.width / (QuarterTurn(mapping_.clockwiseRotation) ? mapping_.sourceHeight : mapping_.sourceWidth);
    const auto minHeight = bounds.height / (QuarterTurn(mapping_.clockwiseRotation) ? mapping_.sourceWidth : mapping_.sourceHeight);
    if (handle_ == CropHandle::NewSelection) {
        left = std::min(dragAnchor_.x, display.x); right = std::max(dragAnchor_.x, display.x);
        top = std::min(dragAnchor_.y, display.y); bottom = std::max(dragAnchor_.y, display.y);
        if (right - left < minWidth || bottom - top < minHeight) return false;
    } else if (handle_ == CropHandle::Move) {
        left = std::clamp(left + dx, bounds.x, bounds.Right() - dragDisplay_.width);
        top = std::clamp(top + dy, bounds.y, bounds.Bottom() - dragDisplay_.height);
        right = left + dragDisplay_.width; bottom = top + dragDisplay_.height;
    } else {
        const bool west = handle_ == CropHandle::West || handle_ == CropHandle::NorthWest || handle_ == CropHandle::SouthWest;
        const bool east = handle_ == CropHandle::East || handle_ == CropHandle::NorthEast || handle_ == CropHandle::SouthEast;
        const bool north = handle_ == CropHandle::North || handle_ == CropHandle::NorthWest || handle_ == CropHandle::NorthEast;
        const bool south = handle_ == CropHandle::South || handle_ == CropHandle::SouthWest || handle_ == CropHandle::SouthEast;
        if (west) left = std::clamp(left + dx, bounds.x, right - minWidth);
        if (east) right = std::clamp(right + dx, left + minWidth, bounds.Right());
        if (north) top = std::clamp(top + dy, bounds.y, bottom - minHeight);
        if (south) bottom = std::clamp(bottom + dy, top + minHeight, bounds.Bottom());
    }
    auto candidate = TransformRect({left, top, right - left, bottom - top},
        [&](CropPoint point) { return ToSource(mapping_, point); });
    if (!InSource(candidate, mapping_)) return false;
    selection_ = candidate;
    return true;
}
void CropSelection::EndDrag() noexcept { dragging_ = false; handle_ = CropHandle::None; }
void CropSelection::CancelDrag() noexcept {
    if (dragging_) selection_ = dragSelection_;
    EndDrag();
}
bool CropSelection::SetPixels(CropPixels pixels) {
    if (!active_ || !InSource(Rect(pixels), mapping_)) return false;
    CancelDrag(); selection_ = Rect(pixels); return true;
}
bool CropSelection::SelectCenteredAspect(double width, double height) {
    if (!active_ || !Finite(width) || !Finite(height) || width <= 0 || height <= 0) return false;
    const auto ratio = width / height;
    double selectedWidth = mapping_.sourceWidth;
    double selectedHeight = selectedWidth / ratio;
    if (selectedHeight > mapping_.sourceHeight) {
        selectedHeight = mapping_.sourceHeight; selectedWidth = selectedHeight * ratio;
    }
    if (!Finite(selectedWidth) || !Finite(selectedHeight) || selectedWidth < 1 || selectedHeight < 1) return false;
    CancelDrag();
    selection_ = {(mapping_.sourceWidth - selectedWidth) / 2, (mapping_.sourceHeight - selectedHeight) / 2,
        selectedWidth, selectedHeight};
    return true;
}
std::optional<CropCommit> CropSelection::Confirm() {
    if (!active_) return {};
    const auto pixels = QuantizeCropRect(selection_, mapping_.sourceWidth, mapping_.sourceHeight);
    if (!pixels) return {};
    CropCommit result{mapping_.mediaGeneration, *pixels,
        pixels->x == 0 && pixels->y == 0 && pixels->width == mapping_.sourceWidth && pixels->height == mapping_.sourceHeight, {}};
    if (!result.removeManagedCrop) {
        const auto filter = MPVFilter::Crop(pixels->width, pixels->height, pixels->x, pixels->y);
        result.filterExpression = MPVFilter(filter.Name(), std::string("iina_crop"), filter.Params()).StringFormat();
    }
    // Commit returns a request only: caller waits for mpv reply/property before
    // presenting this as applied and decides whether it still owns pause state.
    active_ = false; EndDrag();
    return result;
}
void CropSelection::Cancel() noexcept { active_ = false; dragging_ = false; handle_ = CropHandle::None; }
}

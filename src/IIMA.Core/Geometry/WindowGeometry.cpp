// Adapted from IINA GeometryDef.swift and MainWindowController.swift.
// Copyright © 2017 lhc. SPDX-License-Identifier: GPL-3.0-or-later
#include "WindowGeometry.h"
#include <algorithm>
#include <charconv>
#include <cmath>
namespace iima {
namespace {
std::optional<GeometryLength> ReadLength(std::string_view text, std::size_t& offset) {
    auto start = offset;
    while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') ++offset;
    if (offset == start) return {};
    double value{};
    auto [end, ec] = std::from_chars(text.data() + start, text.data() + offset, value);
    if (ec != std::errc{} || end != text.data() + offset || !std::isfinite(value)) return {};
    bool percentage = offset < text.size() && text[offset] == '%';
    if (percentage) ++offset;
    return GeometryLength{value, percentage};
}
bool ValidRect(GeometryRect rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
        std::isfinite(rect.width) && std::isfinite(rect.height) &&
        rect.width > 0 && rect.height > 0;
}
}
double GeometryLength::Resolve(double available) const noexcept {
    return percentage ? value * 0.01 * available : value;
}
std::optional<WindowGeometry> WindowGeometry::Parse(std::string_view text) {
    if (text.empty()) return {};
    WindowGeometry result;
    std::size_t offset{};
    if (text[offset] >= '0' && text[offset] <= '9') {
        result.width = ReadLength(text, offset);
        if (!result.width) return {};
    }
    if (offset < text.size() && text[offset] == 'x') {
        ++offset;
        result.height = ReadLength(text, offset);
        if (!result.height) return {};
    }
    if (offset < text.size() && (text[offset] == '+' || text[offset] == '-')) {
        result.right = text[offset++] == '-';
        result.x = ReadLength(text, offset);
        if (!result.x || offset >= text.size() || (text[offset] != '+' && text[offset] != '-')) return {};
        result.bottom = text[offset++] == '-';
        result.y = ReadLength(text, offset);
        if (!result.y) return {};
    }
    if (offset != text.size() || (!result.width && !result.height && !result.x)) return {};
    return result;
}
std::optional<GeometryRect> WindowGeometry::ResolveVisible(GeometryRect current,
    GeometryRect work, GeometrySize minimum) const noexcept {
    auto resolved = Resolve(current, work, minimum);
    if (!resolved) return {};
    // Fit once, preserving the frame aspect ratio even on a monitor smaller
    // than the preferred minimum. A disconnected/off-screen geometry is not
    // allowed to strand the title bar outside the current work area.
    const bool resized = (width && (width->value != 0 || width->percentage)) ||
        (height && (height->value != 0 || height->percentage));
    const double minimumFactor = resized ? std::max({1.0, minimum.width / resolved->width,
        minimum.height / resolved->height}) : 1.0;
    const double factor = std::min({minimumFactor, work.width / resolved->width, work.height / resolved->height});
    resolved->width *= factor;
    resolved->height *= factor;
    if (!ValidRect(*resolved) || resolved->width < 1 || resolved->height < 1) return {};
    resolved->width = std::min(resolved->width, work.width);
    resolved->height = std::min(resolved->height, work.height);
    // Re-evaluate anchors after fitting: percentage coordinates refer to the
    // fitted window center, and negative offsets to its right/bottom edges.
    auto anchors = *this;
    anchors.width.reset(); anchors.height.reset();
    if (!x && resized) resolved->x = work.x + (work.width - resolved->width) / 2;
    if (!y && resized) resolved->y = work.y + (work.height - resolved->height) / 2;
    resolved = anchors.Resolve(*resolved, work, minimum);
    if (!resolved) return {};
    resolved->x = std::clamp(resolved->x, work.x, work.x + work.width - resolved->width);
    resolved->y = std::clamp(resolved->y, work.y, work.y + work.height - resolved->height);
    return resolved;
}
std::optional<GeometryRect> WindowGeometry::Resolve(GeometryRect current,
    GeometryRect workArea, GeometrySize minimum) const noexcept {
    if (!ValidRect(current) || !ValidRect(workArea) ||
        !std::isfinite(minimum.width) || !std::isfinite(minimum.height) ||
        minimum.width <= 0 || minimum.height <= 0) return {};
    auto aspect = current.width / current.height;
    bool resized = false;
    if (width && (width->value != 0 || width->percentage)) {
        current.width = std::max(minimum.width, width->Resolve(workArea.width));
        current.height = current.width / aspect;
        resized = true;
    } else if (height && (height->value != 0 || height->percentage)) {
        current.height = std::max(minimum.height, height->Resolve(workArea.height));
        current.width = current.height * aspect;
        resized = true;
    }
    if (x) {
        auto position = x->Resolve(workArea.width) - (x->percentage ? current.width / 2 : 0);
        current.x = workArea.x + (right ? workArea.width - position - current.width : position);
    } else if (resized) current.x = workArea.x + (workArea.width - current.width) / 2;
    if (y) {
        auto position = y->Resolve(workArea.height) - (y->percentage ? current.height / 2 : 0);
        current.y = workArea.y + (bottom ? workArea.height - position - current.height : position);
    } else if (resized) current.y = workArea.y + (workArea.height - current.height) / 2;
    // Unlike the Cocoa implementation, preserve absolute existing coordinates
    // when no axis/size is specified rather than adding the monitor origin twice.
    return ValidRect(current) ? std::optional{current} : std::nullopt;
}
}

namespace iima {
std::optional<GeometryRect> ScaledVideoWindow(GeometrySize video, GeometryRect current,
    GeometryRect work, GeometrySize chrome, GeometrySize minimum,
    std::optional<double> scale, bool physicalResolution, double dpiScale) noexcept {
    auto positive = [](double v) { return std::isfinite(v) && v > 0 && v <= 1000000; };
    auto nonnegative = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1000000; };
    if (!positive(video.width) || !positive(video.height) ||
        !positive(current.width) || !positive(current.height) ||
        !positive(work.width) || !positive(work.height) ||
        !std::isfinite(current.x) || !std::isfinite(current.y) ||
        !std::isfinite(work.x) || !std::isfinite(work.y) ||
        !nonnegative(chrome.width) || !nonnegative(chrome.height) ||
        !positive(minimum.width) || !positive(minimum.height) ||
        !std::isfinite(dpiScale) || dpiScale < 0.5 || dpiScale > 8 ||
        (scale && (!std::isfinite(*scale) || *scale <= 0 || *scale > 16))) return {};
    const auto availableWidth = work.width - chrome.width;
    const auto availableHeight = work.height - chrome.height;
    if (availableWidth < 1 || availableHeight < 1) return {};
    const double units = physicalResolution ? 1.0 : dpiScale;
    const double baseWidth = video.width * units, baseHeight = video.height * units;
    const auto maximum = std::min(availableWidth / baseWidth, availableHeight / baseHeight);
    const auto smallest = std::max({0.0, (minimum.width - chrome.width) / baseWidth,
        (minimum.height - chrome.height) / baseHeight});
    // A small monitor takes precedence over the preferred minimum. Clamp the
    // scale once so pixel aspect is preserved; never clamp width/height apart.
    const auto factor = std::min(maximum, std::max(scale.value_or(maximum), smallest));
    GeometryRect result{0, 0, baseWidth * factor + chrome.width, baseHeight * factor + chrome.height};
    if (!positive(result.width) || !positive(result.height)) return {};
    result.width = std::min(work.width, result.width);
    result.height = std::min(work.height, result.height);
    if (result.width < 1 || result.height < 1) return {};
    const double centerX = scale ? current.x + current.width / 2 : work.x + work.width / 2;
    const double centerY = scale ? current.y + current.height / 2 : work.y + work.height / 2;
    result.x = std::clamp(centerX - result.width / 2, work.x, work.x + work.width - result.width);
    result.y = std::clamp(centerY - result.height / 2, work.y, work.y + work.height - result.height);
    return result;
}
}

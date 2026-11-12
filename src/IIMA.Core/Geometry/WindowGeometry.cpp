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

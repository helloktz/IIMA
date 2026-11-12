// Adapted from IINA Aspect.swift and Regex.aspect. Copyright © 2016 lhc.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Aspect.h"
#include <charconv>
#include <cmath>
namespace iima {
namespace {
std::optional<double> Component(std::string_view text) {
    if (text.empty()) return {};
    bool dot = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '.' && !dot && i > 0 && i + 1 < text.size()) dot = true;
        else if (text[i] < '0' || text[i] > '9') return {};
    }
    double value{};
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() ||
        !std::isfinite(value) || value <= 0) return {};
    return value;
}
}
std::optional<Aspect> Aspect::Parse(std::string_view text) {
    auto colon = text.find(':');
    if (colon == std::string_view::npos) return {};
    auto width = Component(text.substr(0, colon));
    auto height = Component(text.substr(colon + 1));
    if (!width || !height || !std::isfinite(*width / *height)) return {};
    return Aspect{*width, *height};
}
}

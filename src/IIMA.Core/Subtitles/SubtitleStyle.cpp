// IINA Preference.swift and PlayerCore.setSubText* / setSubFont.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "SubtitleStyle.h"
#include <charconv>
#include <cmath>
#include <stdexcept>
namespace iima {
namespace {
bool Unit(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }
std::string Decimal(double value) {
    char buffer[64];
    auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec != std::errc{}) throw std::invalid_argument("Invalid subtitle number");
    return {buffer, end};
}
}
bool SubtitleColor::Valid() const noexcept { return Unit(red) && Unit(green) && Unit(blue) && Unit(alpha); }
std::string SubtitleColor::MpvString() const {
    if (!Valid()) throw std::invalid_argument("Invalid subtitle RGBA color");
    return Decimal(red) + "/" + Decimal(green) + "/" + Decimal(blue) + "/" + Decimal(alpha);
}
bool SubtitleStyle::Valid() const noexcept {
    return !font.empty() && font.find('\0') == std::string::npos &&
        std::isfinite(fontSize) && fontSize >= 1 && fontSize <= 9000 &&
        std::isfinite(outlineSize) && outlineSize >= 0 && outlineSize <= 100 &&
        color.Valid() && outlineColor.Valid() && backgroundColor.Valid();
}
std::optional<std::vector<std::pair<std::string, std::string>>> SubtitleStyle::Properties() const {
    if (!Valid()) return {};
    return std::vector<std::pair<std::string, std::string>>{
        {"sub-font", font}, {"options/sub-font-size", Decimal(fontSize)},
        {"options/sub-bold", bold ? "yes" : "no"}, {"options/sub-italic", italic ? "yes" : "no"},
        {"options/sub-color", color.MpvString()}, {"options/sub-outline-color", outlineColor.MpvString()},
        {"options/sub-outline-size", Decimal(outlineSize)}, {"options/sub-back-color", backgroundColor.MpvString()}};
}
}

// IINA Preference.swift and PlayerCore.setSubText* / setSubFont.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "SubtitleStyle.h"
#include <charconv>
#include <cstdint>
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
std::optional<SubtitleColor> SubtitleColor::ParseHex(std::string_view text) noexcept {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') return {};
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data() + 1, text.data() + text.size(), value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return {};
    const double alpha = text.size() == 9 ? ((value >> 24) & 255) / 255.0 : 1;
    return SubtitleColor{((value >> 16) & 255) / 255.0, ((value >> 8) & 255) / 255.0,
                         (value & 255) / 255.0, alpha};
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

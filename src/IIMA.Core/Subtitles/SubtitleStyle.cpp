// IINA Preference.swift and PlayerCore.setSubText* / setSubFont.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "SubtitleStyle.h"
#include <charconv>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <initializer_list>
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
std::optional<double> StepSubtitleScale(double current, int direction) noexcept {
    if (direction == 0) return 1.0;
    if ((direction != -1 && direction != 1) || !std::isfinite(current) || current <= 0 || current > 100) return {};
    const auto display = current >= 1 ? current : -1 / current;
    if (!std::isfinite(display) || !std::isfinite(display * 100)) return {};
    const auto rounded = std::round(display * 100) / 100;
    const auto amount = direction > 0 ? 0.1 : -0.1;
    auto next = rounded + amount;
    if ((next > 0 && next < 1) || (next > -1 && next < 0)) next = -rounded + amount;
    if (!std::isfinite(next) || next == 0) return {};
    const auto scale = std::abs(next > 0 ? next : 1 / next);
    // Honor current mpv's range without silently changing the requested step.
    if (!std::isfinite(scale) || scale <= 0 || scale > 100) return {};
    return scale;
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
std::optional<SubtitleColor> SubtitleColor::ParseMpv(std::string_view text) noexcept {
    // IINA Extensions.NSColor(mpvColorString:) accepts RGB or RGBA fractions.
    // Unlike its permissive hex scanner, reject incomplete/invalid components.
    if (text.starts_with('#')) return ParseHex(text);
    if (text.empty() || text.size() > 256) return {};
    double channels[4]{0, 0, 0, 1};
    unsigned count{};
    while (!text.empty()) {
        if (count == 4) return {};
        auto slash = text.find('/');
        auto component = text.substr(0, slash);
        if (component.empty()) return {};
        auto result = std::from_chars(component.data(), component.data() + component.size(), channels[count]);
        if (result.ec != std::errc{} || result.ptr != component.data() + component.size() || !Unit(channels[count])) return {};
        ++count;
        if (slash == std::string_view::npos) break;
        text.remove_prefix(slash + 1);
        if (text.empty()) return {};
    }
    if (count != 3 && count != 4) return {};
    return SubtitleColor{channels[0], channels[1], channels[2], channels[3]};
}
std::string SubtitleColor::HexString() const {
    if (!Valid()) throw std::invalid_argument("Invalid subtitle RGBA color");
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result("#");
    for (double component : {alpha, red, green, blue}) {
        auto value = static_cast<unsigned>(std::lround(component * 255));
        result += digits[value >> 4]; result += digits[value & 15];
    }
    return result;
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

// IINA Preference.swift subtitle defaults and PlayerCore subtitle setters.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace iima {
// MainMenuActions.menuChangeSubScale: signed reciprocal display-space steps.
// direction 0 resets to 1; -1/+1 step. Invalid/zero input is not inverted.
std::optional<double> StepSubtitleScale(double current, int direction) noexcept;

struct SubtitleColor {
    double red{}, green{}, blue{}, alpha{1};
    static std::optional<SubtitleColor> ParseHex(std::string_view text) noexcept;
    static std::optional<SubtitleColor> ParseMpv(std::string_view text) noexcept;
    std::string HexString() const; // #AARRGGBB, rounded to 8-bit sRGB channels.
    bool Valid() const noexcept;
    std::string MpvString() const;
};
struct SubtitleStyle {
    std::string font{"sans-serif"};
    double fontSize{55};
    bool bold{}, italic{};
    SubtitleColor color{1, 1, 1, 1};
    SubtitleColor outlineColor{0, 0, 0, 1};
    SubtitleColor backgroundColor{0, 0, 0, 0};
    double outlineSize{3};
    bool Valid() const noexcept;
    // A plan, not a transaction: each asynchronous mpv property result must
    // still be reported; a later failure cannot roll back earlier properties.
    std::optional<std::vector<std::pair<std::string, std::string>>> Properties() const;
};
}

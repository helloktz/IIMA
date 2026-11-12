// Adapted from IINA Aspect.swift. Copyright © 2016 lhc.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string_view>
namespace iima {
struct Aspect final {
    double width{};
    double height{};
    double Value() const noexcept { return width / height; }
    // Decimal width:height; excludes signs, exponent notation and whitespace.
    static std::optional<Aspect> Parse(std::string_view text);
};
}

// Adapted from IINA MenuController.sameKeyAction/updateKeyEquivalentsFrom.
// Copyright (c) 2016 lhc. GPL-3.0; see references/iina/LICENSE.
#include "MenuKeyBinding.h"
#include "BindingConfiguration.h"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace iima {
namespace {
std::optional<double> MenuNumber(std::string_view text) {
    // Swift Double accepts a leading plus; std::from_chars does not.
    if (text.starts_with('+')) {
        text.remove_prefix(1);
        if (text.starts_with('+') || text.starts_with('-')) return {};
    }
    if (text.empty()) return {};
    double value{};
    auto [end, error] = std::from_chars(text.data(), text.data()+text.size(), value);
    if (error != std::errc{} || end != text.data()+text.size() || !std::isfinite(value)) return {};
    return value;
}
std::vector<std::string> MenuAction(std::span<std::string const> action, bool iinaCommand) {
    std::vector<std::string> result(action.begin(), action.end());
    if (iinaCommand || result.empty() || result.front() != "seek" ||
        result.size() < 3 || result.size() > 4) return result;
    if (result.size() == 4) {
        // Legacy seek amount mode precision. Normalize the combined flags,
        // not the pre-combination last token retained by the Swift snapshot.
        result[2] += "+" + result[3];
        result.pop_back();
    }
    bool relative{}, exact{}, recognized = true;
    std::string_view flags = result[2];
    for (;;) {
        const auto separator = flags.find('+');
        const auto flag = flags.substr(0, separator);
        if (flag == "relative" && !relative) relative = true;
        else if (flag == "exact" && !exact) exact = true;
        else { recognized = false; break; }
        if (separator == flags.npos) break;
        flags.remove_prefix(separator + 1);
    }
    if (recognized) result.pop_back();
    return result;
}
bool CandidateCommand(KeyMapping const& binding) {
    switch (ClassifyBindingAction(binding).kind) {
    case BindingActionKind::Application:
    case BindingActionKind::AbLoop:
    case BindingActionKind::Quit:
    case BindingActionKind::Screenshot:
    case BindingActionKind::MpvCommandTokens: return true;
    default: return false;
    }
}
}
std::optional<MenuBindingMatch> FindMenuKeyBinding(KeyBindingSet const& bindings,
    std::span<std::string const> action, bool iinaCommand,
    std::optional<std::pair<double, double>> numericRange,
    MenuBindingEligibility const& eligible) {
    const auto expected = MenuAction(action, iinaCommand);
    if (expected.empty()) return {};
    const auto expectedNumber = iinaCommand ? std::nullopt : MenuNumber(expected.back());
    if (numericRange && (!expectedNumber || !std::isfinite(numericRange->first) ||
        !std::isfinite(numericRange->second) || numericRange->first > numericRange->second)) return {};
    for (auto const& binding : bindings.Ordered()) {
        if (binding.IsIinaCommand() != iinaCommand || !CandidateCommand(binding) ||
            (eligible && !eligible(binding))) continue;
        const auto actual = MenuAction(binding.Action(), iinaCommand);
        if (actual.size() != expected.size()) continue;
        if (!expectedNumber) {
            if (actual == expected) return MenuBindingMatch{binding, {}};
            continue;
        }
        if (!std::equal(actual.begin(), actual.end()-1, expected.begin())) continue;
        const auto value = MenuNumber(actual.back());
        if (!value) continue;
        const bool matches = numericRange ?
            *value >= numericRange->first && *value <= numericRange->second : *value == *expectedNumber;
        if (matches) return MenuBindingMatch{binding, value};
    }
    return {};
}
}

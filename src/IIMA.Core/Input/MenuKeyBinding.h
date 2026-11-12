// Adapted from IINA MenuController.sameKeyAction/updateKeyEquivalentsFrom.
// Copyright (c) 2016 lhc. GPL-3.0; see references/iina/LICENSE.
#pragma once
#include "KeyMapping.h"
#include <functional>
#include <utility>

namespace iima {
struct MenuBindingMatch {
    KeyMapping binding; // Keep original command/flags for typed dispatch.
    std::optional<double> amount;
};
using MenuBindingEligibility = std::function<bool(KeyMapping const&)>;
// First matching active binding in display order; KeyBindingSet already applies
// default-section filtering and last-definition-wins for each normalized key.
// Numeric final arguments compare as finite doubles; ranges are inclusive.
// Only relative/exact seek flags are ignored for equivalence, never keyframes,
// absolute or percent semantics. The returned binding retains all its flags.
// The built-in classifier rejects unsupported command families, but is NOT a
// full platform dispatcher. Supply eligible to reject unavailable application
// handlers, unsupported key sequences and argument variants. Without it the
// result is only a candidate and MUST be validated before showing a shortcut.
std::optional<MenuBindingMatch> FindMenuKeyBinding(KeyBindingSet const& bindings,
    std::span<std::string const> action, bool iinaCommand,
    std::optional<std::pair<double, double>> numericRange = {},
    MenuBindingEligibility const& eligible = {});
}

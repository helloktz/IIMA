// Derived from IINA SavedFilter.swift, copyright (c) 2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include "MPVFilter.h"
#include <cstdint>

namespace iima {
// Modifier values are platform independent. Command is preserved in persisted
// IINA dictionaries; the Windows UI displays it as Win, never silently Ctrl.
enum class FilterShortcutModifier : std::uint8_t {
    None = 0, Control = 1, Alt = 2, Shift = 4, Windows = 8
};
constexpr FilterShortcutModifier operator|(FilterShortcutModifier left,
                                           FilterShortcutModifier right) noexcept {
    return static_cast<FilterShortcutModifier>(static_cast<unsigned>(left) |
                                               static_cast<unsigned>(right));
}
constexpr bool HasFilterModifier(FilterShortcutModifier value,
                                 FilterShortcutModifier modifier) noexcept {
    return (static_cast<unsigned>(value) & static_cast<unsigned>(modifier)) != 0;
}
struct SavedFilter {
    std::string name;
    std::string filterString;
    std::string shortcutKey;
    FilterShortcutModifier modifiers{FilterShortcutModifier::None};
    // Ephemeral applied-state projection; never serialized as user preference.
    bool enabled{};

    static std::optional<SavedFilter> FromDictionary(MPVFilter::Parameters const& dictionary);
    MPVFilter::Parameters ToDictionary() const;
    std::string ReadableShortcut() const;
    std::optional<MPVFilter> Filter() const;
    // The mpv active filter list is authoritative. A saved filter can be
    // enabled by another UI or command source, not just by its own shortcut.
    void SynchronizeEnabled(std::vector<MPVFilter> const& activeFilters);
};
}

// Derived from IINA SavedFilter.swift, copyright (c) 2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#include "SavedFilter.h"
#include <algorithm>

namespace iima {
std::optional<SavedFilter> SavedFilter::FromDictionary(MPVFilter::Parameters const& dictionary) {
    auto const name = dictionary.find("name");
    auto const filter = dictionary.find("filterString");
    auto const key = dictionary.find("shortcutKey");
    auto const modifiers = dictionary.find("shortcutKeyModifiers");
    if (name == dictionary.end() || filter == dictionary.end() ||
        key == dictionary.end() || modifiers == dictionary.end()) return std::nullopt;
    SavedFilter result{name->second, filter->second, key->second};
    for (auto const flag : modifiers->second) {
        switch (flag) {
        case 'c': result.modifiers = result.modifiers | FilterShortcutModifier::Control; break;
        case 'o': result.modifiers = result.modifiers | FilterShortcutModifier::Alt; break;
        case 's': result.modifiers = result.modifiers | FilterShortcutModifier::Shift; break;
        case 'm': result.modifiers = result.modifiers | FilterShortcutModifier::Windows; break;
        default: break; // Preserve upstream tolerance for future/unknown flags.
        }
    }
    return result;
}
MPVFilter::Parameters SavedFilter::ToDictionary() const {
    std::string flags;
    // Deterministic ordering replaces the unspecified Swift Dictionary order.
    if (HasFilterModifier(modifiers, FilterShortcutModifier::Control)) flags += 'c';
    if (HasFilterModifier(modifiers, FilterShortcutModifier::Alt)) flags += 'o';
    if (HasFilterModifier(modifiers, FilterShortcutModifier::Shift)) flags += 's';
    if (HasFilterModifier(modifiers, FilterShortcutModifier::Windows)) flags += 'm';
    return {{"name", name}, {"filterString", filterString}, {"shortcutKey", shortcutKey},
            {"shortcutKeyModifiers", std::move(flags)}};
}
std::string SavedFilter::ReadableShortcut() const {
    if (shortcutKey.empty()) return {};
    std::string result;
    if (HasFilterModifier(modifiers, FilterShortcutModifier::Control)) result += "Ctrl+";
    if (HasFilterModifier(modifiers, FilterShortcutModifier::Alt)) result += "Alt+";
    if (HasFilterModifier(modifiers, FilterShortcutModifier::Shift)) result += "Shift+";
    if (HasFilterModifier(modifiers, FilterShortcutModifier::Windows)) result += "Win+";
    if (shortcutKey == " ") result += "Space";
    else if (shortcutKey == "\r" || shortcutKey == "\n") result += "Enter";
    else if (shortcutKey == "\t") result += "Tab";
    else if (shortcutKey == "\x1b") result += "Esc";
    else if (shortcutKey.size() == 1 && shortcutKey.front() >= 'a' && shortcutKey.front() <= 'z')
        result += static_cast<char>(shortcutKey.front() - 'a' + 'A');
    else result += shortcutKey;
    return result;
}
std::optional<MPVFilter> SavedFilter::Filter() const { return MPVFilter::Parse(filterString); }
void SavedFilter::SynchronizeEnabled(std::vector<MPVFilter> const& activeFilters) {
    auto const saved = Filter();
    enabled = saved && std::find(activeFilters.begin(), activeFilters.end(), *saved) != activeFilters.end();
}
}

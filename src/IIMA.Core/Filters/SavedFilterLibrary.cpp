// Static port of IINA saved-filter editing, menu projection and toggling.
// Copyright (c) 2016-2017 lhc; GNU GPL v3, references/iina/LICENSE.
#include "SavedFilterLibrary.h"
#include "../Input/KeyMapping.h"
#include <algorithm>
#include <charconv>
#include <utility>

namespace iima {
namespace {
constexpr std::size_t MaximumEntries = 512;
constexpr std::size_t MaximumFilterBytes = 16384;
std::string Trim(std::string_view value) {
    auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!value.empty() && blank(value.front())) value.remove_prefix(1);
    while (!value.empty() && blank(value.back())) value.remove_suffix(1);
    return std::string(value);
}
std::string NameKey(std::string_view value) {
    std::string result(value);
    for (auto& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return result;
}
bool ContainsControl(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
// A saved entry represents one filter, not a chain. Honor mpv fixed-length,
// quoted and bracket-delimited parameter strings so graph commas are valid.
// The mpv parser is still the authority for filter-specific option syntax.
bool SingleFilter(std::string_view text) {
    unsigned brackets{};
    char quote{};
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\0' || c == '\r' || c == '\n') return false;
        if (c == '\\') {
            if (++i == text.size()) return false;
            continue;
        }
        if (quote) { if (c == quote) quote = 0; continue; }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '%') {
            auto end = text.find('%', i + 1);
            if (end != std::string_view::npos && end > i + 1) {
                std::size_t length{};
                auto parsed = std::from_chars(text.data() + i + 1, text.data() + end, length);
                if (parsed.ec == std::errc{} && parsed.ptr == text.data() + end) {
                    if (length > text.size() - end - 1) return false;
                    auto value = text.substr(end + 1, length);
                    if (value.find('\0') != std::string_view::npos) return false;
                    i = end + length;
                    continue;
                }
            }
        }
        if (c == '[') ++brackets;
        else if (c == ']') { if (!brackets) return false; --brackets; }
        else if (c == ',' && !brackets) return false;
    }
    return !quote && brackets == 0;
}
std::optional<std::size_t> Match(SavedFilter const& saved, FilterChainSnapshot const& chain,
                               bool enabled) {
    auto parsed = saved.Filter();
    if (!chain.complete || !parsed) return std::nullopt;
    for (std::size_t i = 0; i < chain.entries.size(); ++i) {
        auto const& item = chain.entries[i];
        if (item.enabled == enabled && item.filter && *item.filter == *parsed) return i;
    }
    return std::nullopt;
}
}
std::vector<SavedFilter> const& SavedFilterLibrary::Values(bool audio) const noexcept {
    return audio ? audio_ : video_;
}
std::optional<std::string> SavedFilterLibrary::ShortcutKey(SavedFilter const& value) {
    if (value.shortcutKey.empty()) return std::string{};
    auto key = value.shortcutKey;
    if (key == " ") key = "SPACE";
    else if (key == "\r" || key == "\n") key = "ENTER";
    else if (key == "\t") key = "TAB";
    else if (key == "\x1b") key = "ESC";
    else if (key == "+") key = "PLUS";
    else if (key == "#") key = "SHARP";
    if (key.size() > 64 || ContainsControl(key) || key.find(' ') != std::string::npos ||
        key.find('+') != std::string::npos || (key.size() > 1 && key.find('-') != std::string::npos) ||
        (static_cast<unsigned>(value.modifiers) & ~15u)) return std::nullopt;
    std::string combined;
    if (HasFilterModifier(value.modifiers, FilterShortcutModifier::Control)) combined += "Ctrl+";
    if (HasFilterModifier(value.modifiers, FilterShortcutModifier::Alt)) combined += "Alt+";
    if (HasFilterModifier(value.modifiers, FilterShortcutModifier::Shift)) combined += "Shift+";
    if (HasFilterModifier(value.modifiers, FilterShortcutModifier::Windows)) combined += "Meta+";
    return NormalizeMpvKey(combined + key);
}
SavedFilterLibraryResult SavedFilterLibrary::Validate(bool audio, SavedFilter& value,
    std::optional<std::size_t> replacing) const {
    value.name = Trim(value.name);
    value.filterString = Trim(value.filterString);
    value.enabled = false;
    if (value.name.empty() || value.name.size() > 256 || ContainsControl(value.name))
        return {false, 0, "A filter name must contain 1..256 bytes without control characters"};
    if (value.filterString.empty() || value.filterString.size() > MaximumFilterBytes ||
        !SingleFilter(value.filterString) || !value.Filter())
        return {false, 0, "Enter one valid filter expression"};
    auto shortcut = ShortcutKey(value);
    if (!shortcut) return {false, 0, "Shortcut must contain one base key and supported modifiers"};
    auto const& entries = Values(audio);
    if (!replacing && video_.size() + audio_.size() >= MaximumEntries)
        return {false, 0, "Saved filter library limit reached"};
    auto name = NameKey(value.name);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (replacing == i) continue;
        if (NameKey(entries[i].name) == name) return {false, 0, "A filter with that name already exists"};
    }
    if (!shortcut->empty()) {
        for (bool category : {false, true}) {
            auto const& candidates = Values(category);
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                if (category == audio && replacing == i) continue;
                if (ShortcutKey(candidates[i]) == shortcut)
                    return {false, 0, "A saved filter already uses that shortcut"};
            }
        }
    }
    return {true, replacing.value_or(entries.size()), {}};
}
std::vector<std::string> SavedFilterLibrary::Restore(std::vector<SavedFilter> video,
                                                    std::vector<SavedFilter> audio) {
    video_.clear(); audio_.clear(); ++revision_;
    std::vector<std::string> issues;
    for (bool category : {false, true}) {
        auto& source = category ? audio : video;
        for (std::size_t i = 0; i < source.size(); ++i) {
            auto result = Add(category, std::move(source[i]));
            if (!result.success) issues.push_back(std::string(category ? "Audio" : "Video") +
                " saved filter #" + std::to_string(i + 1) + ": " + result.error);
        }
    }
    return issues;
}
SavedFilterLibraryResult SavedFilterLibrary::Add(bool audio, SavedFilter value) {
    auto result = Validate(audio, value, std::nullopt);
    if (!result.success) return result;
    (audio ? audio_ : video_).push_back(std::move(value)); ++revision_;
    return result;
}
SavedFilterLibraryResult SavedFilterLibrary::Edit(bool audio, std::size_t index, SavedFilter value) {
    auto& entries = audio ? audio_ : video_;
    if (index >= entries.size()) return {false, index, "Saved filter no longer exists"};
    auto result = Validate(audio, value, index);
    if (!result.success) return result;
    entries[index] = std::move(value); ++revision_;
    return result;
}
SavedFilterLibraryResult SavedFilterLibrary::Copy(bool audio, std::size_t index, std::string newName) {
    auto const& entries = Values(audio);
    if (index >= entries.size()) return {false, index, "Saved filter no longer exists"};
    auto value = entries[index];
    value.name = std::move(newName);
    // Duplicating must not steal the original shortcut.
    value.shortcutKey.clear(); value.modifiers = FilterShortcutModifier::None;
    return Add(audio, std::move(value));
}
SavedFilterLibraryResult SavedFilterLibrary::Move(bool audio, std::size_t index, std::size_t destination) {
    auto& entries = audio ? audio_ : video_;
    if (index >= entries.size() || destination >= entries.size())
        return {false, index, "Saved filter position is out of range"};
    if (index == destination) return {true, index, {}};
    auto entry = std::move(entries[index]);
    entries.erase(entries.begin() + index);
    entries.insert(entries.begin() + destination, std::move(entry)); ++revision_;
    return {true, destination, {}};
}
SavedFilterLibraryResult SavedFilterLibrary::Remove(bool audio, std::size_t index) {
    auto& entries = audio ? audio_ : video_;
    if (index >= entries.size()) return {false, index, "Saved filter no longer exists"};
    entries.erase(entries.begin() + index); ++revision_;
    return {true, index, {}};
}
std::optional<std::pair<bool, std::size_t>> SavedFilterLibrary::FindShortcut(std::string_view key) const {
    auto normalized = NormalizeMpvKey(key);
    if (normalized.empty()) return std::nullopt;
    for (bool category : {false, true}) {
        auto const& entries = Values(category);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            auto shortcut = ShortcutKey(entries[i]);
            if (shortcut && !shortcut->empty() && *shortcut == normalized) return std::pair{category, i};
        }
    }
    return std::nullopt;
}
SavedFilterObservedState SavedFilterLibrary::ObservedState(bool audio, std::size_t index,
    FilterChainSnapshot const& chain) const {
    auto const& entries = Values(audio);
    if (index >= entries.size() || !chain.complete) return SavedFilterObservedState::Unavailable;
    if (Match(entries[index], chain, true)) return SavedFilterObservedState::Enabled;
    if (Match(entries[index], chain, false)) return SavedFilterObservedState::Disabled;
    return SavedFilterObservedState::Absent;
}
SavedFilterApplyPlan SavedFilterLibrary::PlanToggle(bool audio, std::size_t index,
    FilterChainSnapshot const& chain) const {
    SavedFilterApplyPlan plan;
    plan.audio = audio;
    auto const& entries = Values(audio);
    if (index >= entries.size() || !chain.complete) {
        plan.error = "Saved filter or observed filter list is unavailable"; return plan;
    }
    auto const& saved = entries[index];
    if (auto active = Match(saved, chain, true)) {
        plan.edit = FilterChainEditor::Remove(chain, chain.revision, *active);
    } else if (auto disabled = Match(saved, chain, false)) {
        plan.edit = FilterChainEditor::SetEnabled(chain, chain.revision, *disabled, true);
    } else {
        plan.kind = SavedFilterApplyPlan::Kind::Add;
        plan.filterString = saved.filterString;
        return plan;
    }
    if (plan.edit.valid) plan.kind = SavedFilterApplyPlan::Kind::EditChain;
    else plan.error = plan.edit.error;
    return plan;
}
}

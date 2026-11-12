// Windows multi-window persistence adaptation of IINA HistoryController.swift.
// Copyright (c) 2017 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "UserDataMerge.h"
#include <unordered_set>
#include <stdexcept>

namespace iima {
namespace {
bool SameHistory(HistoryEntry const& a, HistoryEntry const& b) {
    return a.path == b.path && a.position == b.position &&
        a.duration == b.duration && a.playedAt == b.playedAt;
}
bool SameFilter(SavedFilter const& a, SavedFilter const& b) {
    // enabled is an ephemeral projection of mpv, not persisted preference.
    return a.name == b.name && a.filterString == b.filterString &&
        a.shortcutKey == b.shortcutKey && a.modifiers == b.modifiers;
}
template<class Map> void MergeMap(Map const& base, Map const& edited, Map& latest) {
    for (auto const& [key, value] : base)
        if (!edited.contains(key)) latest.erase(key);
    for (auto const& [key, value] : edited) {
        auto before = base.find(key);
        if (before == base.end() || before->second != value) latest.insert_or_assign(key, value);
    }
}
void MergeFilters(std::vector<SavedFilter> const& base, std::vector<SavedFilter> const& edited,
                  std::vector<SavedFilter>& latest) {
    auto find = [](auto const& list, std::string const& name) {
        return std::find_if(list.begin(), list.end(), [&](auto const& f) { return f.name == name; });
    };
    for (auto const& before : base)
        if (find(edited, before.name) == edited.end())
            std::erase_if(latest, [&](auto const& f) { return f.name == before.name; });
    for (auto const& value : edited) {
        auto before = find(base, value.name);
        if (before != base.end() && SameFilter(*before, value)) continue;
        auto target = std::find_if(latest.begin(), latest.end(), [&](auto const& f) { return f.name == value.name; });
        if (target == latest.end()) latest.push_back(value);
        else *target = value;
    }
    // No ordering intent exists in the current editor. Keep latest order and
    // append new definitions; don't undo another window's collection additions.
}
void ValidateHistory(std::vector<HistoryEntry> const& history) {
    if (history.size() > 1000) throw std::runtime_error("Too many history entries");
    std::unordered_set<std::string> paths;
    for (auto const& entry : history) {
        if (entry.path.empty() || entry.path.find('\0') != std::string::npos ||
            !std::isfinite(entry.position) || entry.position < 0 ||
            entry.playedAt < 0 || entry.playedAt > 1000000000000LL ||
            (entry.duration && (!std::isfinite(*entry.duration) || *entry.duration < 0)) ||
            !paths.insert(entry.path).second)
            throw std::runtime_error("Invalid or duplicate history entry");
    }
}
}
UserData MergeUserData(UserData const& base, UserData const& edited, UserData latest) {
    if (!base.writable || !edited.writable || !latest.writable)
        throw std::runtime_error("Cannot merge unreadable user data");
    if (!ValidUserSettings(base.settings) || !ValidUserSettings(edited.settings) ||
        !ValidUserSettings(latest.settings)) throw std::runtime_error("Invalid settings range");
    ValidateHistory(base.history); ValidateHistory(edited.history); ValidateHistory(latest.history);
    auto const& a = base.settings;
    auto const& b = edited.settings;
    auto& c = latest.settings;
    auto field = [&]<typename T>(T UserSettings::* member) {
        if (a.*member != b.*member) c.*member = b.*member;
    };
    field(&UserSettings::inputConf);
    field(&UserSettings::watchProperties);
    field(&UserSettings::volume); field(&UserSettings::speed);
    field(&UserSettings::recordHistory); field(&UserSettings::resumePosition);
    field(&UserSettings::pauseWhenOpen); field(&UserSettings::autoPlayNext);
    field(&UserSettings::muted); field(&UserSettings::subtitlesVisible);
    field(&UserSettings::maxVolume); field(&UserSettings::audioDelay);
    field(&UserSettings::subtitleDelay); field(&UserSettings::repeatMode);
    field(&UserSettings::screenshotMode);
    // Named input snapshots reject conflicting edits instead of silently losing
    // another window's work. Active inputConf remains a separate existing field.
    for (auto const& [name, text] : a.bindingLibrary) {
        auto edited = b.bindingLibrary.find(name);
        if (edited != b.bindingLibrary.end() && edited->second == text) continue;
        auto latest = c.bindingLibrary.find(name);
        if (latest != c.bindingLibrary.end() && latest->second != text &&
            (edited == b.bindingLibrary.end() || latest->second != edited->second))
            throw std::runtime_error("Binding configuration changed in another window: " + name);
        if (edited == b.bindingLibrary.end()) c.bindingLibrary.erase(name);
    }
    for (auto const& [name, text] : b.bindingLibrary) {
        auto before = a.bindingLibrary.find(name);
        if (before != a.bindingLibrary.end() && before->second == text) continue;
        auto latest = c.bindingLibrary.find(name);
        if (before == a.bindingLibrary.end() && latest != c.bindingLibrary.end() && latest->second != text)
            throw std::runtime_error("Binding configuration name was created in another window: " + name);
        if (before != a.bindingLibrary.end() && latest == c.bindingLibrary.end())
            throw std::runtime_error("Binding configuration was deleted in another window: " + name);
        c.bindingLibrary.insert_or_assign(name, text);
    }
    if (!ValidBindingLibrary(c.bindingLibrary)) throw std::runtime_error("Merged binding configuration library exceeds limits");
    MergeMap(a.advancedPreferences, b.advancedPreferences, c.advancedPreferences);
    MergeMap(a.userEqualizers, b.userEqualizers, c.userEqualizers);
    MergeFilters(a.savedVideoFilters, b.savedVideoFilters, c.savedVideoFilters);
    MergeFilters(a.savedAudioFilters, b.savedAudioFilters, c.savedAudioFilters);
    // Concurrent changes to coupled fields may otherwise yield volume > max.
    // Retain the final maximum and cap persisted volume, as the playback setter does.
    c.volume = std::min(c.volume, c.maxVolume);
    if (!ValidUserSettings(c)) throw std::runtime_error("Merged settings are invalid");

    auto find = [](auto const& entries, std::string const& path) {
        return std::find_if(entries.begin(), entries.end(), [&](auto const& e) { return e.path == path; });
    };
    for (auto const& before : base.history) {
        if (find(edited.history, before.path) != edited.history.end()) continue;
        std::erase_if(latest.history, [&](auto const& entry) { return SameHistory(entry, before); });
    }
    for (auto const& entry : edited.history) {
        auto before = find(base.history, entry.path);
        if (before != base.history.end() && SameHistory(*before, entry)) continue;
        auto current = std::find_if(latest.history.begin(), latest.history.end(),
            [&](auto const& e) { return e.path == entry.path; });
        // A delayed save from an older playback cannot replace a newer record.
        if (current != latest.history.end() && current->playedAt > entry.playedAt) continue;
        if (current != latest.history.end()) latest.history.erase(current);
        latest.history.insert(latest.history.begin(), entry);
    }
    std::stable_sort(latest.history.begin(), latest.history.end(),
        [](auto const& left, auto const& right) { return left.playedAt > right.playedAt; });
    if (latest.history.size() > 1000) latest.history.resize(1000);
    return latest;
}
}

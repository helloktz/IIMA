// Adapted from IINA HistoryWindowController, HistoryController and PlaybackHistory.
// Copyright (c) 2017 lhc. GPL-3.0; references/iina/LICENSE.
#include "HistoryBrowser.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace iima {
namespace {
std::string FoldAscii(std::string_view text) {
    std::string folded(text);
    for (auto& ch : folded) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return folded;
}
bool DefaultContains(std::string_view text, std::string_view query) {
    return FoldAscii(text).find(FoldAscii(query)) != std::string::npos;
}
bool SameNumber(double a, double b) noexcept {
    // Corrupt/non-finite records can still be selected for removal. Do not let
    // NaN's non-reflexive equality make such records undeletable.
    return a == b || (std::isnan(a) && std::isnan(b));
}
bool SameOptional(const std::optional<double>& a, const std::optional<double>& b) noexcept {
    return a.has_value() == b.has_value() && (!a || SameNumber(*a, *b));
}
bool ValidRestorableEntry(const HistoryEntry& entry) {
    return !entry.path.empty() && entry.path.find('\0') == std::string::npos &&
        entry.path.find("://") == std::string::npos && std::isfinite(entry.position) &&
        entry.position >= 0 && entry.playedAt >= 0 &&
        (!entry.duration || (std::isfinite(*entry.duration) && *entry.duration >= 0));
}
std::string_view StripTrailingSeparators(std::string_view path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.remove_suffix(1);
    return path;
}
}
HistoryIdentity IdentifyHistoryEntry(const HistoryEntry& entry) {
    return {entry.path, entry.playedAt, entry.position, entry.duration};
}
bool MatchesHistoryIdentity(const HistoryEntry& entry, const HistoryIdentity& identity) noexcept {
    return entry.path == identity.path && entry.playedAt == identity.playedAt &&
        SameNumber(entry.position, identity.position) && SameOptional(entry.duration, identity.duration);
}
std::string HistoryFileName(std::string_view path) {
    path = StripTrailingSeparators(path);
    const auto split = path.find_last_of("/\\");
    return std::string(split == std::string_view::npos ? path : path.substr(split + 1));
}
std::string HistoryParentLocation(std::string_view path) {
    path = StripTrailingSeparators(path);
    const auto split = path.find_last_of("/\\");
    if (split == std::string_view::npos) return {};
    if (split == 0) return std::string(path.substr(0, 1));
    // Preserve the separator on a drive root instead of showing drive-relative C:.
    if (split == 2 && path.size() > 1 && path[1] == ':') return std::string(path.substr(0, 3));
    return std::string(path.substr(0, split));
}
const HistoryViewEntry* HistoryView::EntryAt(std::size_t row) const noexcept {
    if (row >= rows.size()) return nullptr;
    const auto& location = rows[row];
    if (location.groupIndex >= groups.size() || !location.entryIndex) return nullptr;
    const auto& entries = groups[location.groupIndex].entries;
    return *location.entryIndex < entries.size() ? &entries[*location.entryIndex] : nullptr;
}
HistoryView BuildHistoryView(std::span<const HistoryEntry> history, const HistoryQuery& query,
                             const HistoryPresentation& presentation) {
    HistoryView result;
    result.totalEntries = history.size();
    std::map<std::string, std::size_t> groupIndex;
    for (std::size_t index = 0; index < history.size(); ++index) {
        const auto& entry = history[index];
        auto fileName = HistoryFileName(entry.path);
        const auto searchText = query.searchScope == HistorySearchScope::FileName ?
            std::string_view(fileName) : std::string_view(entry.path);
        if (!query.search.empty()) {
            const bool matches = presentation.localizedContains ?
                presentation.localizedContains(searchText, query.search) : DefaultContains(searchText, query.search);
            if (!matches) continue;
        }
        std::string key;
        std::string label;
        if (query.grouping == HistoryGrouping::LastPlayed) {
            auto date = presentation.localDate ? presentation.localDate(entry.playedAt) : std::nullopt;
            if (date && !date->key.empty()) { key = "date:" + date->key; label = date->label; }
            else { key = "unknown-date"; label = presentation.unknownDateLabel; }
        } else {
            auto parent = HistoryParentLocation(entry.path);
            key = "location:" + parent;
            label = parent.empty() ? presentation.unknownLocationLabel : parent;
        }
        auto found = groupIndex.find(key);
        std::size_t group;
        if (found == groupIndex.end()) {
            group = result.groups.size();
            groupIndex.emplace(key, group);
            result.groups.push_back({std::move(key), std::move(label), {}, entry.playedAt});
        } else group = found->second;
        auto availability = presentation.availability ? presentation.availability(entry.path) : HistoryAvailability::Unknown;
        std::optional<double> progress;
        if (availability != HistoryAvailability::Missing && entry.duration &&
            std::isfinite(*entry.duration) && *entry.duration > 0 && std::isfinite(entry.position))
            progress = std::clamp(entry.position / *entry.duration, 0.0, 1.0);
        result.groups[group].entries.push_back({entry, IdentifyHistoryEntry(entry), index,
                                               std::move(fileName), availability, progress});
        result.groups[group].newestPlayedAt = std::max(result.groups[group].newestPlayedAt, entry.playedAt);
        ++result.matchedEntries;
    }
    for (auto& group : result.groups) {
        std::stable_sort(group.entries.begin(), group.entries.end(), [](const auto& a, const auto& b) {
            return a.entry.playedAt > b.entry.playedAt;
        });
    }
    std::stable_sort(result.groups.begin(), result.groups.end(), [&](const auto& a, const auto& b) {
        if (query.grouping == HistoryGrouping::LastPlayed) return a.newestPlayedAt > b.newestPlayedAt;
        return presentation.localizedLess ? presentation.localizedLess(a.label, b.label) : a.label < b.label;
    });
    for (std::size_t group = 0; group < result.groups.size(); ++group) {
        result.rows.push_back({group, std::nullopt});
        for (std::size_t entry = 0; entry < result.groups[group].entries.size(); ++entry)
            result.rows.push_back({group, entry});
    }
    return result;
}
std::vector<HistoryIdentity> SelectHistoryRows(const HistoryView& view,
    std::span<const std::size_t> selectedRows, std::optional<std::size_t> clickedRow) {
    std::set<std::size_t> rows(selectedRows.begin(), selectedRows.end());
    if (clickedRow) rows.insert(*clickedRow);
    std::set<std::string> paths;
    std::vector<HistoryIdentity> result;
    for (auto row : rows) if (auto entry = view.EntryAt(row); entry && paths.insert(entry->entry.path).second)
        result.push_back(entry->identity);
    return result;
}
std::vector<std::string> PlayableHistoryPaths(const HistoryView& view,
    std::span<const HistoryIdentity> selection) {
    std::set<std::string> seen;
    std::vector<std::string> result;
    for (const auto& identity : selection) {
        const HistoryViewEntry* selected = nullptr;
        for (const auto& group : view.groups) {
            const auto found = std::find_if(group.entries.begin(), group.entries.end(), [&](const auto& entry) {
                return MatchesHistoryIdentity(entry.entry, identity);
            });
            if (found != group.entries.end()) { selected = &*found; break; }
        }
        if (!selected || selected->availability == HistoryAvailability::Missing || selected->entry.path.empty()) continue;
        if (seen.insert(selected->entry.path).second) result.push_back(selected->entry.path);
    }
    return result;
}
HistoryRemoval RemoveHistoryEntries(std::vector<HistoryEntry>& history,
                                    std::span<const HistoryIdentity> selection) {
    HistoryRemoval result;
    std::vector<HistoryEntry> retained;
    retained.reserve(history.size());
    for (std::size_t index = 0; index < history.size(); ++index) {
        const auto& entry = history[index];
        bool selected = std::any_of(selection.begin(), selection.end(), [&](const auto& identity) {
            return MatchesHistoryIdentity(entry, identity);
        });
        if (selected) result.removed.push_back({entry, index});
        else retained.push_back(entry);
    }
    history = std::move(retained);
    return result;
}
HistoryRemoval ClearHistoryEntries(std::vector<HistoryEntry>& history) {
    HistoryRemoval result;
    result.removed.reserve(history.size());
    for (std::size_t index = 0; index < history.size(); ++index) result.removed.push_back({history[index], index});
    history.clear();
    return result;
}
HistoryRestoreResult RestoreHistoryEntries(std::vector<HistoryEntry>& history,
    const HistoryRemoval& removal, std::size_t maximumEntries) {
    HistoryRestoreResult result;
    // The persistent collection's cap is fixed; callers can request a smaller
    // budget, but cannot bypass it by passing a large number.
    maximumEntries = std::min<std::size_t>(maximumEntries, 1000);
    std::set<std::string> present;
    for (const auto& entry : history) present.insert(entry.path);
    auto candidates = removal.removed;
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.entry.playedAt > b.entry.playedAt;
    });
    for (const auto& candidate : candidates) {
        if (!ValidRestorableEntry(candidate.entry)) { ++result.invalid; continue; }
        if (present.contains(candidate.entry.path)) { ++result.alreadyPresent; continue; }
        if (history.size() >= maximumEntries) { ++result.capacitySkipped; continue; }
        history.push_back(candidate.entry);
        present.insert(candidate.entry.path);
        ++result.restored;
    }
    std::stable_sort(history.begin(), history.end(), [](const auto& a, const auto& b) {
        return a.playedAt > b.playedAt;
    });
    return result;
}
std::optional<double> HistoryResumePosition(const HistoryViewEntry& value, bool resumeEnabled) noexcept {
    if (value.availability == HistoryAvailability::Missing || value.entry.path.empty()) return std::nullopt;
    const auto& entry = value.entry;
    if (!resumeEnabled || !std::isfinite(entry.position) || entry.position <= 0) return 0;
    if (entry.duration && std::isfinite(*entry.duration) && entry.position >= *entry.duration) return 0;
    return entry.position;
}
} // namespace iima

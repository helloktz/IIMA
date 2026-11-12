// Adapted from IINA HistoryWindowController.prepareData/searchFieldAction,
// selectedEntries, and HistoryController.remove; PlaybackHistory presentation.
// Copyright (c) 2017 lhc. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "../Persistence/UserData.h"
#include <functional>
#include <span>
#include <string_view>

namespace iima {

enum class HistoryGrouping { LastPlayed, FileLocation };
enum class HistorySearchScope { FileName, FullPath };
enum class HistoryAvailability { Unknown, Available, Missing };
struct HistoryQuery {
    HistoryGrouping grouping = HistoryGrouping::LastPlayed;
    HistorySearchScope searchScope = HistorySearchScope::FullPath;
    std::string search;
};
struct HistoryLocalDate {
    // Stable date key, e.g. Gregorian YYYY-MM-DD in the user's local time zone.
    // The label may be localized. Equal labels need not imply equal keys.
    std::string key;
    std::string label;
};
struct HistoryPresentation {
    std::function<std::optional<HistoryLocalDate>(std::int64_t)> localDate;
    std::function<bool(std::string_view, std::string_view)> localizedContains;
    std::function<bool(std::string_view, std::string_view)> localizedLess;
    // Supply a precomputed availability map rather than doing disk/network I/O
    // in this callback on the UI thread. Unknown does not mean missing.
    std::function<HistoryAvailability(std::string_view)> availability;
    std::string unknownDateLabel = "Unknown date";
    std::string unknownLocationLabel = "Unknown location";
};
struct HistoryIdentity {
    // Full snapshot identity prevents a delayed confirmation deleting a newer
    // playback sample at the same path. No filesystem path canonicalization.
    std::string path;
    std::int64_t playedAt = 0;
    double position = 0;
    std::optional<double> duration;
};
HistoryIdentity IdentifyHistoryEntry(const HistoryEntry& entry);
bool MatchesHistoryIdentity(const HistoryEntry& entry, const HistoryIdentity& identity) noexcept;
std::string HistoryFileName(std::string_view path);
std::string HistoryParentLocation(std::string_view path);

struct HistoryViewEntry {
    HistoryEntry entry;
    HistoryIdentity identity;
    std::size_t sourceIndex = 0;
    std::string fileName;
    HistoryAvailability availability = HistoryAvailability::Unknown;
    std::optional<double> progress; // 0..1, absent for missing/unknown-duration media
};
struct HistoryGroup {
    std::string key;
    std::string label;
    std::vector<HistoryViewEntry> entries;
    std::int64_t newestPlayedAt = 0;
};
struct HistoryViewRow {
    std::size_t groupIndex = 0;
    // null means group heading; never resolve a heading to an actual media path.
    std::optional<std::size_t> entryIndex;
};
struct HistoryView {
    std::vector<HistoryGroup> groups;
    std::vector<HistoryViewRow> rows;
    std::size_t matchedEntries = 0;
    std::size_t totalEntries = 0;
    const HistoryViewEntry* EntryAt(std::size_t row) const noexcept;
};
// Pure projection: never changes history, queries disk, or opens media. Sorting
// defaults to deterministic ordinal order, with Windows-localized hooks above.
HistoryView BuildHistoryView(std::span<const HistoryEntry> history,
                             const HistoryQuery& query = {},
                             const HistoryPresentation& presentation = {});
// Selected rows plus optional context-clicked row, in visual order and deduped.
// Headings and out-of-range rows are ignored, matching the upstream outline view.
std::vector<HistoryIdentity> SelectHistoryRows(const HistoryView& view,
    std::span<const std::size_t> selectedRows, std::optional<std::size_t> clickedRow = {});
std::vector<std::string> PlayableHistoryPaths(const HistoryView& view,
    std::span<const HistoryIdentity> selection);

struct RemovedHistoryEntry {
    HistoryEntry entry;
    std::size_t originalIndex = 0;
};
struct HistoryRemoval {
    std::vector<RemovedHistoryEntry> removed;
};
// These mutate only the supplied history collection, never files or mpv's
// watch-later store. The UI owns confirmation and durable UserData persistence.
HistoryRemoval RemoveHistoryEntries(std::vector<HistoryEntry>& history,
                                    std::span<const HistoryIdentity> selection);
HistoryRemoval ClearHistoryEntries(std::vector<HistoryEntry>& history);
struct HistoryRestoreResult {
    std::size_t restored = 0;
    std::size_t alreadyPresent = 0;
    std::size_t invalid = 0;
    std::size_t capacitySkipped = 0;
};
// Undo affects absent paths only. It cannot overwrite a new playback record.
// Restoring obeys the existing 1000-record storage cap without evicting newer
// current records. Caller clears/discards its undo batch as appropriate.
HistoryRestoreResult RestoreHistoryEntries(std::vector<HistoryEntry>& history,
    const HistoryRemoval& removal, std::size_t maximumEntries = 1000);
// Resuming completed or invalid entries starts from beginning; missing media
// has no playable position. A resume request is not a file-existence guarantee.
std::optional<double> HistoryResumePosition(const HistoryViewEntry& entry,
                                          bool resumeEnabled) noexcept;

} // namespace iima

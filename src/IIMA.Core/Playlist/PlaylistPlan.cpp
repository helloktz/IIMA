// Derived from IINA PlayerCore.swift, MPVCommandWrappers.swift and
// SidebarPlaylistPane.swift. Copyright (c) 2016-2025 lhc and contributors.
// GPL-3.0; references/iina-source.txt and references/iina/LICENSE.
#include "PlaylistPlan.h"
#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace iima {
namespace {
using Queue = std::vector<PlaylistExpectation>;
Queue Capture(std::span<const PlaylistItem> items) {
    Queue queue;
    for (const auto& item : items) queue.push_back({item.id, item.path, std::nullopt});
    return queue;
}
PlaylistEditPlan Base(std::span<const PlaylistItem> items, std::uint64_t revision) {
    PlaylistEditPlan plan;
    plan.sourceRevision = revision;
    std::set<std::int64_t> ids;
    std::size_t current = 0;
    for (const auto& item : items) {
        if (item.id < 0 || !ids.insert(item.id).second) plan.error = "Playlist requires unique nonnegative mpv entry IDs";
        current += item.current;
    }
    if (current > 1) plan.error = "Playlist contains multiple current entries";
    // mpv playlist-move/remove use C int indices.
    if (items.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) plan.error = "Playlist exceeds mpv index range";
    return plan;
}
void Move(PlaylistEditPlan& plan, Queue& queue, std::size_t from, std::size_t finalIndex) {
    if (from == finalIndex) return;
    auto before = queue;
    auto item = std::move(queue[from]);
    queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(from));
    queue.insert(queue.begin() + static_cast<std::ptrdiff_t>(finalIndex), std::move(item));
    // mpv's destination is a position BEFORE removal, not the final index.
    const auto destination = finalIndex + (from < finalIndex ? 1 : 0);
    plan.steps.push_back({{"playlist-move", std::to_string(from), std::to_string(destination)}, std::move(before), queue});
}
std::optional<std::vector<std::size_t>> Selection(std::span<const std::size_t> indices, std::size_t size) {
    std::vector<std::size_t> selected(indices.begin(), indices.end());
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    if (!selected.empty() && selected.back() >= size) return std::nullopt;
    return selected;
}
std::string DisplayName(const PlaylistItem& item) {
    if (!item.title.empty()) return item.title;
    // URLs remain full URLs, as in MPVPlaylistItem.filenameForDisplay.
    if (item.path.find("://") != std::string::npos) return item.path;
    const auto slash = item.path.find_last_of("/\\");
    return slash == std::string::npos ? item.path : item.path.substr(slash + 1);
}
} // namespace

PlaylistEditPlan PlanPlaylistReorder(std::span<const PlaylistItem> items,
                                    std::span<const std::int64_t> desired,
                                    std::uint64_t revision) {
    auto plan = Base(items, revision);
    if (!plan) return plan;
    std::set<std::int64_t> existing, requested(desired.begin(), desired.end());
    for (const auto& item : items) existing.insert(item.id);
    if (desired.size() != items.size() || requested.size() != desired.size() || requested != existing) {
        plan.error = "Reorder must contain every existing entry ID exactly once";
        return plan;
    }
    auto queue = Capture(items);
    for (std::size_t target = 0; target < desired.size(); ++target) {
        const auto found = std::find_if(queue.begin() + static_cast<std::ptrdiff_t>(target), queue.end(),
                                        [&](const auto& entry) { return entry.id == desired[target]; });
        Move(plan, queue, static_cast<std::size_t>(found - queue.begin()), target);
    }
    // Unlike IINA's clear-and-reappend, moves preserve every mpv entry ID and
    // the currently playing entry, even when the same path occurs twice.
    return plan;
}

PlaylistEditPlan PlanPlaylistSort(std::span<const PlaylistItem> items, PlaylistSort order, std::uint64_t revision) {
    std::vector<const PlaylistItem*> sorted;
    for (const auto& item : items) sorted.push_back(&item);
    const bool path = order == PlaylistSort::PathAscending || order == PlaylistSort::PathDescending;
    const bool descending = order == PlaylistSort::PathDescending || order == PlaylistSort::DisplayNameDescending;
    std::stable_sort(sorted.begin(), sorted.end(), [&](const auto* left, const auto* right) {
        const auto a = path ? left->path : DisplayName(*left);
        const auto b = path ? right->path : DisplayName(*right);
        // Correct strict comparator: upstream descending !less also returned
        // true for equal names. UTF-8 lexical order is deterministic, not a
        // promise of Windows Explorer localized collation.
        return descending ? b < a : a < b;
    });
    std::vector<std::int64_t> ids;
    for (const auto* item : sorted) ids.push_back(item->id);
    return PlanPlaylistReorder(items, ids, revision);
}

PlaylistEditPlan PlanPlaylistInsert(std::span<const PlaylistItem> items,
                                   std::span<const std::string> paths,
                                   std::optional<std::size_t> at, std::uint64_t revision) {
    auto plan = Base(items, revision);
    if (!plan) return plan;
    if (at && *at > items.size()) {
        plan.error = "Insertion position is outside the current playlist";
        return plan;
    }
    if (paths.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) - items.size()) {
        plan.error = "Insertion exceeds mpv index range";
        return plan;
    }
    for (const auto& path : paths) {
        if (path.empty() || path.find('\0') != std::string::npos) {
            plan.error = "Playlist path is empty or contains NUL";
            return plan;
        }
    }
    auto queue = Capture(items);
    std::size_t nextToken = 0;
    for (const auto& path : paths) {
        auto before = queue;
        queue.push_back({std::nullopt, path, nextToken++});
        plan.steps.push_back({{"loadfile", path, "append"}, std::move(before), queue});
    }
    if (at) {
        for (std::size_t index = 0; index < paths.size(); ++index) Move(plan, queue, items.size() + index, *at + index);
    }
    return plan;
}

PlaylistEditPlan PlanPlaylistRemove(std::span<const PlaylistItem> items,
                                   std::span<const std::size_t> indices, std::uint64_t revision) {
    auto plan = Base(items, revision);
    if (!plan) return plan;
    const auto selected = Selection(indices, items.size());
    if (!selected) { plan.error = "Removal index is outside the current playlist"; return plan; }
    auto queue = Capture(items);
    std::size_t removed = 0;
    for (const auto original : *selected) {
        const auto index = original - removed++;
        auto before = queue;
        queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(index));
        plan.steps.push_back({{"playlist-remove", std::to_string(index)}, std::move(before), queue});
        plan.mayChangePlayback |= items[original].current;
    }
    return plan;
}

PlaylistEditPlan PlanPlaylistDrop(std::span<const PlaylistItem> items,
                                 std::span<const std::size_t> indices,
                                 std::size_t beforeRow, std::uint64_t revision) {
    auto plan = Base(items, revision);
    if (!plan) return plan;
    const auto selected = Selection(indices, items.size());
    if (!selected || beforeRow > items.size()) {
        plan.error = "Drag source or destination is outside the current playlist";
        return plan;
    }
    std::vector<std::int64_t> desired;
    std::vector<std::int64_t> moving;
    std::size_t insertion = 0;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (std::binary_search(selected->begin(), selected->end(), index)) moving.push_back(items[index].id);
        else {
            if (index < beforeRow) ++insertion;
            desired.push_back(items[index].id);
        }
    }
    desired.insert(desired.begin() + static_cast<std::ptrdiff_t>(insertion), moving.begin(), moving.end());
    return PlanPlaylistReorder(items, desired, revision);
}

PlaylistEditPlan PlanPlaylistPlayNext(std::span<const PlaylistItem> items,
                                     std::span<const std::size_t> indices, std::uint64_t revision) {
    auto plan = Base(items, revision);
    if (!plan) return plan;
    const auto selected = Selection(indices, items.size());
    const auto current = std::find_if(items.begin(), items.end(), [](const auto& item) { return item.current; });
    if (!selected || current == items.end()) {
        plan.error = "Play-next requires a current entry and valid selection";
        return plan;
    }
    std::vector<std::int64_t> desired, moving;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (!items[index].current && std::binary_search(selected->begin(), selected->end(), index)) moving.push_back(items[index].id);
        else desired.push_back(items[index].id);
    }
    const auto anchor = std::find(desired.begin(), desired.end(), current->id);
    desired.insert(anchor + 1, moving.begin(), moving.end());
    return PlanPlaylistReorder(items, desired, revision);
}

PlaylistEditPlan PlanPlaylistClear(std::span<const PlaylistItem> items, std::uint64_t revision) {
    auto plan = Base(items, revision);
    if (!plan) return plan;
    // mpv distinguishes currently played from merely current. The input
    // projection has only current, so do not invent a guaranteed post-state.
    plan.steps.push_back({{"playlist-clear"}, Capture(items), std::nullopt});
    return plan;
}

PlaylistEditPlan PlanPlaylistShuffle(std::span<const PlaylistItem> items, bool shuffled, std::uint64_t revision) {
    auto plan = Base(items, revision);
    if (!plan) return plan;
    plan.clearShuffleState = false;
    plan.steps.push_back({{shuffled ? "playlist-unshuffle" : "playlist-shuffle"}, Capture(items), std::nullopt});
    // UI should flip its shuffle indicator only after successful completion.
    // mpv unshuffle is best-effort, not a deterministic inverse after edits.
    return plan;
}

bool MatchesPlaylistExpectation(std::span<const PlaylistItem> observed, std::span<const PlaylistExpectation> expected) {
    if (observed.size() != expected.size()) return false;
    std::set<std::int64_t> ids;
    for (const auto& item : observed) {
        if (item.id < 0 || !ids.insert(item.id).second) return false;
    }
    for (std::size_t index = 0; index < observed.size(); ++index) {
        if (expected[index].id && observed[index].id != *expected[index].id) return false;
        if (observed[index].path != expected[index].path) return false;
    }
    return true;
}

PlaylistPlanCursor::PlaylistPlanCursor(PlaylistEditPlan plan) : plan_(std::move(plan)) {}
const PlaylistEditStep* PlaylistPlanCursor::Next(std::span<const PlaylistItem> observed, std::uint64_t revision) {
    prepared_ = false;
    if (Finished() || pending_) return nullptr;
    if (next_ == 0 && revision != plan_.sourceRevision) {
        plan_.error = "Playlist changed since this edit was planned";
        return nullptr;
    }
    if (!MatchesPlaylistExpectation(observed, plan_.steps[next_].before)) {
        plan_.error = "Playlist diverged before the next edit command";
        return nullptr;
    }
    prepared_ = true;
    return &plan_.steps[next_];
}
bool PlaylistPlanCursor::Begin(std::uint64_t request) {
    if (Finished() || pending_ || !prepared_) return false;
    pending_ = request;
    prepared_ = false;
    return true;
}
bool PlaylistPlanCursor::Complete(std::uint64_t request, int error, std::span<const PlaylistItem> refreshed) {
    if (!pending_ || *pending_ != request || cancelled_) return false;
    pending_.reset();
    if (error < 0) {
        plan_.error = "Playlist edit failed: mpv error " + std::to_string(error);
        return false;
    }
    const auto& after = plan_.steps[next_].after;
    if (after && !MatchesPlaylistExpectation(refreshed, *after)) {
        plan_.error = "Playlist changed unexpectedly after edit; remaining commands cancelled";
        return false;
    }
    // Bind appended placeholders to the actual new mpv IDs for all following
    // expectations, so later same-path external replacements are detectable.
    if (after) {
        for (std::size_t index = 0; index < after->size(); ++index) {
            if ((*after)[index].id) continue;
            const auto token = (*after)[index].newEntryToken;
            if (!token) continue;
            for (std::size_t step = next_ + 1; step < plan_.steps.size(); ++step) {
                auto bind = [&](Queue& queue) {
                    for (auto& entry : queue) {
                        if (!entry.id && entry.newEntryToken == token) {
                            entry.id = refreshed[index].id;
                        }
                    }
                };
                bind(plan_.steps[step].before);
                if (plan_.steps[step].after) bind(*plan_.steps[step].after);
            }
        }
    }
    ++next_;
    return true;
}
void PlaylistPlanCursor::Cancel() { cancelled_ = true; prepared_ = false; pending_.reset(); }
bool PlaylistPlanCursor::Finished() const noexcept { return cancelled_ || !plan_.error.empty() || next_ == plan_.steps.size(); }
bool PlaylistPlanCursor::Waiting() const noexcept { return pending_.has_value(); }
const std::string& PlaylistPlanCursor::Error() const noexcept { return plan_.error; }
} // namespace iima

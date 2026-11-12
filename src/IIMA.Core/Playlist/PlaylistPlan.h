// Derived from IINA PlayerCore.swift, MPVCommandWrappers.swift and
// SidebarPlaylistPane.swift. Copyright (c) 2016-2025 lhc and contributors.
// GPL-3.0; references/iina-source.txt and references/iina/LICENSE.
#pragma once
#include "../Mpv/MpvClient.h"
#include <span>

namespace iima {
struct PlaylistExpectation {
    std::optional<std::int64_t> id; // unknown only for a newly appended entry
    std::string path;
    std::optional<std::size_t> newEntryToken; // plan-local identity before mpv assigns ID
};
struct PlaylistEditStep {
    std::vector<std::string> arguments;
    std::vector<PlaylistExpectation> before;
    std::optional<std::vector<PlaylistExpectation>> after;
};
struct PlaylistEditPlan {
    std::uint64_t sourceRevision = 0;
    std::vector<PlaylistEditStep> steps;
    std::string error;
    // Mutation invalidates the application's previous shuffle indicator.
    bool clearShuffleState = true;
    // Removing the currently selected entry can change playback. The caller
    // must route subsequent lifecycle events through PlayerSession normally.
    bool mayChangePlayback = false;
    explicit operator bool() const noexcept { return error.empty(); }
};

enum class PlaylistSort { DisplayNameAscending, DisplayNameDescending, PathAscending, PathDescending };
PlaylistEditPlan PlanPlaylistReorder(std::span<const PlaylistItem> current,
                                    std::span<const std::int64_t> desiredIds,
                                    std::uint64_t revision);
PlaylistEditPlan PlanPlaylistSort(std::span<const PlaylistItem> current,
                                 PlaylistSort order, std::uint64_t revision);
PlaylistEditPlan PlanPlaylistInsert(std::span<const PlaylistItem> current,
                                   std::span<const std::string> paths,
                                   std::optional<std::size_t> at,
                                   std::uint64_t revision);
// IINA AutoFileMatcher.addFilesToPlaylist: retain the currently playing entry
// between naturally ordered earlier/later siblings. One guarded cursor plan.
PlaylistEditPlan PlanPlaylistAutoExpand(std::span<const PlaylistItem> current,
                                       std::span<const std::string> before,
                                       std::span<const std::string> after,
                                       std::uint64_t revision);
PlaylistEditPlan PlanPlaylistRemove(std::span<const PlaylistItem> current,
                                   std::span<const std::size_t> indices,
                                   std::uint64_t revision);
PlaylistEditPlan PlanPlaylistDrop(std::span<const PlaylistItem> current,
                                 std::span<const std::size_t> indices,
                                 std::size_t beforeRow, std::uint64_t revision);
PlaylistEditPlan PlanPlaylistPlayNext(std::span<const PlaylistItem> current,
                                     std::span<const std::size_t> indices,
                                     std::uint64_t revision);
PlaylistEditPlan PlanPlaylistClear(std::span<const PlaylistItem> current,
                                  std::uint64_t revision);
PlaylistEditPlan PlanPlaylistShuffle(std::span<const PlaylistItem> current,
                                    bool shuffled, std::uint64_t revision);

bool MatchesPlaylistExpectation(std::span<const PlaylistItem> observed,
                                std::span<const PlaylistExpectation> expected);

// Per-UI-thread transaction guard. This is NOT an mpv executor: issue one
// command, await its matching reply, then fetch a fresh playlist property.
// Never enqueue the complete plan blindly. Changed indices, recursive playlist
// expansion, failed replies or concurrent external edits abort remaining steps.
class PlaylistPlanCursor final {
public:
    explicit PlaylistPlanCursor(PlaylistEditPlan plan);
    const PlaylistEditStep* Next(std::span<const PlaylistItem> observed,
                                 std::uint64_t initialRevision);
    bool Begin(std::uint64_t commandRequest);
    bool Complete(std::uint64_t commandRequest, int commandError,
                  std::span<const PlaylistItem> refreshed);
    void Cancel();
    bool Finished() const noexcept;
    bool Waiting() const noexcept;
    const std::string& Error() const noexcept;
private:
    PlaylistEditPlan plan_;
    std::size_t next_ = 0;
    std::optional<std::uint64_t> pending_;
    bool cancelled_ = false;
    bool prepared_ = false;
};
} // namespace iima

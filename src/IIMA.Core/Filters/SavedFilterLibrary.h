// Derived from IINA FilterWindowController.swift, SavedFilter.swift and
// MenuController.updateSavedFilters, copyright (c) 2016-2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include "SavedFilter.h"
#include "FilterChain.h"
#include <utility>

namespace iima {
struct SavedFilterLibraryResult {
    bool success{};
    std::size_t index{};
    std::string error;
};
struct SavedFilterApplyPlan {
    enum class Kind { None, Add, EditChain };
    Kind kind{Kind::None};
    bool audio{};
    std::string filterString;
    FilterEditPlan edit;
    std::string error;
};
enum class SavedFilterObservedState { Unavailable, Absent, Disabled, Enabled };
class SavedFilterLibrary final {
public:
    // Each category has its own order and unique names. Restoring reports every
    // rejected entry, never assumes invalid persisted strings are executable.
    // Stored enabled flags are ignored: the observed mpv list is authoritative.
    std::vector<std::string> Restore(std::vector<SavedFilter> video,
                                     std::vector<SavedFilter> audio);
    std::vector<SavedFilter> const& Values(bool audio) const noexcept;
    SavedFilterLibraryResult Add(bool audio, SavedFilter value);
    SavedFilterLibraryResult Edit(bool audio, std::size_t index, SavedFilter value);
    SavedFilterLibraryResult Copy(bool audio, std::size_t index, std::string newName);
    SavedFilterLibraryResult Move(bool audio, std::size_t index, std::size_t destination);
    SavedFilterLibraryResult Remove(bool audio, std::size_t index);
    std::uint64_t Revision() const noexcept { return revision_; }

    static std::optional<std::string> ShortcutKey(SavedFilter const&);
    // Exact canonical key collisions are rejected across audio and video;
    // callers must also arbitrate conflicts with the input.conf binding set.
    std::optional<std::pair<bool, std::size_t>> FindShortcut(std::string_view key) const;
    SavedFilterObservedState ObservedState(bool audio, std::size_t index,
                                           FilterChainSnapshot const&) const;
    // Like IINA, remove the first matching active filter by index rather than
    // ambiguous parameter-string removal. A disabled matching row is enabled
    // in place instead of adding a duplicate. No optimistic state mutation.
    SavedFilterApplyPlan PlanToggle(bool audio, std::size_t index,
                                   FilterChainSnapshot const&) const;
private:
    SavedFilterLibraryResult Validate(bool audio, SavedFilter&,
                                     std::optional<std::size_t> replacing) const;
    std::vector<SavedFilter> video_, audio_;
    std::uint64_t revision_{};
};
}

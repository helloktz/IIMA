// IINA PlayerCore.active/lastActive/newPlayerCore and AppDelegate.menuNewWindow.
// Copyright (c) 2016 lhc and IINA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace iima {
// Non-owning, UI-thread-only policy. The application owns the actual windows,
// their players, and asynchronous destruction. No callbacks run from this model.
// All registry calls and window creation must be serialized on the same UI thread.
// Deliberate Windows differences: no immortal `first` Cocoa player, no implicit
// creation in an accessor, and at most 16 live/closing native windows. The app
// decides whether last-window closure terminates the process. This policy does
// not implement openURLs URL equality/deduplication, split-vs-grouped opens, or
// persist alwaysOpenInNewWindow; callers must implement those explicitly rather
// than assuming this registry alone provides IINA's entire multiwindow behavior.
class PlayerWindowRegistry final {
public:
    using WindowId = std::uint64_t;
    static constexpr std::size_t WindowLimit = 16;
    enum class OpenPolicy {
        PreferActive,       // Reuse the last activated eligible window, else create.
        ReuseIdleOrCreate,  // IINA newPlayerCore: first idle, non-busy player, else new.
        AlwaysCreate        // Explicit independent window, even if one is idle.
    };
    enum class SelectionKind { Existing, Create, AtCapacity, ShuttingDown, IdsExhausted };
    struct Selection {
        SelectionKind kind{SelectionKind::AtCapacity};
        std::optional<WindowId> id;
    };
    struct Entry {
        WindowId id{};
        bool idle{true};
        bool backgroundBusy{};
        bool closing{};
    };

    // Register before wiring callbacks which may query policy. A failed native
    // creation must Remove this ID; the consumed ID is intentionally not reused.
    // Reaching the cap or process shutdown returns nullopt without mutation.
    std::optional<WindowId> Register(bool idle = true);
    bool Remove(WindowId id) noexcept;
    // Mark only after close has actually been accepted, not while a save dialog
    // can still cancel it. Closing is irreversible here: a cancelled dialog must
    // leave its entry eligible. Remove is reserved for completed native closure.
    bool MarkClosing(WindowId id) noexcept;
    bool Activate(WindowId id);
    bool SetIdle(WindowId id, bool idle) noexcept;
    bool SetBackgroundBusy(WindowId id, bool busy) noexcept;
    bool Contains(WindowId id) const noexcept;
    bool IsClosing(WindowId id) const noexcept;

    // Retains last activated window after OS deactivation; closing/removed entries
    // are skipped. Before first activation, choose the earliest eligible creation.
    std::optional<WindowId> ActiveTarget() const noexcept;
    Selection Select(OpenPolicy policy = OpenPolicy::PreferActive) const noexcept;
    // Permanent process termination gate, analogous to AppDelegate.isTerminating.
    // Do not call merely because one of several windows begins closing.
    void BeginShutdown() noexcept { shuttingDown_ = true; }
    bool ShuttingDown() const noexcept { return shuttingDown_; }
    std::size_t Count() const noexcept { return entries_.size(); }
    std::size_t EligibleCount() const noexcept;
    // View invalidated by Register/Remove. Do not retain it across UI callbacks.
    std::span<Entry const> Entries() const noexcept { return entries_; }

private:
    Entry* Find(WindowId id) noexcept;
    Entry const* Find(WindowId id) const noexcept;
    Selection NewSelection() const noexcept;
    std::vector<Entry> entries_; // Creation order, like IINA playerCores.
    std::vector<WindowId> activationOrder_; // Least to most recently activated.
    WindowId nextId_{1}; // Zero is never a valid ID; zero also marks exhaustion.
    bool shuttingDown_{};
};
}

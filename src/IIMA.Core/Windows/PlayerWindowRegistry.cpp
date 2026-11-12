// Windows adaptation of references/iina/iina/PlayerCore.swift multiple instances.
// Copyright (c) 2016 lhc and IINA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerWindowRegistry.h"
#include <algorithm>
#include <limits>

namespace iima {
PlayerWindowRegistry::Entry* PlayerWindowRegistry::Find(WindowId id) noexcept
{
    auto found = std::find_if(entries_.begin(), entries_.end(),
        [id](Entry const& entry) { return entry.id == id; });
    return found == entries_.end() ? nullptr : &*found;
}
PlayerWindowRegistry::Entry const* PlayerWindowRegistry::Find(WindowId id) const noexcept
{
    auto found = std::find_if(entries_.begin(), entries_.end(),
        [id](Entry const& entry) { return entry.id == id; });
    return found == entries_.end() ? nullptr : &*found;
}
std::optional<PlayerWindowRegistry::WindowId> PlayerWindowRegistry::Register(bool idle)
{
    if (shuttingDown_ || entries_.size() >= WindowLimit || nextId_ == 0) return std::nullopt;
    // Reserve both before committing registration. Activate then cannot allocate
    // or leave the ordering incomplete after the window has already gained focus.
    entries_.reserve(WindowLimit);
    activationOrder_.reserve(WindowLimit);
    const auto id = nextId_;
    entries_.push_back({id, idle, false, false});
    nextId_ = id == std::numeric_limits<WindowId>::max() ? 0 : id + 1;
    return id;
}
bool PlayerWindowRegistry::Remove(WindowId id) noexcept
{
    const auto oldSize = entries_.size();
    std::erase_if(entries_, [id](Entry const& entry) { return entry.id == id; });
    std::erase(activationOrder_, id);
    return entries_.size() != oldSize;
}
bool PlayerWindowRegistry::MarkClosing(WindowId id) noexcept
{
    auto entry = Find(id);
    if (!entry || entry->closing) return false;
    entry->closing = true;
    std::erase(activationOrder_, id);
    return true;
}
bool PlayerWindowRegistry::Activate(WindowId id)
{
    auto entry = Find(id);
    if (shuttingDown_ || !entry || entry->closing) return false;
    std::erase(activationOrder_, id);
    activationOrder_.push_back(id);
    return true;
}
bool PlayerWindowRegistry::SetIdle(WindowId id, bool idle) noexcept
{
    auto entry = Find(id);
    if (!entry || entry->closing) return false;
    entry->idle = idle;
    return true;
}
bool PlayerWindowRegistry::SetBackgroundBusy(WindowId id, bool busy) noexcept
{
    auto entry = Find(id);
    if (!entry || entry->closing) return false;
    entry->backgroundBusy = busy;
    return true;
}
bool PlayerWindowRegistry::Contains(WindowId id) const noexcept { return Find(id) != nullptr; }
bool PlayerWindowRegistry::IsClosing(WindowId id) const noexcept
{
    auto entry = Find(id);
    return entry && entry->closing;
}
std::size_t PlayerWindowRegistry::EligibleCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(),
        [](Entry const& entry) { return !entry.closing; }));
}
std::optional<PlayerWindowRegistry::WindowId> PlayerWindowRegistry::ActiveTarget() const noexcept
{
    if (shuttingDown_) return std::nullopt;
    for (auto it = activationOrder_.rbegin(); it != activationOrder_.rend(); ++it) {
        auto entry = Find(*it);
        if (entry && !entry->closing) return entry->id;
    }
    for (auto const& entry : entries_) if (!entry.closing) return entry.id;
    return std::nullopt;
}
PlayerWindowRegistry::Selection PlayerWindowRegistry::NewSelection() const noexcept
{
    if (shuttingDown_) return {SelectionKind::ShuttingDown, std::nullopt};
    // Closing players still own GPU/mpv resources, so they consume capacity until
    // the owning App observes completed closure and calls Remove.
    if (entries_.size() >= WindowLimit) return {SelectionKind::AtCapacity, std::nullopt};
    if (nextId_ == 0) return {SelectionKind::IdsExhausted, std::nullopt};
    return {SelectionKind::Create, std::nullopt};
}
PlayerWindowRegistry::Selection PlayerWindowRegistry::Select(OpenPolicy policy) const noexcept
{
    if (shuttingDown_) return {SelectionKind::ShuttingDown, std::nullopt};
    if (policy == OpenPolicy::PreferActive) {
        if (auto target = ActiveTarget()) return {SelectionKind::Existing, target};
    } else if (policy == OpenPolicy::ReuseIdleOrCreate) {
        for (auto const& entry : entries_)
            if (!entry.closing && entry.idle && !entry.backgroundBusy)
                return {SelectionKind::Existing, entry.id};
    }
    return NewSelection();
}
}

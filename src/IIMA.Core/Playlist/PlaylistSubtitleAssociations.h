// IINA SidebarPlaylistPane subtitle associations / PlayerCore.fileStarted.
// Copyright (c) 2016-2025 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "../Mpv/MpvClient.h"
#include <span>
namespace iima {
// Per-player, nonpersistent associations. Real mpv IDs distinguish duplicate
// paths and reinsertions. An empty vector is an explicit auto-match override.
class PlaylistSubtitleAssociations {
public:
    std::vector<std::string> const* Find(std::int64_t id, std::string const& path) const noexcept;
    bool Set(std::int64_t id, std::string path, std::vector<std::string> subtitles);
    void Reconcile(std::span<const PlaylistItem> queue);
    void Clear() noexcept { entries_.clear(); }
private:
    struct Entry { std::int64_t id; std::string path; std::vector<std::string> subtitles; };
    std::vector<Entry> entries_;
};
}

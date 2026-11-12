// IINA SidebarPlaylistPane subtitle associations / PlayerCore.fileStarted.
// Copyright (c) 2016-2025 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "PlaylistSubtitleAssociations.h"
#include <algorithm>
#include <utility>
namespace iima {
std::vector<std::string> const* PlaylistSubtitleAssociations::Find(std::int64_t id,std::string const& path) const noexcept {
    auto found=std::find_if(entries_.begin(),entries_.end(),[&](auto const& e) { return e.id==id && e.path==path; });
    return found==entries_.end() ? nullptr : &found->subtitles;
}
bool PlaylistSubtitleAssociations::Set(std::int64_t id,std::string path,std::vector<std::string> subtitles) {
    if (id<0 || path.empty() || path.find('\0')!=path.npos || subtitles.size()>64) return false;
    std::vector<std::string> unique;
    for (auto& sub:subtitles) {
        if (sub.empty() || sub.find('\0')!=sub.npos) return false;
        if (std::find(unique.begin(),unique.end(),sub)==unique.end()) unique.push_back(std::move(sub));
    }
    auto found=std::find_if(entries_.begin(),entries_.end(),[&](auto const& e) { return e.id==id; });
    if (found==entries_.end() && entries_.size()>=256) return false;
    std::size_t references=unique.size(), bytes{};
    auto add=[&](std::string const& value) {
        if (value.size()>4*1024*1024-bytes) return false;
        bytes+=value.size(); return true;
    };
    if (!add(path)) return false;
    for (auto const& sub:unique) if (!add(sub)) return false;
    for (auto const& entry:entries_) {
        if (entry.id==id) continue;
        references+=entry.subtitles.size();
        if (references>4096 || !add(entry.path)) return false;
        for (auto const& sub:entry.subtitles) if (!add(sub)) return false;
    }
    if (found==entries_.end()) entries_.push_back({id,std::move(path),std::move(unique)});
    else *found={id,std::move(path),std::move(unique)};
    return true;
}
void PlaylistSubtitleAssociations::Reconcile(std::span<const PlaylistItem> queue) {
    std::erase_if(entries_,[&](auto const& entry) {
        return std::none_of(queue.begin(),queue.end(),[&](auto const& item) { return item.id==entry.id && item.path==entry.path; });
    });
}
}

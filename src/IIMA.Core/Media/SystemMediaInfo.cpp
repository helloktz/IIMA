// Adapted from IINA NowPlayingInfoManager.updateInfo and PlayerCore.getMusicMetadata.
// Copyright (c) 2025 lhc and IINA contributors. GPL-3.0, references/iina/LICENSE.
#include "SystemMediaInfo.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

namespace {
std::optional<std::int64_t> Ticks(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds < 0) return {};
    const auto ticks = static_cast<long double>(seconds) * 10000000.0L;
    if (ticks >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())) return {};
    return static_cast<std::int64_t>(ticks);
}
std::string MetadataText(std::string_view text) {
    // These are labels for Windows' shared media surface, not a path channel.
    // Refuse URLs and path-shaped metadata rather than disclosing credentials,
    // queries, home directories or UNC servers masquerading as a title.
    if (text.find("://") != std::string_view::npos || text.find('\\') != std::string_view::npos ||
        text.starts_with('/') || (text.size() >= 2 && text[1] == ':')) return {};
    std::string result;
    result.reserve(std::min<std::size_t>(text.size(), 512));
    for (const unsigned char c : text) {
        if (c < 32 || c == 127) result += ' ';
        else result += static_cast<char>(c);
    }
    if (result.size() > 512) {
        std::size_t end = 512;
        while (end && (static_cast<unsigned char>(result[end]) & 0xc0) == 0x80) --end;
        result.resize(end);
    }
    const auto first = result.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    result.erase(0, first);
    result.resize(result.find_last_not_of(' ') + 1);
    return result;
}
std::string FileTitle(std::string_view path) {
    if (path.find("://") != std::string_view::npos) return "IIMA";
    const auto slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
    auto title = MetadataText(path);
    return title.empty() ? "IIMA" : title;
}
}
namespace iima {
SystemMediaInfo MakeSystemMediaInfo(const PlaybackSnapshot& snapshot) {
    SystemMediaInfo result;
    result.active = snapshot.session == SessionState::Ready && snapshot.media == MediaState::Loaded;
    if (!result.active) return result;
    result.paused = snapshot.paused;
    result.video = std::any_of(snapshot.tracks.begin(), snapshot.tracks.end(), [](auto const& track) {
        return track.type == "video" && track.selected && !track.albumArt;
    });
    // Chapter title is IINA's preferred music title, notably for cue sheets.
    if (!result.video && snapshot.chapterIndex) {
        const auto chapter = std::find_if(snapshot.chapters.begin(), snapshot.chapters.end(), [&](auto const& item) {
            return item.index == *snapshot.chapterIndex;
        });
        if (chapter != snapshot.chapters.end()) result.title = MetadataText(chapter->title);
    }
    if (result.title.empty()) result.title = MetadataText(snapshot.mediaTitle);
    if (result.title.empty()) result.title = MetadataText(snapshot.metadataTitle);
    if (result.title.empty()) result.title = FileTitle(snapshot.path);
    if (!result.video) {
        result.album = MetadataText(snapshot.metadataAlbum);
        result.artist = MetadataText(snapshot.metadataArtist);
    }
    if (snapshot.duration) {
        if (auto duration = Ticks(*snapshot.duration); duration && *duration > 0) {
            result.durationTicks = *duration;
            result.canSeek = snapshot.seekable;
            if (snapshot.position) {
                if (auto position = Ticks(*snapshot.position))
                    result.positionTicks = std::min(*position, result.durationTicks);
            }
        }
    }
    if (std::isfinite(snapshot.speed) && snapshot.speed > 0) result.rate = snapshot.speed;
    return result;
}
}

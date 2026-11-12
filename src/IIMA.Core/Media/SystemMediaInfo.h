// Adapted from IINA NowPlayingInfoManager.updateInfo and PlayerCore.getMusicMetadata.
// Copyright (c) 2025 lhc and IINA contributors. GPL-3.0, references/iina/LICENSE.
#pragma once
#include "../Playback/PlaybackState.h"
#include <cstdint>
#include <string>

namespace iima {
struct SystemMediaInfo {
    bool active{};
    bool video{};
    bool paused{true};
    bool canSeek{};
    std::string title;
    std::string album;
    std::string artist;
    // Windows TimeSpan uses signed 100ns ticks. Unknown/live duration gives a
    // zero-length range, never a fabricated seekable timeline.
    std::int64_t positionTicks{};
    std::int64_t durationTicks{};
    double rate{1};
};
SystemMediaInfo MakeSystemMediaInfo(const PlaybackSnapshot& snapshot);
}

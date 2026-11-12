// Derived from IINA JavascriptAPICore.StatusAPI and EventController.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "../../IIMA.Core/Playback/PlaybackState.h"
#include <string>
#include <vector>
namespace iima::plugins {
struct PlaybackEvent { std::string name; std::string argumentJson{"null"}; };
// A copied view, never a native pointer or mutable mpv node. Playlist filenames
// and titles require explicit startup disclosure, including network URL secrets. Flat
// status fields plus audio/subtitle/video objects and chapters array. Throws for
// over 256 tracks, 2048 chapters, 1024 playlist entries, 4096-byte text or 64-KiB snapshot;
// does not silently truncate collections. Secondary selected subtitle ID is not
// observed by the current payload and remains null.
std::string PlaybackStatusJson(PlaybackSnapshot const& state, std::uint64_t mediaToken = 0);
class PlaybackProjection {
public:
    void Reset(PlaybackSnapshot const& state, bool music);
    std::vector<PlaybackEvent> Update(PlaybackSnapshot const& state, bool music);
private:
    MediaState media_{MediaState::Empty};
    std::int64_t entry_{-1};
    bool paused_{true}, music_{}, muted_{};
    double speed_{1}, volume_{100}, audioDelay_{}, subtitleDelay_{};
    std::optional<double> position_, duration_;
};
}

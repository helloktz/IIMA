#pragma once
#include "../Mpv/MpvClient.h"

namespace iima {
enum class SessionState { Initializing, Ready, Closing, Closed, Failed };
enum class MediaState { Empty, Loading, Loaded, Ended, Error };
struct PlaybackSnapshot {
    SessionState session{SessionState::Initializing};
    MediaState media{MediaState::Empty};
    bool paused{true}, seeking{}, buffering{};
    std::optional<double> position, duration;
    double volume{100}, speed{1};
    std::int64_t entryId{-1};
    std::string path, error;
};
// Ordered event reduction. Command acceptance never asserts successful playback.
void ApplyPlaybackEvent(PlaybackSnapshot& state, MpvEvent const& event);
}

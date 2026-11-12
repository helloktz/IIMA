// Derived from IINA JavascriptAPICore.StatusAPI and EventController.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "../../IIMA.Core/Playback/PlaybackState.h"
#include <string>
#include <vector>
namespace iima::plugins {
struct PlaybackEvent { std::string name; std::string argumentJson{"null"}; };
// A deliberately path-free, copied view, never a native pointer or mutable mpv node.
std::string PlaybackStatusJson(PlaybackSnapshot const& state);
class PlaybackProjection {
public:
    void Reset(PlaybackSnapshot const& state, bool music);
    std::vector<PlaybackEvent> Update(PlaybackSnapshot const& state, bool music);
private:
    MediaState media_{MediaState::Empty};
    std::int64_t entry_{-1};
    bool paused_{true}, music_{};
    double speed_{1};
    std::optional<double> position_, duration_;
};
}

// PlayerCore *Changed / MPVController property-driven OSD adaptation.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "OSDMessage.h"
#include "../Playback/PlaybackState.h"
#include <array>
namespace iima {
// Retains only scalar presentation facts, not media paths, metadata or playlists.
// First observation after load establishes a baseline. Unknown -> known optional
// values establish their own baseline rather than announcing defaults as edits.
class PlaybackOSD final {
public:
    std::vector<OSDMessage> Observe(PlaybackSnapshot const& state);
    void Reset() noexcept;
private:
    struct State {
        bool paused{}, muted{};
        double volume{}, speed{}, audioDelay{};
        std::array<std::optional<double>, 5> subtitle; // delay, secondary delay, scale, position, secondary position
        std::array<std::optional<bool>, 2> visibility;
        std::array<std::optional<std::int64_t>, 4> track; // video, audio, primary, secondary
        std::array<std::optional<int>, 5> equalizer; // brightness, contrast, saturation, gamma, hue
        std::optional<std::string> deinterlace, hwdec;
        RepeatMode repeat{RepeatMode::Off};
    };
    std::optional<State> previous_;
    std::int64_t entry_{-1};
};
}

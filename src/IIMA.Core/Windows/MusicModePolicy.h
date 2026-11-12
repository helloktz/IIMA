// IINA PlayerCore.switchToMiniPlayer/switchBackFromMiniPlayer/trackListChanged,
// PlaybackInfo.isAudio and MiniPlayerWindowController artwork policy.
// Copyright (c) 2016 lhc and IINA contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../Mpv/MpvClient.h"
#include <cstdint>
#include <optional>
#include <span>

namespace iima {
enum class MusicMediaKind { Unknown, Audio, Video };
struct MusicTrackSummary {
    MusicMediaKind kind{MusicMediaKind::Unknown};
    bool hasAudio{}, hasRealVideo{}, hasAlbumArt{};
};
// The complete media track list is relevant, not only currently selected tracks.
// A video file with its video track disabled must not be mistaken for music.
// Network media retains IINA's conservative non-audio classification.
MusicTrackSummary ClassifyMusicTracks(std::span<MediaTrack const> tracks,
                                     bool tracksKnown, bool network) noexcept;
// Playback readiness differs from layout classification: known audio without
// real video can play without an attached swap chain, including network audio.
// Cover art is optional visual output; its absence must not block audible sound.
bool AudioOnlyPlaybackEligible(std::span<MediaTrack const> tracks, bool tracksKnown) noexcept;

// UI-thread-owned policy, independent of WinUI controls or HWNDs. Actual windows,
// presenter transitions and playlist/artwork layout are owned by the caller.
class MusicModePolicy final {
public:
    struct MediaContext {
        // Changes on each START_FILE/open, including same-path reload. Do not use
        // only playlist entry ID: replay can retain that ID.
        std::uint64_t generation{};
        bool active{};
        bool tracksKnown{};
        bool network{};
        bool fullscreen{};
        bool automatic{true}; // Preference.autoSwitchToMusicMode default.
        std::span<MediaTrack const> tracks;
    };
    enum class Cause { Automatic, Manual };
    struct Transition {
        bool music{};
        Cause cause{Cause::Automatic};
        std::uint64_t generation{};
        std::uint64_t revision{};
    };

    // Feed media generation before handling manual input. The span is inspected
    // synchronously and not retained. Inactive/unknown media does not auto-switch.
    // Every call invalidates an uncommitted older recommendation. The manual
    // override persists across media generations, matching IINA's player lifetime;
    // it toggles on successful manual transitions and resets only on Close.
    std::optional<Transition> Update(MediaContext const& context);
    // Manual entry is allowed in fullscreen; caller must first successfully leave
    // fullscreen. No-op requests do not toggle the manual override.
    std::optional<Transition> Manual(bool music);
    // Call only after the corresponding native layout change succeeds. A failed
    // native change leaves mode and override intact. A stale ticket is rejected.
    bool Commit(Transition const& transition) noexcept;
    void CancelPending() noexcept;
    void Close() noexcept;
    bool IsMusic() const noexcept { return music_; }
    bool ManualOverride() const noexcept { return override_; }
    MusicTrackSummary Tracks() const noexcept { return summary_; }

private:
    std::optional<Transition> Propose(bool music, Cause cause);
    bool music_{}, override_{};
    std::optional<std::uint64_t> generation_;
    std::uint64_t revision_{};
    std::optional<Transition> pending_;
    MusicTrackSummary summary_;
};
}

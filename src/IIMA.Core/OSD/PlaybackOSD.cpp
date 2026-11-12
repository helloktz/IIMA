// IINA PlayerCore aidChanged/vidChanged/sidChanged/subScaleChanged etc.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "PlaybackOSD.h"
#include "../Media/TrackPresentation.h"
#include <algorithm>
#include <cmath>
namespace iima {
void PlaybackOSD::Reset() noexcept { previous_.reset(); entry_ = -1; }
std::vector<OSDMessage> PlaybackOSD::Observe(PlaybackSnapshot const& s) {
    std::vector<OSDMessage> messages;
    if (s.session != SessionState::Ready || s.media != MediaState::Loaded) { Reset(); return messages; }
    State next{s.paused, s.muted, s.volume, s.speed, s.audioDelay,
        {s.observedSubtitleDelay, s.secondarySubtitleDelay, s.subtitleScale, s.subtitlePosition, s.secondarySubtitlePosition},
        {s.subtitleVisibility, s.secondarySubtitlesVisible},
        {s.videoId, s.audioId, s.subtitleId, s.secondarySubtitleId},
        {s.videoBrightness, s.videoContrast, s.videoSaturation, s.videoGamma, s.videoHue},
        s.deinterlaceMode, s.hardwareDecodeMode, s.repeatMode};
    if (!previous_ || entry_ != s.entryId) { previous_ = std::move(next); entry_ = s.entryId; return messages; }
    auto const& old = *previous_;
    auto number = [&](OSDKind kind, double value) {
        if (!std::isfinite(value)) return;
        OSDMessage message; message.kind = kind; message.value = value; messages.push_back(std::move(message));
    };
    auto plain = [&](OSDKind kind) { OSDMessage message; message.kind = kind; messages.push_back(std::move(message)); };
    auto detail = [&](std::string title, std::string value) {
        OSDMessage message; message.kind = OSDKind::CustomWithDetail;
        message.text = std::move(title); message.detail = std::move(value); messages.push_back(std::move(message));
    };
    if (old.repeat != next.repeat)
        plain(next.repeat == RepeatMode::File ? OSDKind::FileLoop : next.repeat == RepeatMode::Playlist ? OSDKind::PlaylistLoop : OSDKind::NoLoop);
    constexpr OSDKind eq[]{OSDKind::Brightness, OSDKind::Contrast, OSDKind::Saturation, OSDKind::Gamma, OSDKind::Hue};
    for (std::size_t i = 0; i < next.equalizer.size(); ++i) {
        if (!old.equalizer[i] || !next.equalizer[i] || old.equalizer[i] == next.equalizer[i]) continue;
        OSDMessage message; message.kind = eq[i]; message.integer = *next.equalizer[i]; messages.push_back(std::move(message));
    }
    if (old.deinterlace && next.deinterlace && old.deinterlace != next.deinterlace)
        detail("Deinterlace", *next.deinterlace); // Preserve auto; never convert it to a misleading off.
    if (old.hwdec && next.hwdec && old.hwdec != next.hwdec)
        detail("Hardware decoding request", *next.hwdec + " (actual: " + (s.hwdecCurrent.empty() ? "unknown" : s.hwdecCurrent) + ")");
    constexpr OSDKind sub[]{OSDKind::SubtitleDelay, OSDKind::SecondarySubtitleDelay, OSDKind::SubtitleScale,
        OSDKind::SubtitlePosition, OSDKind::SecondarySubtitlePosition};
    for (std::size_t i = 0; i < next.subtitle.size(); ++i) {
        if (!old.subtitle[i] || !next.subtitle[i] || old.subtitle[i] == next.subtitle[i]) continue;
        auto value = *next.subtitle[i];
        if (i == 2) {
            // PlayerCore.subScaleChanged uses signed reciprocal display units.
            if (value <= 0) continue;
            value = std::round((value >= 1 ? value : -1 / value) * 100) / 100;
        }
        number(sub[i], value);
    }
    for (std::size_t i = 0; i < next.visibility.size(); ++i) {
        if (!old.visibility[i] || !next.visibility[i] || old.visibility[i] == next.visibility[i]) continue;
        plain(i == 0 ? (*next.visibility[i] ? OSDKind::SubtitleVisible : OSDKind::SubtitleHidden) :
            (*next.visibility[i] ? OSDKind::SecondarySubtitleVisible : OSDKind::SecondarySubtitleHidden));
    }
    constexpr OSDTrackType types[]{OSDTrackType::Video, OSDTrackType::Audio, OSDTrackType::Subtitle, OSDTrackType::SecondarySubtitle};
    constexpr std::string_view trackNames[]{"video", "audio", "sub", "sub"};
    for (std::size_t i = 0; i < next.track.size(); ++i) {
        if (!old.track[i] || !next.track[i] || old.track[i] == next.track[i]) continue;
        auto id = *next.track[i];
        OSDMessage message; message.kind = OSDKind::Track; message.trackType = types[i];
        if (id == -2) message.text = "Off";
        else if (id == -1) message.text = "Auto";
        else {
            auto track = std::find_if(s.tracks.begin(), s.tracks.end(), [&](auto const& t) { return t.type == trackNames[i] && t.id == id; });
            message.text = "ID " + std::to_string(id);
            if (track != s.tracks.end()) if (auto value = TrackPresentation::FromMediaTrack(*track))
                message.text = value->ReadableString();
        }
        messages.push_back(std::move(message));
    }
    // These high-frequency controls retain priority when a snapshot changes
    // several fields at once. The UI displays the last eligible message only.
    if (old.audioDelay != next.audioDelay) number(OSDKind::AudioDelay, next.audioDelay);
    if (old.speed != next.speed) number(OSDKind::Speed, next.speed);
    if (old.volume != next.volume) number(OSDKind::Volume, next.volume);
    if (old.muted != next.muted) plain(next.muted ? OSDKind::Mute : OSDKind::Unmute);
    if (old.paused != next.paused) plain(next.paused ? OSDKind::Pause : OSDKind::Resume);
    previous_ = std::move(next);
    return messages;
}
}

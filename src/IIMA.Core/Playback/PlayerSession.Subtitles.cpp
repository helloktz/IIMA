// IINA PlayerCore.swift subtitle operations; Copyright © 2016 lhc.
// GPL v3, references/iina/LICENSE. mpv DOCS/man/input.rst sub-reload/sub-seek.
#include "PlayerSession.h"
#include <algorithm>
#include <cmath>
namespace iima {
void PlayerSession::ApplySubtitleStyle(SubtitleStyle const& style) {
    if (snapshot_.session != SessionState::Ready) return;
    auto properties = style.Properties();
    if (!properties) {
        snapshot_.error = "Invalid subtitle style";
        Notify();
        return;
    }
    for (auto const& [name, value] : *properties) client_->Command({"set", name, value});
}
void PlayerSession::SetSubtitleScale(double scale, bool primary) {
    if (snapshot_.session != SessionState::Ready || !std::isfinite(scale)) return;
    scale = std::abs(scale); // IINA accepts signed slider values as magnitudes.
    // mpv mp_subtitle_shared_sub_opts permits zero (invisible text), unlike
    // IINA's narrower slider. Primary UI may still constrain its own editor.
    if (scale > 100) return;
    client_->Command({"set", primary ? "sub-scale" : "secondary-sub-scale", std::to_string(scale)});
}
void PlayerSession::SetSubtitlePosition(int percent, bool primary) {
    // Current mpv supports 0..150 for both slots. Values above 100 can put
    // text outside the visible image; the UI must explain that explicitly.
    if (snapshot_.session != SessionState::Ready || percent < 0 || percent > 150) return;
    client_->Command({"set", primary ? "sub-pos" : "secondary-sub-pos", std::to_string(percent)});
}
void PlayerSession::SetSecondarySubtitleDelay(double seconds) {
    if (snapshot_.session == SessionState::Ready && std::isfinite(seconds))
        client_->Command({"set", "secondary-sub-delay", std::to_string(seconds)});
}
void PlayerSession::SelectSecondarySubtitle(std::int64_t id) {
    if (snapshot_.session != SessionState::Ready || id < 0) return;
    if (id && std::none_of(snapshot_.tracks.begin(), snapshot_.tracks.end(), [id](auto const& track) {
        return track.type == "sub" && track.id == id;
    })) return;
    client_->Command({"set", "secondary-sid", id ? std::to_string(id) : "no"});
}
void PlayerSession::SetSecondarySubtitlesVisible(bool visible) {
    if (snapshot_.session == SessionState::Ready)
        client_->Command({"set", "secondary-sub-visibility", visible ? "yes" : "no"});
}
void PlayerSession::ReloadSubtitles() {
    if (snapshot_.session != SessionState::Ready) return;
    // Reload only external tracks. Embedded subtitle tracks cannot be reloaded.
    // mpv retains each reloaded track ID; do not optimistically reselect it.
    for (auto const& track : snapshot_.tracks)
        if (track.type == "sub" && track.external)
            client_->Command({"sub-reload", std::to_string(track.id)});
}
void PlayerSession::SeekSubtitle(int direction) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        direction < -1 || direction > 1) return;
    client_->Command({"sub-seek", std::to_string(direction)});
}
}

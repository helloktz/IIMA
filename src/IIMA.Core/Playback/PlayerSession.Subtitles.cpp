// IINA PlayerCore.swift subtitle operations; Copyright © 2016 lhc.
// GPL v3, references/iina/LICENSE. mpv DOCS/man/input.rst sub-reload/sub-seek.
#include "PlayerSession.h"
#include <algorithm>
#include "../Subtitles/CharacterEncoding.h"
#include <cmath>
namespace iima {
void PlayerSession::ApplySubtitleStyle(SubtitleStyle const& style, std::vector<std::string> fields) {
    if (snapshot_.session != SessionState::Ready) return;
    auto properties = style.Properties();
    if (!properties) {
        snapshot_.error = "Invalid subtitle style";
        Notify();
        return;
    }
    // IINA's individual setters leave unrelated style fields untouched. Validate
    // the complete requested subset before issuing any asynchronous commands.
    for (auto const& field : fields) {
        if (std::none_of(properties->begin(), properties->end(), [&](auto const& pair) { return pair.first == field; })) {
            snapshot_.error = "Unknown subtitle style field"; Notify(); return;
        }
    }
    for (auto const& [name, value] : *properties)
        if (fields.empty() || std::find(fields.begin(), fields.end(), name) != fields.end())
            client_->Command({"set", name, value});
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
void PlayerSession::SeekSubtitle(int direction, bool primary) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !snapshot_.seekable || waitingForStart_ || playlistEdit_ || direction < -1 || direction > 1) return;
    auto id = primary ? snapshot_.subtitleId : snapshot_.secondarySubtitleId;
    if (!id || *id <= 0) return;
    client_->Command({"sub-seek", std::to_string(direction), primary ? "primary" : "secondary"});
}
// IINA's shipped input.conf Ctrl+Shift+LEFT/RIGHT uses sub-step, which
// changes subtitle delay rather than moving the audio/video playback position.
void PlayerSession::StepSubtitleTiming(int direction, bool primary) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || playlistEdit_ || direction < -1 || direction > 1) return;
    auto id = primary ? snapshot_.subtitleId : snapshot_.secondarySubtitleId;
    if (!id || *id <= 0) return;
    client_->Command({"sub-step", std::to_string(direction), primary ? "primary" : "secondary"});
    // Neither compute a synthetic delay nor seek/resume: observed delay is
    // authoritative and available events depend on mpv's subtitle prefetch.
}
bool PlayerSession::SetSubtitleEncoding(std::string encoding) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || playlistEdit_ || subtitleEncodingRequest_ || !IsSubtitleEncoding(encoding)) return false;
    subtitleEncodingTracks_.clear();
    for (auto const& track : snapshot_.tracks)
        if (track.type == "sub" && track.external)
            subtitleEncodingTracks_.emplace_back(track.id, track.externalPath);
    subtitleEncodingGeneration_ = windowMediaGeneration_;
    subtitleEncodingRequest_ = client_->Command({"set", "sub-codepage", std::move(encoding)});
    Notify();
    return subtitleEncodingRequest_ != 0;
}
void PlayerSession::HandleSubtitleEncodingReply(MpvEvent const& event) {
    if (event.kind != MpvEvent::Kind::CommandReply || !subtitleEncodingRequest_ ||
        event.requestId != subtitleEncodingRequest_) return;
    subtitleEncodingRequest_ = 0;
    auto tracks = std::move(subtitleEncodingTracks_);
    subtitleEncodingTracks_.clear();
    if (event.error < 0 || snapshot_.media != MediaState::Loaded || waitingForStart_ ||
        subtitleEncodingGeneration_ != windowMediaGeneration_) return;
    // Do not reload embedded tracks or a replacement external track reusing an ID.
    // mpv retains subtitle IDs on reload. Selection remains the observed state.
    for (auto const& [id, filename] : tracks)
        if (std::any_of(snapshot_.tracks.begin(), snapshot_.tracks.end(), [&](auto const& track) {
            return track.type == "sub" && track.external && track.id == id && track.externalPath == filename;
        })) client_->Command({"sub-reload", std::to_string(id)});
}

}

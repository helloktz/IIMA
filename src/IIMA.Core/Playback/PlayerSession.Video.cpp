// Adapted from IINA PlayerCore.swift setVideoAspect, setVideoRotate,
// toggleDeinterlace, setVideoEqualizer, addVideoFilter/removeVideoFilter.
// Copyright © 2016 lhc. SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerSession.h"
#include "../Mpv/Catalog/MPVOption.h"
#include "../Geometry/Aspect.h"
#include "../Filters/MPVFilter.h"
#include <algorithm>
namespace iima {
void PlayerSession::SetVideoAspect(std::string aspect) {
    if (snapshot_.session != SessionState::Ready) return;
    client_->Command({"set", std::string(mpv_catalog::MPVOption::Video::videoAspectOverride), Aspect::Parse(aspect) ? aspect : "-1"});
}
void PlayerSession::SetVideoRotation(int degrees) {
    if (snapshot_.session != SessionState::Ready) return;
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) return;
    client_->Command({"set", "video-rotate", std::to_string(degrees)});
}
// IINA PlayerCore.toggleHardwareDecoding: this is a session override, not
// a persistent preference write. Actual hardware use comes from hwdec-current.
void PlayerSession::SetHardwareDecoding(std::string mode) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || playlistEdit_) return;
    if (mode != "no" && mode != "auto" && mode != "auto-copy") return;
    client_->Command({"set", "hwdec", std::move(mode)});
}
void PlayerSession::SetDeinterlace(bool enabled) {
    if (snapshot_.session == SessionState::Ready)
        client_->Command({"set", "deinterlace", enabled ? "yes" : "no"});
}
void PlayerSession::SetVideoEqualizer(std::string property, int value) {
    if (snapshot_.session != SessionState::Ready) return;
    if (property != "brightness" && property != "contrast" && property != "saturation" &&
        property != "gamma" && property != "hue") return;
    client_->Command({"set", std::move(property), std::to_string(std::clamp(value, -100, 100))});
}
void PlayerSession::AddVideoFilter(std::string filter) {
    if (snapshot_.session != SessionState::Ready) return;
    auto parsed = MPVFilter::Parse(filter);
    if (!parsed || filter.find('\0') != std::string::npos) {
        snapshot_.error = "Invalid video filter expression";
        Notify();
        return;
    }
    // mpv vf add replaces an existing filter carrying the same label. The
    // asynchronous error path remains authoritative; no speculative filter state.
    client_->Command({"vf", "add", parsed->StringFormat()});
}
void PlayerSession::RemoveVideoFilter(std::string label) {
    if (snapshot_.session != SessionState::Ready || label.empty()) return;
    // Removal is by label, never an untrusted filter expression or list index.
    if (!std::all_of(label.begin(), label.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-';
    })) return;
    client_->Command({"vf", "remove", "@" + label});
}
void PlayerSession::ToggleVideoOrientation(bool mirror) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || playlistEdit_) return;
    auto const& chain=snapshot_.videoFilters;
    auto status=FindOrientationFilter(chain,mirror);
    switch (status.state) {
    case OrientationFilterState::Absent:
        // Labelled add is idempotent on repeated clicks before observation.
        // A script may still race the final add: mpv has no compare-and-swap.
        AddVideoFilter(mirror ? "@iina_mirror:hflip" : "@iina_flip:vflip"); break;
    case OrientationFilterState::Disabled:
        EditFilters(false,FilterChainEditor::SetEnabled(chain,chain.revision,status.index,true)); break;
    case OrientationFilterState::Enabled:
        EditFilters(false,FilterChainEditor::Remove(chain,chain.revision,status.index)); break;
    default:
        snapshot_.error="Orientation filter state is unavailable or its reserved label conflicts"; Notify(); break;
    }
}

}

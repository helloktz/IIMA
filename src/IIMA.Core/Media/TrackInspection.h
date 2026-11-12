// IINA InspectorWindowController.TracksVC / MPVTrack.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "../Mpv/MpvClient.h"
#include <span>
#include <utility>
namespace iima {
struct TrackIdentity {
    std::string type;
    std::int64_t id{};
    bool operator==(TrackIdentity const&) const = default;
};
struct TrackInspection {
    TrackIdentity identity;
    std::string label;
    std::vector<std::pair<std::string,std::string>> fields;
    bool truncated{};
    // Plain text only: path/URL values are never interpreted or executed.
    std::string Report() const;
};
// Reject the whole list for >256 entries, unsupported types, negative IDs or
// duplicate (type,id). IDs alone are not unique across different track types.
// Field copies are at most 1024 UTF-8 bytes, label at most 1152; invalid UTF-8,
// control/bidi bytes are replaced. Unknown optional values display "Unknown";
// false flags mean the compact model's false (including an absent mpv flag).
// A bounded projection does not imply the upstream track-list was complete.
std::optional<std::vector<TrackInspection>> InspectTracks(std::span<MediaTrack const>);
std::optional<TrackInspection> InspectTrack(std::span<MediaTrack const>,TrackIdentity const&);
}

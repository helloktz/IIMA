// Adapted from IINA MPVTrack.swift (copyright 2016 lhc), ISO639Helper.swift
// (copyright 2017 lhc), and Extensions.swift. GNU GPL v3; see references/iina/LICENSE.
#pragma once
#include "../Mpv/MpvClient.h"
#include "../Mpv/include/mpv/client.h"
#include <map>
#include <string_view>

namespace iima {
enum class TrackKind { Video, Audio, Subtitle, SecondarySubtitle };
struct TrackLabelStrings {
    std::string noTitle{"No Title"};
    std::string none{"<None>"};
    std::string defaultTrack{"Default"};
};
// Return a Windows-localized language name, or nullopt when not recognised.
// The resolver is called synchronously and never retained by TrackPresentation.
using TrackLanguageResolver = std::function<std::optional<std::string>(std::string_view)>;

struct TrackPresentation {
    std::int64_t id{};
    TrackKind kind{TrackKind::Video};
    std::optional<std::int64_t> sourceId;
    std::optional<std::string> title, language, externalFilename, codec, decoderDescription;
    std::optional<std::int64_t> width, height, channelCount, sampleRate;
    std::optional<std::string> channelLayout;
    std::optional<double> framesPerSecond;
    bool isDefault{}, forced{}, image{}, selected{}, external{}, albumArt{};

    // Copies all fields while mpv_node memory remains valid. ID/type are required; absent optional flags are false. No borrowed pointer is kept in the result.
    static std::optional<TrackPresentation> FromNode(mpv_node const& node);
    // Adapter for the current compact event payload, with absent details left
    // unknown. Do not infer codec/default/forced flags from display text.
    static std::optional<TrackPresentation> FromMediaTrack(MediaTrack const& track);
    static TrackPresentation None(TrackKind kind, TrackLabelStrings const& strings = {});
    std::string IdString() const;
    std::string ReadableString(TrackLabelStrings const& strings = {},
        TrackLanguageResolver const& resolver = {}, bool includingLanguage = true) const;
    std::string ReadableTitle(TrackLabelStrings const& strings = {},
        TrackLanguageResolver const& resolver = {}) const;
    bool IsImageSubtitle() const noexcept;
    bool IsAssSubtitle() const noexcept;
};

// Explicit selection values avoid confusing menu row indexes with mpv IDs.
// A request is not confirmation: selection must still be projected from mpv.
struct TrackSelectionValue {
    enum class Mode { None, Automatic, Id };
    Mode mode{Mode::None};
    std::int64_t id{};
    static std::optional<TrackSelectionValue> FromMenuId(std::int64_t id);
    static std::optional<TrackSelectionValue> Parse(std::string_view value);
    std::optional<std::string> MpvValue() const;
};
std::string_view TrackSelectionProperty(TrackKind kind) noexcept;

struct TrackLanguageEntry {
    std::string code;
    std::vector<std::string> names;
    std::string Description() const;
};
// ISO639Helper's business rules without copying its data into source lines.
// Caller loads the original ISO639 strings resource (or a Windows resource
// equivalent) into this map, preserving licensing and localization separately.
std::vector<TrackLanguageEntry> BuildTrackLanguages(
    std::map<std::string, std::string, std::less<>> const& namesByCode,
    std::function<bool(std::string const&, std::string const&)> const& localizedLess = {});
std::optional<std::string> LanguageCodeFromDescription(std::string_view description);
}

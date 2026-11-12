// Adapted from IINA MPVTrack.swift (copyright 2016 lhc), ISO639Helper.swift
// (copyright 2017 lhc), and Extensions.swift. GNU GPL v3; see references/iina/LICENSE.
#include "TrackPresentation.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <locale>
#include <sstream>

namespace iima {
namespace {
mpv_node const* Field(mpv_node const& node, char const* name) {
    if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list ||
        !node.u.list->keys || !node.u.list->values) return nullptr;
    auto const& list = *node.u.list;
    for (int index = 0; index < list.num; ++index)
        if (list.keys[index] && std::strcmp(list.keys[index], name) == 0) return &list.values[index];
    return nullptr;
}
std::optional<std::string> Text(mpv_node const& node, char const* key) {
    auto field = Field(node, key);
    if (!field || field->format != MPV_FORMAT_STRING || !field->u.string) return std::nullopt;
    return std::string(field->u.string);
}
std::optional<std::int64_t> Integer(mpv_node const& node, char const* key) {
    auto field = Field(node, key);
    if (!field || field->format != MPV_FORMAT_INT64) return std::nullopt;
    return field->u.int64;
}
std::optional<bool> Flag(mpv_node const& node, char const* key) {
    auto field = Field(node, key);
    if (!field || field->format != MPV_FORMAT_FLAG) return std::nullopt;
    return field->u.flag != 0;
}
std::optional<TrackKind> Kind(std::string_view value) {
    if (value == "video") return TrackKind::Video;
    if (value == "audio") return TrackKind::Audio;
    if (value == "sub") return TrackKind::Subtitle;
    if (value == "secondSub") return TrackKind::SecondarySubtitle;
    return std::nullopt;
}
bool HasVisibleText(std::string_view value) {
    // Decode enough UTF-8 to recognise Foundation whitespaceAndNewlines. The
    // original string is retained; only the all-whitespace fallback is tested.
    for (std::size_t offset = 0; offset < value.size();) {
        auto lead = static_cast<unsigned char>(value[offset++]);
        std::uint32_t code = lead;
        unsigned continuation = 0;
        if ((lead & 0xe0) == 0xc0) { code &= 0x1f; continuation = 1; }
        else if ((lead & 0xf0) == 0xe0) { code &= 0x0f; continuation = 2; }
        else if ((lead & 0xf8) == 0xf0) { code &= 0x07; continuation = 3; }
        else if (lead >= 0x80) return true;
        if (continuation > value.size() - offset) return true;
        while (continuation--) {
            auto next = static_cast<unsigned char>(value[offset++]);
            if ((next & 0xc0) != 0x80) return true;
            code = (code << 6) | (next & 0x3f);
        }
        bool const whitespace = (code >= 9 && code <= 13) || code == 0x20 ||
            code == 0x85 || code == 0xa0 || code == 0x1680 ||
            (code >= 0x2000 && code <= 0x200a) || code == 0x2028 || code == 0x2029 ||
            code == 0x202f || code == 0x205f || code == 0x3000;
        if (!whitespace) return true;
    }
    return false;
}
std::string Pretty(double value) {
    if (!std::isfinite(value)) return {};
    // Three decimal places with trailing zeros removed matches prettyFormat
    // for media rates, and avoids locale-specific commas in the UI labels.
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream.precision(3);
    auto const scaled = value * 1000;
    stream << (std::isfinite(scaled) ? std::round(scaled) / 1000 : value);
    auto result = stream.str();
    while (!result.empty() && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    if (result == "-0") return "0";
    return result;
}
std::string Join(std::vector<std::string> const& values, std::string_view separator) {
    std::string result;
    bool first = true;
    for (auto const& value : values) {
        if (!first) result += separator;
        first = false;
        result += value;
    }
    return result;
}
}

std::optional<TrackPresentation> TrackPresentation::FromNode(mpv_node const& node) {
    auto id = Integer(node, "id");
    auto type = Text(node, "type");
    auto kind = type ? Kind(*type) : std::nullopt;
    auto isDefault = Flag(node, "default");
    auto forced = Flag(node, "forced");
    auto image = Flag(node, "image");
    auto selected = Flag(node, "selected");
    auto external = Flag(node, "external");
    // The pinned IINA initializer requires every flag. Keep compatible with
    // partial mpv node snapshots: absent optional flags are false, not a reason
    // to hide an otherwise identifiable audio/subtitle track.
    if (!id || !kind) return std::nullopt;
    TrackPresentation result;
    result.id = *id;
    result.kind = *kind;
    result.isDefault = isDefault.value_or(false);
    result.forced = forced.value_or(false);
    result.image = image.value_or(false);
    result.selected = selected.value_or(false);
    result.external = external.value_or(false);
    result.sourceId = Integer(node, "src-id");
    result.title = Text(node, "title");
    if (!result.title || !HasVisibleText(*result.title)) {
        auto metadata = Field(node, "metadata");
        result.title = metadata ? Text(*metadata, "name") : std::nullopt;
        if (result.title && !HasVisibleText(*result.title)) result.title.reset();
    }
    result.language = Text(node, "lang");
    result.codec = Text(node, "codec");
    result.externalFilename = Text(node, "external-filename");
    result.decoderDescription = Text(node, "decoder-desc");
    result.albumArt = Flag(node, "albumart").value_or(false);
    result.width = Integer(node, "demux-w");
    result.height = Integer(node, "demux-h");
    result.channelCount = Integer(node, "demux-channel-count");
    result.channelLayout = Text(node, "demux-channels");
    result.sampleRate = Integer(node, "demux-samplerate");
    auto fps = Field(node, "demux-fps");
    if (fps && fps->format == MPV_FORMAT_DOUBLE && std::isfinite(fps->u.double_))
        result.framesPerSecond = fps->u.double_;
    return result;
}
std::optional<TrackPresentation> TrackPresentation::FromMediaTrack(MediaTrack const& track) {
    auto kind = Kind(track.type);
    if (!kind) return std::nullopt;
    TrackPresentation result;
    result.id = track.id;
    result.kind = *kind;
    if (HasVisibleText(track.title)) result.title = track.title;
    if (!track.lang.empty()) result.language = track.lang;
    if (!track.externalPath.empty()) result.externalFilename = track.externalPath;
    result.selected = track.selected;
    result.external = track.external;
    result.sourceId = track.sourceId; result.width = track.width; result.height = track.height;
    result.channelCount = track.channelCount; result.sampleRate = track.sampleRate;
    result.codec = track.codec; result.decoderDescription = track.decoderDescription;
    result.channelLayout = track.channelLayout; result.framesPerSecond = track.framesPerSecond;
    result.isDefault = track.isDefault; result.forced = track.forced;
    result.image = track.image; result.albumArt = track.albumArt;
    return result;
}
TrackPresentation TrackPresentation::None(TrackKind kind, TrackLabelStrings const& strings) {
    TrackPresentation result;
    result.kind = kind;
    // Also label secondary None, fixing the unlabeled IINA sentinel.
    result.title = strings.none;
    return result;
}
std::string TrackPresentation::IdString() const { return "#" + std::to_string(id); }
std::string TrackPresentation::ReadableString(TrackLabelStrings const& strings,
    TrackLanguageResolver const& resolver, bool includingLanguage) const {
    std::vector<std::string> pieces;
    if (includingLanguage && language && *language != "und" && resolver) {
        auto display = resolver(*language);
        if (display && !display->empty()) pieces.push_back("[" + *display + "]");
    }
    auto name = title.value_or(strings.noTitle);
    if (!name.empty()) pieces.push_back(std::move(name));
    std::vector<std::string> details;
    if (codec) details.push_back(*codec);
    if (kind == TrackKind::Video) {
        if (width && height) details.push_back(std::to_string(*width) + "\xc3\x97" + std::to_string(*height));
        if (framesPerSecond && std::isfinite(*framesPerSecond)) details.push_back(Pretty(*framesPerSecond) + "fps");
    } else if (kind == TrackKind::Audio) {
        if (channelCount) details.push_back(std::to_string(*channelCount) + "ch");
        if (sampleRate) details.push_back(Pretty(static_cast<double>(*sampleRate) / 1000) + "kHz");
    }
    if (isDefault) details.push_back(strings.defaultTrack);
    auto info = Join(details, ", ");
    if (!info.empty()) pieces.push_back("(" + info + ")");
    return Join(pieces, " ");
}
std::string TrackPresentation::ReadableTitle(TrackLabelStrings const& strings,
    TrackLanguageResolver const& resolver) const {
    return IdString() + " " + ReadableString(strings, resolver);
}
bool TrackPresentation::IsImageSubtitle() const noexcept {
    return (kind == TrackKind::Subtitle || kind == TrackKind::SecondarySubtitle) &&
        (codec == "hdmv_pgs_subtitle" || codec == "dvb_subtitle");
}
bool TrackPresentation::IsAssSubtitle() const noexcept {
    return (kind == TrackKind::Subtitle || kind == TrackKind::SecondarySubtitle) && codec == "ass";
}
std::string_view TrackSelectionProperty(TrackKind kind) noexcept {
    switch (kind) {
    case TrackKind::Video: return "vid";
    case TrackKind::Audio: return "aid";
    case TrackKind::Subtitle: return "sid";
    case TrackKind::SecondarySubtitle: return "secondary-sid";
    }
    return {};
}
std::optional<TrackSelectionValue> TrackSelectionValue::FromMenuId(std::int64_t value) {
    if (value < 0) return std::nullopt;
    return TrackSelectionValue{value == 0 ? Mode::None : Mode::Id, value};
}
std::optional<TrackSelectionValue> TrackSelectionValue::Parse(std::string_view value) {
    if (value.empty()) return std::nullopt;
    if (value == "auto") return TrackSelectionValue{Mode::Automatic};
    if (value == "no") return TrackSelectionValue{};
    std::int64_t id{};
    auto result = std::from_chars(value.data(), value.data() + value.size(), id);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return std::nullopt;
    return FromMenuId(id);
}
std::optional<std::string> TrackSelectionValue::MpvValue() const {
    switch (mode) {
    case Mode::None: return "no";
    case Mode::Automatic: return "auto";
    case Mode::Id: if (id > 0) return std::to_string(id); else return std::nullopt;
    }
    return std::nullopt;
}
std::string TrackLanguageEntry::Description() const {
    return (names.empty() ? code : names.front()) + " (" + code + ")";
}
std::vector<TrackLanguageEntry> BuildTrackLanguages(
    std::map<std::string, std::string, std::less<>> const& namesByCode,
    std::function<bool(std::string const&, std::string const&)> const& localizedLess) {
    std::vector<TrackLanguageEntry> result;
    for (auto const& [code, names] : namesByCode) {
        TrackLanguageEntry entry{code};
        std::string_view rest = names;
        while (!rest.empty()) {
            auto delimiter = rest.find(';');
            auto name = rest.substr(0, delimiter);
            if (!name.empty()) entry.names.emplace_back(name);
            if (delimiter == std::string_view::npos) break;
            rest.remove_prefix(delimiter + 1);
        }
        result.push_back(std::move(entry));
    }
    std::stable_sort(result.begin(), result.end(), [&](auto const& a, auto const& b) {
        return localizedLess ? localizedLess(a.Description(), b.Description()) : a.Description() < b.Description();
    });
    return result;
}
std::optional<std::string> LanguageCodeFromDescription(std::string_view description) {
    if (description.empty() || description.back() != ')') return std::nullopt;
    auto open = description.rfind('(');
    if (open == std::string_view::npos || open == 0) return std::nullopt;
    auto code = description.substr(open + 1, description.size() - open - 2);
    if (code.size() < 2 || code.size() > 3) return std::nullopt;
    if (!std::all_of(code.begin(), code.end(), [](char c) { return c >= 'a' && c <= 'z'; })) return std::nullopt;
    return std::string(code);
}
}

// Derived from IINA JavascriptAPICore.StatusAPI / EventController / PlayerCore.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginPlaybackProjection.h"
#include "../../IIMA.Core/Media/TrackPresentation.h"
#include <winrt/Windows.Data.Json.h>
#include <cmath>
#include <charconv>
#include <stdexcept>
#include <string_view>
namespace iima::plugins {
namespace {
using namespace winrt::Windows::Data::Json;
IJsonValue Number(std::optional<double> value) {
    return value && std::isfinite(*value) ? JsonValue::CreateNumberValue(*value) : JsonValue::CreateNullValue();
}
std::string JsonNumber(std::optional<double> value) { return winrt::to_string(Number(value).Stringify()); }
IJsonValue Dimension(MediaProperties const& properties, char const* key) {
    auto found = properties.find(key);
    if (found == properties.end()) return JsonValue::CreateNullValue();
    int value{};
    auto parsed = std::from_chars(found->second.data(), found->second.data() + found->second.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != found->second.data() + found->second.size() || value <= 0)
        return JsonValue::CreateNullValue();
    return JsonValue::CreateNumberValue(value);
}

constexpr std::size_t SnapshotByteLimit = 64 * 1024;
constexpr std::size_t MetadataTextLimit = 4096;
constexpr std::int64_t LargestExactJsonInteger = 9007199254740991LL;
IJsonValue Integer(std::optional<std::int64_t> value) {
    if (!value) return JsonValue::CreateNullValue();
    if (*value < -LargestExactJsonInteger || *value > LargestExactJsonInteger)
        throw std::invalid_argument("Plugin metadata contains an integer outside the JavaScript safe range.");
    return JsonValue::CreateNumberValue(static_cast<double>(*value));
}
IJsonValue Text(std::optional<std::string> const& value) {
    if (!value) return JsonValue::CreateNullValue();
    if (value->size() > MetadataTextLimit)
        throw std::length_error("Plugin track/chapter metadata exceeds the text budget.");
    return JsonValue::CreateStringValue(winrt::to_hstring(*value));
}
IJsonValue OptionalText(std::string const& value) {
    return Text(value.empty() ? std::nullopt : std::optional<std::string>(value));
}
// Budget each serialized node before adding it to the shared payload; a hostile
// file must not cause unbounded metadata allocation on the UI thread. The final
// exact UTF-8 byte check below includes all commas, keys and JSON escaping.
class ProjectionBudget {
public:
    void Account(IJsonValue const& value) {
        auto size = winrt::to_string(value.Stringify()).size();
        if (size > SnapshotByteLimit - used_)
            throw std::length_error("Plugin playback snapshot exceeds 64 KiB.");
        used_ += size;
    }
private:
    std::size_t used_{1024}; // fixed status and track-container overhead
};
JsonObject SerializeTrack(MediaTrack const& track) {
    JsonObject result;
    result.Insert(L"id", Integer(track.id));
    result.Insert(L"title", OptionalText(track.title));
    result.Insert(L"lang", OptionalText(track.lang));
    result.Insert(L"codec", Text(track.codec));
    result.Insert(L"isDefault", JsonValue::CreateBooleanValue(track.isDefault));
    result.Insert(L"isForced", JsonValue::CreateBooleanValue(track.forced));
    result.Insert(L"isSelected", JsonValue::CreateBooleanValue(track.selected));
    result.Insert(L"isExternal", JsonValue::CreateBooleanValue(track.external));
    result.Insert(L"demuxW", Integer(track.width));
    result.Insert(L"demuxH", Integer(track.height));
    result.Insert(L"demuxChannelCount", Integer(track.channelCount));
    result.Insert(L"demuxChannels", Text(track.channelLayout));
    result.Insert(L"demuxSamplerate", Integer(track.sampleRate));
    result.Insert(L"demuxFPS", Number(track.framesPerSecond));
    // Track/chapter labels are part of the explicit metadata grant; external
    // paths are not. ReadableTitle's copied model never receives externalPath.
    auto display = track;
    display.externalPath.clear();
    auto label = TrackPresentation::FromMediaTrack(display);
    result.Insert(L"formattedTitle", Text(label ?
        std::optional<std::string>(label->ReadableTitle()) : std::nullopt));
    return result;
}
JsonObject TrackApi(PlaybackSnapshot const& state, std::string_view type, ProjectionBudget& budget) {
    JsonObject result;
    JsonArray tracks;
    IJsonValue selected = JsonValue::CreateNullValue();
    IJsonValue selectedId = JsonValue::CreateNullValue();
    std::size_t selectedCount{};
    for (auto const& track : state.tracks) {
        if (track.type != type) continue;
        auto value = SerializeTrack(track);
        budget.Account(value);
        tracks.Append(value);
        if (track.selected) { ++selectedCount; selected = value; selectedId = Integer(track.id); }
    }
    // The current compact mpv payload does not carry main-selection. In
    // particular two selected subtitle tracks cannot be assigned primary vs
    // secondary without inventing an identity; leave selection unknown then.
    if (selectedCount != 1) {
        selected = JsonValue::CreateNullValue(); selectedId = JsonValue::CreateNullValue();
    }
    budget.Account(selected); // currentTrack duplicates the object in JSON
    result.Insert(L"tracks", tracks);
    result.Insert(L"currentTrack", selected);
    result.Insert(L"id", selectedId);
    if (type == "audio") {
        result.Insert(L"delay", Number(state.audioDelay));
        result.Insert(L"volume", Number(state.volume));
        result.Insert(L"muted", JsonValue::CreateBooleanValue(state.muted));
    } else if (type == "sub") {
        result.Insert(L"delay", Number(state.subtitleDelay));
        result.Insert(L"secondID", JsonValue::CreateNullValue());
    }
    return result;
}
}
std::string PlaybackStatusJson(PlaybackSnapshot const& state, std::uint64_t mediaToken) {
    // No truncation: runtime/UI callers receive an explicit failure rather than
    // a misleading claim that getChapters()/tracks returned a complete list.
    if (state.tracks.size() > 256 || state.chapters.size() > 2048 || state.playlist.size() > 1024)
        throw std::length_error("Plugin media collection exceeds the projection limit.");
    ProjectionBudget budget;
    JsonObject json;
    // Internal transport tag, not a property of the public core.status API.
    // Decimal string avoids loss above JavaScript's largest exact integer.
    json.Insert(L"_mediaToken", JsonValue::CreateStringValue(winrt::to_hstring(std::to_string(mediaToken))));
    json.Insert(L"paused", JsonValue::CreateBooleanValue(state.paused));
    json.Insert(L"idle", JsonValue::CreateBooleanValue(state.media == MediaState::Empty || state.media == MediaState::Ended));
    json.Insert(L"position", Number(state.position));
    json.Insert(L"duration", Number(state.duration));
    json.Insert(L"speed", Number(state.speed));
    json.Insert(L"videoWidth", Dimension(state.videoParameters, "w"));
    json.Insert(L"videoHeight", Dimension(state.videoParameters, "h"));
    json.Insert(L"isNetworkResource", JsonValue::CreateBooleanValue(state.path.find("://") != std::string::npos));
    // The core status URL/title remain outside this API subset. The separately
    // disclosed playlist API below includes its complete filenames/titles; do
    // not mistake these null status fields for a path-confidentiality boundary.
    json.Insert(L"url", JsonValue::CreateNullValue());
    json.Insert(L"title", JsonValue::CreateNullValue());
    json.Insert(L"audio", TrackApi(state, "audio", budget));
    json.Insert(L"subtitle", TrackApi(state, "sub", budget));
    json.Insert(L"video", TrackApi(state, "video", budget));
    JsonArray chapters;
    for (auto const& chapter : state.chapters) {
        JsonObject value;
        value.Insert(L"title", Text(std::optional<std::string>(chapter.title)));
        value.Insert(L"start", Number(chapter.time));
        budget.Account(value);
        chapters.Append(value);
    }
    json.Insert(L"chapters", chapters);
    JsonArray playlist, playlistIds;
    for (auto const& item : state.playlist) {
        JsonObject value;
        value.Insert(L"filename", Text(std::optional<std::string>(item.path)));
        value.Insert(L"title", OptionalText(item.title));
        value.Insert(L"isCurrent", JsonValue::CreateBooleanValue(item.current));
        value.Insert(L"isPlaying", JsonValue::CreateBooleanValue(item.id == state.entryId &&
            (state.media == MediaState::Loading || state.media == MediaState::Loaded)));
        budget.Account(value);
        playlist.Append(value);
        // Full decimal identity, never rounded through a JavaScript Number.
        if (item.id < 0) throw std::invalid_argument("Invalid observed playlist entry identity.");
        auto id = JsonValue::CreateStringValue(winrt::to_hstring(std::to_string(item.id)));
        budget.Account(id);
        playlistIds.Append(id);
    }
    json.Insert(L"playlist", playlist);
    json.Insert(L"_playlistIds", playlistIds);
    auto output = winrt::to_string(json.Stringify());
    if (output.size() > SnapshotByteLimit)
        throw std::length_error("Plugin playback snapshot exceeds 64 KiB.");
    return output;
}
void PlaybackProjection::Reset(PlaybackSnapshot const& state, bool music) {
    media_ = state.media; entry_ = state.entryId; paused_ = state.paused; music_ = music;
    speed_ = state.speed; position_ = state.position; duration_ = state.duration;
    volume_ = state.volume; muted_ = state.muted;
    audioDelay_ = state.audioDelay; subtitleDelay_ = state.subtitleDelay;
}
std::vector<PlaybackEvent> PlaybackProjection::Update(PlaybackSnapshot const& state, bool music) {
    std::vector<PlaybackEvent> events;
    if (state.media == MediaState::Loading && (media_ != MediaState::Loading || entry_ != state.entryId))
        events.push_back({"iina.file-started"});
    if (state.media == MediaState::Loaded && (media_ != MediaState::Loaded || entry_ != state.entryId))
        events.push_back({"iina.file-loaded"});
    if (state.paused != paused_) events.push_back({"mpv.pause.changed", state.paused ? "true" : "false"});
    if (state.position != position_) events.push_back({"mpv.time-pos.changed", JsonNumber(state.position)});
    if (state.duration != duration_) events.push_back({"mpv.duration.changed", JsonNumber(state.duration)});
    if (state.speed != speed_) events.push_back({"mpv.speed.changed", JsonNumber(state.speed)});
    if (state.volume != volume_) events.push_back({"mpv.volume.changed", JsonNumber(state.volume)});
    if (state.muted != muted_) events.push_back({"mpv.mute.changed", state.muted ? "true" : "false"});
    if (state.audioDelay != audioDelay_) events.push_back({"mpv.audio-delay.changed", JsonNumber(state.audioDelay)});
    if (state.subtitleDelay != subtitleDelay_) events.push_back({"mpv.sub-delay.changed", JsonNumber(state.subtitleDelay)});
    if (music != music_) events.push_back({"iina.music-mode.changed", music ? "true" : "false"});
    Reset(state, music);
    return events;
}
}

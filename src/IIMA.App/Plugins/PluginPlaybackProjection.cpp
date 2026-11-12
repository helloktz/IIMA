// Derived from IINA JavascriptAPICore.StatusAPI / EventController / PlayerCore.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginPlaybackProjection.h"
#include <winrt/Windows.Data.Json.h>
#include <cmath>
#include <charconv>
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
}
std::string PlaybackStatusJson(PlaybackSnapshot const& state) {
    JsonObject json;
    json.Insert(L"paused", JsonValue::CreateBooleanValue(state.paused));
    json.Insert(L"idle", JsonValue::CreateBooleanValue(state.media == MediaState::Empty || state.media == MediaState::Ended));
    json.Insert(L"position", Number(state.position));
    json.Insert(L"duration", Number(state.duration));
    json.Insert(L"speed", Number(state.speed));
    json.Insert(L"videoWidth", Dimension(state.videoParameters, "w"));
    json.Insert(L"videoHeight", Dimension(state.videoParameters, "h"));
    json.Insert(L"isNetworkResource", JsonValue::CreateBooleanValue(state.path.find("://") != std::string::npos));
    // This initial grant is playback control, not arbitrary path/history access.
    // IINA returns the complete resource URL/title; IIMA deliberately omits both
    // until an explicit metadata/path grant is introduced.
    json.Insert(L"url", JsonValue::CreateNullValue());
    json.Insert(L"title", JsonValue::CreateNullValue());
    return winrt::to_string(json.Stringify());
}
void PlaybackProjection::Reset(PlaybackSnapshot const& state, bool music) {
    media_ = state.media; entry_ = state.entryId; paused_ = state.paused; music_ = music;
    speed_ = state.speed; position_ = state.position; duration_ = state.duration;
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
    if (music != music_) events.push_back({"iina.music-mode.changed", music ? "true" : "false"});
    Reset(state, music);
    return events;
}
}

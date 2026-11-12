#pragma once
// History recency and preference defaults adapted from IINA HistoryController.swift
// (Copyright © 2017 lhc) and Preference.swift. GPL-3.0; references/iina/LICENSE.
#include "../Playback/PlaybackSettings.h"
#include "../Input/BindingLibrary.h"
#include "../Filters/SavedFilter.h"
#include "../Audio/Equalizations.h"
#include "../Settings/PreferenceSchema.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iima {
struct UserSettings {
    settings::PreferenceValues advancedPreferences;
    std::vector<SavedFilter> savedVideoFilters, savedAudioFilters;
    UserEQProfiles::Profiles userEqualizers;
    // Missing means platform defaults; an empty custom file intentionally unbinds all.
    std::optional<std::string> inputConf;
    BindingLibrary bindingLibrary; // Named snapshots; inputConf remains the active copy.
    std::vector<std::string> watchProperties; // IINA default is an empty inspector watch list.
    double volume{100};
    double speed{1};
    bool recordHistory{true};
    bool resumePosition{true};
    bool pauseWhenOpen{false};
    bool autoPlayNext{true};
    bool muted{false};
    bool subtitlesVisible{true};
    double maxVolume{100}; // mpv volume-max range: 100..1000; default IINA 100.
    double audioDelay{0}; // Seconds; positive delays audio. Finite, immediate.
    double subtitleDelay{0}; // Seconds; positive delays subtitles. Finite, immediate.
    RepeatMode repeatMode{RepeatMode::Off};
    ScreenshotMode screenshotMode{ScreenshotMode::Subtitles};
};
inline bool ValidUserSettings(UserSettings const& s) {
    if (!ValidBindingLibrary(s.bindingLibrary)) return false;
    if (s.watchProperties.size() > 32) return false;
    for (auto const& name : s.watchProperties) {
        if (name.empty() || name.size() > 128) return false;
        for (unsigned char c : name) if (c <= 32 || c >= 127) return false;
    }
    return (!s.inputConf || (s.inputConf->size() <= 256 * 1024 && s.inputConf->find('\0') == std::string::npos)) &&
        std::isfinite(s.maxVolume) && s.maxVolume>=100 && s.maxVolume<=1000 &&
        std::isfinite(s.volume) && s.volume>=0 && s.volume<=s.maxVolume &&
        std::isfinite(s.speed) && s.speed>=0.01 && s.speed<=100 &&
        std::isfinite(s.audioDelay) && std::isfinite(s.subtitleDelay) &&
        (s.repeatMode==RepeatMode::Off || s.repeatMode==RepeatMode::File || s.repeatMode==RepeatMode::Playlist) &&
        (s.screenshotMode==ScreenshotMode::Subtitles || s.screenshotMode==ScreenshotMode::Video || s.screenshotMode==ScreenshotMode::Window);
}
struct HistoryEntry {
    std::string path;
    double position{};
    std::optional<double> duration;
    std::int64_t playedAt{}; // UTC Unix seconds
};
struct UserData {
    UserSettings settings;
    std::vector<HistoryEntry> history; // newest first, one entry per exact local path
    std::string warning;
    bool writable{true}; // False when loading failed before safe corruption preservation.
};
inline void RecordHistory(UserData& data, HistoryEntry entry) {
    if(!data.settings.recordHistory || entry.path.empty() || entry.path.find("://") != std::string::npos || !std::isfinite(entry.position)) return;
    entry.position=std::max(0.0,entry.position);
    if(entry.duration && (!std::isfinite(*entry.duration) || *entry.duration<0)) entry.duration.reset();
    if(entry.duration) entry.position=std::min(entry.position,*entry.duration);
    std::erase_if(data.history,[&](auto const& old) { return old.path==entry.path; });
    data.history.insert(data.history.begin(),std::move(entry));
    if(data.history.size()>1000) data.history.resize(1000);
}
inline std::optional<double> ResumePosition(UserData const& data, std::string const& path) {
    if(!data.settings.resumePosition) return {};
    auto found=std::find_if(data.history.begin(),data.history.end(),[&](auto const& e){return e.path==path;});
    // Completed media opens at the beginning, never at an unplayable EOF position.
    if(found==data.history.end() || found->position<=0 ||
       (found->duration && found->position>=*found->duration)) return {};
    return found->position;
}
}

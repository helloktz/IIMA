#pragma once
// History recency and preference defaults adapted from IINA HistoryController.swift
// (Copyright © 2017 lhc) and Preference.swift. GPL-3.0; references/iina/LICENSE.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iima {
struct UserSettings {
    double volume{100};
    double speed{1};
    bool recordHistory{true};
    bool resumePosition{true};
};
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
    if(!data.settings.recordHistory || entry.path.empty() || !std::isfinite(entry.position)) return;
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

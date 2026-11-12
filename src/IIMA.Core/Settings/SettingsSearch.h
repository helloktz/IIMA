// Adapted from IINA SettingsSearch.swift suffix/space-separated term matching.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "PreferenceSchema.h"
namespace iima::settings {
struct SettingsSearchEntry {
    std::string key;
    PreferenceArea area{};
    // UI supplies Unicode-lowercased UTF-8. The core also folds ASCII for
    // schema keys and mpv tokens. No values, paths or credentials are indexed.
    std::string searchable;
};
struct SettingsSearchResult {
    std::vector<std::size_t> indices;
    bool queryTooLarge{};
};
std::string_view PreferenceAreaTitle(PreferenceArea) noexcept;
// Every space/full-width-space separated term must occur in the entry's title
// or extra terms. Stable source order; no relevance guesses or external search.
SettingsSearchResult SearchSettings(std::span<SettingsSearchEntry const> entries,
    std::string_view query, std::optional<PreferenceArea> area,
    PreferenceValues const& overrides, bool modifiedOnly);
}

// IINA SettingsSearch.Entry/Trie semantics, bounded scan rather than suffix trie.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "SettingsSearch.h"
#include <algorithm>
namespace iima::settings {
std::string_view PreferenceAreaTitle(PreferenceArea area) noexcept {
    switch (area) {
    case PreferenceArea::Video: return "视频 Video";
    case PreferenceArea::Audio: return "音频 Audio";
    case PreferenceArea::Subtitles: return "字幕 Subtitles";
    case PreferenceArea::Window: return "窗口 Window";
    case PreferenceArea::Interface: return "界面 Interface";
    case PreferenceArea::Network: return "网络 Network";
    }
    return {};
}
namespace {
std::string FoldAscii(std::string value) {
    for (auto& c:value) if (c>='A' && c<='Z') c+='a'-'A';
    return value;
}
}
SettingsSearchResult SearchSettings(std::span<SettingsSearchEntry const> entries,
    std::string_view query,std::optional<PreferenceArea> area,
    PreferenceValues const& overrides,bool modifiedOnly) {
    SettingsSearchResult result;
    if (query.size()>4096 || entries.size()>1024 || query.find('\0')!=std::string_view::npos) {
        result.queryTooLarge=true; return result;
    }
    std::vector<std::string> terms;
    std::string term;
    for (std::size_t i=0;i<query.size();) {
        const bool fullWidth=query.substr(i,3)=="\xe3\x80\x80";
        const bool blank=fullWidth || query[i]==' ' || query[i]=='\t' || query[i]=='\n' || query[i]=='\r';
        if (blank) {
            if (!term.empty()) { terms.push_back(FoldAscii(std::move(term))); term.clear(); }
            i+=fullWidth?3:1;
        } else term.push_back(query[i++]);
        if (terms.size()>32) { result.queryTooLarge=true; return result; }
    }
    if (!term.empty()) terms.push_back(FoldAscii(std::move(term)));
    if (terms.size()>32) { result.queryTooLarge=true; return result; }
    for (std::size_t i=0;i<entries.size();++i) {
        auto const& entry=entries[i];
        if (area && entry.area!=*area) continue;
        if (modifiedOnly && !overrides.contains(entry.key)) continue;
        auto searchable=FoldAscii(entry.searchable);
        if (std::all_of(terms.begin(),terms.end(),[&](auto const& t) { return searchable.find(t)!=std::string::npos; }))
            result.indices.push_back(i);
    }
    return result;
}
}

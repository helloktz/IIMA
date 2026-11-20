// IINA AutoFileMatcher.getAllPossibleSubs, Copyright (c) 2017 lhc.
// GPL-3.0; references/iina/LICENSE.
#include "SubtitleSearchPath.h"
#include <algorithm>
#include <utility>
namespace iima {
std::optional<std::vector<SubtitleSearchPath>> ParseSubtitleSearchPaths(
    std::wstring_view text, std::wstring& error) {
    error.clear();
    auto reject=[&](wchar_t const* reason)->std::optional<std::vector<SubtitleSearchPath>> {
        error=reason; return std::nullopt;
    };
    if (text.size()>8192 || text.find(L'\0')!=text.npos)
        return reject(L"TheSubtitleSearchPathIsTooLong");
    std::vector<SubtitleSearchPath> result;
    for (std::size_t start=0; start<text.size();) {
        auto end=text.find(L';',start);
        auto item=text.substr(start,end==text.npos ? text.size()-start : end-start);
        start=end==text.npos ? text.size() : end+1;
        auto first=item.find_first_not_of(L" \t\r\n"), last=item.find_last_not_of(L" \t\r\n");
        if (first==item.npos) continue;
        std::wstring path(item.substr(first,last-first+1));
        std::replace(path.begin(),path.end(),L'/',L'\\');
        const bool children=path.ends_with(L"\\*");
        if (children) path.resize(path.size()-1); // Keep separator: C:/* remains C:\, not C:.
        if (std::any_of(path.begin(),path.end(),[](wchar_t c) { return c<32; }) ||
            path.empty() || path.front()==L'~' || path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\.\\") ||
            path.find_first_of(L"*?\"<>|\r\n\t")!=path.npos)
            return reject(L"SubtitleSearchPathsCannotUseDeviceNamespaces");
        const auto colon=path.find(L':');
        if (colon!=path.npos && (colon!=1 || path.size()<3 || path[2]!=L'\\' ||
            !((path[0]>=L'A' && path[0]<=L'Z') || (path[0]>=L'a' && path[0]<=L'z')) ||
            path.find(L':',2)!=path.npos))
            return reject(L"UseARelativeOrFullyQualifiedSubtitle");
        if (path.front()==L'\\' && !path.starts_with(L"\\\\"))
            return reject(L"UseAFullAbsolutePathIncludingThe");
        if (path.starts_with(L"\\\\")) {
            const auto serverEnd=path.find(L'\\',2);
            const auto shareStart=serverEnd==path.npos ? path.size() : serverEnd+1;
            const auto shareEnd=path.find(L'\\',shareStart);
            if (serverEnd==path.npos || serverEnd==2 || shareStart==path.size() || shareEnd==shareStart)
                return reject(L"AUNCSubtitlePathMustIncludeThe");
        }
        if (result.size()==32) return reject(L"YouCanConfigureUpTo32Subtitle");
        result.push_back({std::move(path),children});
    }
    return result;
}
}

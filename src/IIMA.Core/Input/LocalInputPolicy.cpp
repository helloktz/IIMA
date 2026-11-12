#include "LocalInputPolicy.h"
#include <algorithm>
namespace iima {
namespace {
wchar_t Fold(wchar_t c) { return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c; }
std::wstring Extension(std::wstring_view path) {
    auto dot = path.find_last_of(L'.');
    auto slash = path.find_last_of(L"/\\");
    if (dot == path.npos || (slash != path.npos && dot < slash)) return {};
    std::wstring result{path.substr(dot + 1)};
    std::transform(result.begin(), result.end(), result.begin(), Fold);
    return result;
}
bool Contains(std::wstring_view table, std::wstring const& value) {
    return table.find(L"|" + value + L"|") != table.npos;
}
constexpr auto playlists = L"|cue|m3u|m3u8|pls|";
constexpr auto blacklist = L"|utf|utf8|utf-8|idx|sub|srt|smi|rt|ssa|aqt|jss|js|ass|mks|vtt|sup|scc|lrc|m3u|m3u8|pls|";
// Existing IINA Utility-derived discovery table, separated to retain its
// video-before-audio automatic playlist order without duplicating the policy.
constexpr auto video = L"|mkv|mp4|avi|m4v|mov|3gp|ts|mts|m2ts|wmv|flv|f4v|asf|webm|rm|rmvb|qt|dv|mpg|mpeg|mxf|vob|gif|ogv|ogm|";
constexpr auto audio = L"|mp3|aac|mka|dts|flac|ogg|oga|mogg|m4a|ac3|opus|wav|wv|aiff|aif|ape|tta|tak|";
bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
// Numeric runs are compared without integer conversion or overflow. Unicode
// non-ASCII code units remain ordinal: intentionally not macOS locale collation.
int Natural(std::wstring_view a, std::wstring_view b) {
    std::size_t i{}, j{};
    while (i < a.size() && j < b.size()) {
        if (Digit(a[i]) && Digit(b[j])) {
            auto ae=i, be=j; while(ae<a.size() && Digit(a[ae])) ++ae; while(be<b.size() && Digit(b[be])) ++be;
            auto az=i, bz=j; while(az<ae && a[az]==L'0') ++az; while(bz<be && b[bz]==L'0') ++bz;
            if(ae-az != be-bz) return ae-az < be-bz ? -1 : 1;
            auto compare=a.substr(az,ae-az).compare(b.substr(bz,be-bz));
            if(compare) return compare<0 ? -1 : 1;
            i=ae; j=be; continue;
        }
        auto x=Fold(a[i]), y=Fold(b[j]);
        if(x!=y) return x<y ? -1 : 1;
        ++i; ++j;
    }
    if(i!=a.size() || j!=b.size()) return i==a.size() ? -1 : 1;
    return a.compare(b); // stable total order for equal numbers/case variants
}
}
bool IsPlaylistPath(std::wstring_view path) { return Contains(playlists, Extension(path)); }
bool AcceptExplicitFile(std::wstring_view path) { return !Contains(blacklist, Extension(path)); }
DiscoveredMediaType ClassifyDiscoveredMedia(std::wstring_view path) {
    const auto extension=Extension(path);
    if (Contains(video,extension)) return DiscoveredMediaType::Video;
    if (Contains(audio,extension)) return DiscoveredMediaType::Audio;
    return DiscoveredMediaType::None;
}
bool AcceptDiscoveredFile(std::wstring_view path) { return ClassifyDiscoveredMedia(path)!=DiscoveredMediaType::None; }
void SortUniquePaths(std::vector<std::wstring>& paths) {
    std::sort(paths.begin(),paths.end(),[](auto const& a, auto const& b) {
        auto ai=a.find_last_of(L"/\\"), bi=b.find_last_of(L"/\\");
        std::wstring_view av=a,bv=b;
        auto ad=ai==a.npos ? std::wstring_view{} : av.substr(0,ai);
        auto bd=bi==b.npos ? std::wstring_view{} : bv.substr(0,bi);
        if(ad!=bd) return ad<bd;
        return Natural(av.substr(ai==a.npos ? 0 : ai+1),bv.substr(bi==b.npos ? 0 : bi+1))<0;
    });
    paths.erase(std::unique(paths.begin(),paths.end()),paths.end());
}
}

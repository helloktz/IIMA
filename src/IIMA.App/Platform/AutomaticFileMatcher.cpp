// IINA AutoFileMatcher.startMatching/getAllMediaFiles and PlayerCore auto-load.
// Copyright (c) 2017 lhc. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "AutomaticFileMatcher.h"
#include "../../IIMA.Core/Input/LocalInputPolicy.h"
#include "../../IIMA.Core/Matching/SubtitleSearchPath.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include <iterator>

namespace iima {
namespace {
namespace fs=std::filesystem;
std::atomic_uint activeScans{};
bool Subtitle(fs::path const& path) {
    auto extension=path.extension().native();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c) {
        return c>=L'A' && c<=L'Z' ? static_cast<wchar_t>(c+L'a'-L'A') : c;
    });
    // IINA Utility.supportedFileExt[.sub], also used by explicit local matching.
    constexpr std::wstring_view types[]{L".utf",L".utf8",L".utf-8",L".idx",L".sub",L".srt",L".smi",
        L".rt",L".ssa",L".aqt",L".jss",L".js",L".ass",L".mks",L".vtt",L".sup",L".scc",L".lrc"};
    return std::find(std::begin(types),std::end(types),extension)!=std::end(types);
}
bool SamePath(fs::path const& a,fs::path const& b) {
    auto const& x=a.native(); auto const& y=b.native();
    return CompareStringOrdinal(x.c_str(),static_cast<int>(x.size()),y.c_str(),static_cast<int>(y.size()),TRUE)==CSTR_EQUAL;
}
// Reject links/hidden ancestors as well as the final directory. A UNC server
// root is not an enumerable directory; stop after validating its share root.
int SafeDirectory(fs::path const& path,std::stop_token stop) {
    for (auto part=path; !part.empty();) {
        if (stop.stop_requested()) return 0;
        if (part==part.root_path() && part.native().starts_with(L"\\\\")) break;
        auto attr=GetFileAttributesW(part.c_str());
        if (attr==INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return -1;
        auto name=part.filename().native();
        if ((attr & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_DEVICE)) ||
            (!name.empty() && name.front()==L'.' && name!=L"." && name!=L"..")) return 0;
        auto parent=part.parent_path();
        if (parent==part) break;
        part=std::move(parent);
    }
    return 1;
}
AutomaticFileMatchResult Scan(fs::path current, AutomaticFileMatchOptions options,
    std::uint64_t generation,std::stop_token stop) {
    AutomaticFileMatchResult result; result.current=current; result.generation=generation;
    auto reject=[&](wchar_t const* text) {
        result.orderedMedia.clear(); result.matches={}; result.complete=false;
        result.warnings.emplace_back(text);
        return std::move(result);
    };
    auto text=current.native();
    std::replace(text.begin(),text.end(),L'/',L'\\');
    if (text.empty() || text.size()>32767 || text.find(L'\0')!=text.npos ||
        text.starts_with(L"\\\\?\\") || text.starts_with(L"\\\\.\\") || text.find(L':',2)!=text.npos ||
        !current.is_absolute() || !AcceptDiscoveredFile(text) || IsPlaylistPath(text))
        return reject(L"自动匹配仅支持显式打开的普通本地媒体文件。");
    current=current.lexically_normal(); result.current=current;
    if (SafeDirectory(current.parent_path(),stop)!=1)
        return reject(L"媒体目录不可读或包含隐藏/链接目录，未应用自动匹配。");
    auto mediaAttributes=GetFileAttributesW(current.c_str());
    auto folderAttributes=GetFileAttributesW(current.parent_path().c_str());
    if (mediaAttributes==INVALID_FILE_ATTRIBUTES || folderAttributes==INVALID_FILE_ATTRIBUTES ||
        (mediaAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE|FILE_ATTRIBUTE_HIDDEN)) ||
        !(folderAttributes & FILE_ATTRIBUTE_DIRECTORY) || (folderAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return reject(L"媒体或目录不可用，或是隐藏/链接目标；未自动扩展播放列表。");
    std::vector<std::wstring> videos,audios,subtitles;
    std::error_code error;
    fs::directory_iterator iterator(current.parent_path(),error), end;
    std::size_t visited{}, retainedBytes{}, currentMatches{};
    for (;!error && iterator!=end && !stop.stop_requested();iterator.increment(error)) {
        if (++visited>10000) return reject(L"目录超过自动匹配的10000项扫描限制，请手动打开文件。");
        auto path=iterator->path().lexically_normal(); auto name=path.filename().native();
        const auto attributes=GetFileAttributesW(path.c_str());
        if (attributes==INVALID_FILE_ATTRIBUTES) return reject(L"读取同目录文件属性失败，未应用不完整匹配结果。");
        if ((attributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE|FILE_ATTRIBUTE_HIDDEN)) ||
            (!name.empty() && name.front()==L'.')) continue;
        const bool regular=iterator->is_regular_file(error);
        if (error) return reject(L"无法确认文件类型，未应用不完整匹配结果。");
        if (!regular) continue;
        const auto type=ClassifyDiscoveredMedia(name);
        const bool subtitle=Subtitle(path);
        if (type==DiscoveredMediaType::None && !subtitle) continue;
        retainedBytes+=path.native().size()*sizeof(wchar_t);
        if (retainedBytes>1024*1024) return reject(L"自动匹配路径数据超过1 MiB限制，请手动打开文件。");
        if (type!=DiscoveredMediaType::None) {
            if (SamePath(path,current)) { ++currentMatches; path=current; }
            (type==DiscoveredMediaType::Video ? videos : audios).push_back(path.native());
            if (videos.size()+audios.size()>256) return reject(L"同目录媒体超过256个，未自动扩展播放列表。");
        } else {
            subtitles.push_back(path.native());
            if (subtitles.size()>512) return reject(L"同目录字幕超过512个，未应用自动匹配结果。");
        }
    }
    if (stop.stop_requested()) { result.cancelled=true; return result; }
    if (error) return reject(L"无法完整读取媒体目录，未应用自动匹配结果。");
    if (currentMatches!=1) return reject(L"目录中无法唯一确认当前媒体，未应用自动匹配结果。");
    // Parse even when loading is disabled: invalid persisted settings must not
    // silently become a different search. Directory I/O is skipped in that mode.
    std::wstring parseError;
    auto searches=ParseSubtitleSearchPaths(options.subtitleSearchPaths,parseError);
    if (!searches) return reject(parseError.c_str());
    std::vector<fs::path> scanned{current.parent_path()}, expanded;
    auto contains=[&](auto const& paths,fs::path const& path) {
        return std::any_of(paths.begin(),paths.end(),[&](auto const& value) { return SamePath(value,path); });
    };
    auto addDirectory=[&](fs::path path) {
        path=path.lexically_normal();
        // Remove a trailing separator so X:\Subs and X:\Subs\ deduplicate.
        if (path.filename().empty() && path!=path.root_path()) path=path.parent_path();
        if (contains(scanned,path)) return true;
        if (scanned.size()+expanded.size()>=64 || path.native().size()>32767) return false;
        const auto safe=SafeDirectory(path,stop);
        if (safe<0) return false;
        if (safe==0) return true;
        retainedBytes+=path.native().size()*sizeof(wchar_t);
        if (retainedBytes>1024*1024) return false;
        scanned.push_back(std::move(path));
        return true;
    };
    if (options.subtitles.mode!=SubtitleAutoLoad::Disabled) {
        for (auto const& search:*searches) {
            if (stop.stop_requested()) { result.cancelled=true; return result; }
            fs::path directory(search.directory);
            if (!directory.is_absolute()) directory=current.parent_path()/directory;
            directory=directory.lexically_normal();
            if (directory.filename().empty() && directory!=directory.root_path()) directory=directory.parent_path();
            if (!search.immediateChildren) {
                if (!addDirectory(directory)) return reject(L"字幕搜索目录不可读或超过安全限制，未应用结果。");
                continue;
            }
            if (contains(expanded,directory)) continue;
            if (scanned.size()+expanded.size()>=64 || directory.native().size()>32767)
                return reject(L"字幕搜索目录超过64项或路径长度限制，未应用结果。");
            const auto safe=SafeDirectory(directory,stop);
            if (safe<0) return reject(L"无法读取字幕搜索目录，未应用不完整结果。");
            if (safe==0) continue;
            expanded.push_back(directory);
            fs::directory_iterator children(directory,error);
            for (;!error && children!=end && !stop.stop_requested();children.increment(error)) {
                if (++visited>10000) return reject(L"字幕搜索超过10000项访问限制，未应用结果。");
                auto child=children->path(); auto name=child.filename().native();
                auto attr=GetFileAttributesW(child.c_str());
                if (attr==INVALID_FILE_ATTRIBUTES) return reject(L"读取字幕搜索子目录属性失败，未应用结果。");
                if ((attr & (FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) ||
                    (!name.empty() && name.front()==L'.') || !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (!addDirectory(child)) return reject(L"字幕搜索子目录不可读或超过安全限制，未应用结果。");
            }
            if (error) return reject(L"无法完整枚举字幕搜索子目录，未应用结果。");
        }
        // Index zero was already scanned for both media and subtitles above.
        for (std::size_t i=1;i<scanned.size() && !stop.stop_requested();++i) {
            fs::directory_iterator files(scanned[i],error);
            for (;!error && files!=end && !stop.stop_requested();files.increment(error)) {
                if (++visited>10000) return reject(L"字幕搜索超过10000项访问限制，未应用结果。");
                auto path=files->path().lexically_normal(); auto name=path.filename().native();
                auto attr=GetFileAttributesW(path.c_str());
                if (attr==INVALID_FILE_ATTRIBUTES) return reject(L"读取字幕文件属性失败，未应用结果。");
                if ((attr & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) ||
                    (!name.empty() && name.front()==L'.') || !Subtitle(path)) continue;
                const bool regular=files->is_regular_file(error);
                if (error) return reject(L"无法确认字幕文件类型，未应用结果。");
                if (!regular) continue;
                if (std::any_of(subtitles.begin(),subtitles.end(),[&](auto const& value) { return SamePath(fs::path(value),path); })) continue;
                retainedBytes+=path.native().size()*sizeof(wchar_t);
                if (retainedBytes>1024*1024 || subtitles.size()>=512)
                    return reject(L"字幕搜索超过512个字幕或1 MiB路径限制，未应用结果。");
                subtitles.push_back(path.native());
            }
            if (error) return reject(L"无法完整读取字幕搜索目录，未应用结果。");
        }
    }
    if (stop.stop_requested()) { result.cancelled=true; return result; }
    SortUniquePaths(videos); SortUniquePaths(audios); SortUniquePaths(subtitles);
    for (auto const& path:videos) result.orderedMedia.emplace_back(path);
    for (auto const& path:audios) result.orderedMedia.emplace_back(path);
    std::vector<fs::path> subtitlePaths;
    for (auto const& path:subtitles) subtitlePaths.emplace_back(path);
    // Upstream gates mutual-nearest fallback with playlistAutoAdd. Other
    // configured subtitle matching remains available without queue expansion.
    options.subtitles.forceMatchUnmatched=options.subtitles.forceMatchUnmatched && options.expandPlaylist;
    result.matches=MatchSubtitles(result.orderedMedia,subtitlePaths,options.subtitles,generation,
        [stop] { return stop.stop_requested(); });
    result.cancelled=stop.stop_requested() || result.matches.cancelled;
    if (result.cancelled) { result.orderedMedia.clear(); result.matches={}; return result; }
    if (result.matches.forceMatchLimitReached)
        result.warnings.emplace_back(L"模糊字幕配对达到比较上限，仅保留已完成的规则匹配。");
    result.complete=true;
    return result;
}
}
std::stop_source AutomaticFileMatcher::Discover(fs::path current, AutomaticFileMatchOptions options,
    std::uint64_t generation,winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
    std::function<void(AutomaticFileMatchResult)> completion) {
    std::stop_source source;
    if (activeScans.fetch_add(1)>=4) {
        activeScans.fetch_sub(1);
        AutomaticFileMatchResult result; result.current=std::move(current); result.generation=generation;
        result.warnings.emplace_back(L"自动匹配后台任务忙，未启动额外目录扫描。");
        dispatcher.TryEnqueue([token=source.get_token(),completion=std::move(completion),result=std::move(result)]() mutable {
            try { if (!token.stop_requested() && completion) completion(std::move(result)); } catch (...) {}
        });
        return source;
    }
    try {
        std::thread([current=std::move(current),options=std::move(options),generation,
            dispatcher=std::move(dispatcher),completion=std::move(completion),token=source.get_token()]() mutable {
            struct Completed { ~Completed() { activeScans.fetch_sub(1); } } completed;
            AutomaticFileMatchResult result; result.current=current; result.generation=generation;
            bool apartment{};
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment=true;
                result=Scan(std::move(current),std::move(options),generation,token);
            } catch (...) { result.warnings.emplace_back(L"自动发现或字幕匹配失败，未应用结果。"); }
            if (!token.stop_requested()) try {
                dispatcher.TryEnqueue([token,completion=std::move(completion),result=std::move(result)]() mutable {
                    try { if (!token.stop_requested() && completion) completion(std::move(result)); } catch (...) {}
                });
            } catch (...) { /* Dispatcher closed: discard owned result. */ }
            if (apartment) winrt::uninit_apartment();
        }).detach();
    } catch (...) { activeScans.fetch_sub(1); throw; }
    return source;
}
}

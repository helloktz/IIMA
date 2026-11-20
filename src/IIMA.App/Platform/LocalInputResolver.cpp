#include "pch.h"
#include "LocalInputResolver.h"
#include "../../IIMA.Core/Input/LocalInputPolicy.h"
#include <filesystem>
#include <algorithm>
#include <thread>

namespace iima {
namespace {
namespace fs = std::filesystem;
LocalInputResult Enumerate(std::vector<std::wstring> const& inputs, std::stop_token stop, bool regularMediaOnly) {
    LocalInputResult result;
    std::vector<std::wstring> files;
    auto warn = [&](std::wstring const& path, wchar_t const* message) {
        // Bound diagnostics as well as candidates; never silently drop the first issue.
        if(result.warnings.size()<20) result.warnings.push_back(path + L": " + message);
    };
    for(auto const& input : inputs) {
        if(stop.stop_requested()) break;
        auto checked=input;
        std::replace(checked.begin(),checked.end(),L'/',L'\\');
        if(input.empty() || input.find(L'\0')!=input.npos || checked.starts_with(L"\\\\?\\") || checked.starts_with(L"\\\\.\\")) {
            warn(iima::loc::Text(L"Input"),iima::loc::Text(L"RejectEmptyPathsEmbeddedNULOrDevice")); continue;
        }
        if(input.find(L':',2)!=input.npos) { warn(iima::loc::Text(L"Input"),iima::loc::Text(L"RejectURIOrBackupDataStreamPath")); continue; }
        std::error_code error;
        auto root=fs::absolute(fs::path(input),error).lexically_normal();
        if(error || !root.is_absolute()) { warn(input,iima::loc::Text(L"UnableToParseAbsolutePath")); continue; }
        auto attributes=GetFileAttributesW(root.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES) { warn(input,iima::loc::Text(L"UnableToAccessPath")); continue; }
        if (regularMediaOnly && ((attributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) ||
            IsPlaylistPath(root.native()) || !fs::is_regular_file(root,error) || error)) {
            warn(input,iima::loc::Text(L"HistoryTargetIsNoLongerARegular")); continue;
        }
        if(!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // IINA openURLs passes a single source playlist straight to mpv.
            if((inputs.size()==1 && IsPlaylistPath(root.native())) || AcceptExplicitFile(root.native())) {
                files.push_back(root.native());
                result.singleExplicitMedia=inputs.size()==1 && AcceptDiscoveredFile(root.native()) &&
                    !IsPlaylistPath(root.native()) && !(attributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) &&
                    fs::is_regular_file(root,error) && !error;
            }
            else warn(input,iima::loc::Text(L"NotAnOpenMediaFileSubtitlesUsed"));
            continue;
        }
        if(attributes & FILE_ATTRIBUTE_REPARSE_POINT) { warn(input,iima::loc::Text(L"ToAvoidLoopsDoNotGoThrough")); continue; }
        std::vector<std::pair<fs::path,unsigned>> pending{{root,0}};
        while(!pending.empty() && !stop.stop_requested()) {
            auto [directory,depth]=std::move(pending.back()); pending.pop_back();
            fs::directory_iterator iterator(directory,error), end;
            if(error) { warn(directory.native(),iima::loc::Text(L"CouldNotReadTheFolder")); error.clear(); continue; }
            for(;iterator!=end && !stop.stop_requested();iterator.increment(error)) {
                if(error) { warn(directory.native(),iima::loc::Text(L"SynchronisingS")); error.clear(); break; }
                auto path=iterator->path();
                const auto name=path.filename().native();
                auto attr=GetFileAttributesW(path.c_str());
                if(attr==INVALID_FILE_ATTRIBUTES) { warn(path.native(),iima::loc::Text(L"CouldNotReadFileProperties")); continue; }
                // Windows hidden attributes and dot folders are consistently skipped
                // at every depth, unlike IINA's relative-prefix-only check.
                if((attr & FILE_ATTRIBUTE_HIDDEN) || (!name.empty() && name[0]==L'.')) continue;
                if(attr & FILE_ATTRIBUTE_DIRECTORY) {
                    if(attr & FILE_ATTRIBUTE_REPARSE_POINT) { warn(path.native(),iima::loc::Text(L"SkippedLinkFolder")); continue; }
                    if(depth>=64) { warn(path.native(),iima::loc::Text(L"FileDepthLimitHasBeenReached")); continue; }
                    pending.emplace_back(std::move(path),depth+1);
                } else if(AcceptDiscoveredFile(name)) files.push_back(path.native());
                if(files.size()+pending.size()>=100000) {
                    warn(root.native(),iima::loc::Text(L"SelectASmallerFolderIfTheDirectory"));
                    files.clear(); pending.clear(); return result;
                }
            }
            if(error) { warn(directory.native(),iima::loc::Text(L"SynchronisingS")); error.clear(); }
        }
    }
    if(stop.stop_requested()) { result.cancelled=true; return result; }
    SortUniquePaths(files);
    for(auto const& file:files) result.paths.push_back(winrt::to_string(file));
    if(result.paths.empty() && result.warnings.empty()) result.warnings.push_back(iima::loc::Text(L"NoMediaFilesAreAvailableForPlay"));
    return result;
}
}
std::stop_source LocalInputResolver::Resolve(std::vector<std::wstring> inputs,
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
    std::function<void(LocalInputResult)> completion, bool regularMediaOnly) {
    std::stop_source source;
    std::thread([inputs=std::move(inputs),dispatcher=std::move(dispatcher),completion=std::move(completion),token=source.get_token(),regularMediaOnly]() mutable {
        LocalInputResult result;
        bool apartment{};
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment=true; result=Enumerate(inputs,token,regularMediaOnly); }
        catch(...) { result.warnings.push_back(iima::loc::Text(L"ThisPostIsPartOfOurSpecial")); }
        if(token.stop_requested()) { if(apartment) winrt::uninit_apartment(); return; }
        try {
            dispatcher.TryEnqueue([token,completion=std::move(completion),result=std::move(result)]() mutable {
                try { if(!token.stop_requested() && completion) completion(std::move(result)); } catch(...) {}
            });
        } catch(...) { /* Dispatcher shutdown: drop owned result. */ }
        if(apartment) winrt::uninit_apartment();
    }).detach();
    return source;
}
}

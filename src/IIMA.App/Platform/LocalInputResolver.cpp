#include "pch.h"
#include "LocalInputResolver.h"
#include "../../IIMA.Core/Input/LocalInputPolicy.h"
#include <filesystem>
#include <algorithm>
#include <thread>

namespace iima {
namespace {
namespace fs = std::filesystem;
LocalInputResult Enumerate(std::vector<std::wstring> const& inputs, std::stop_token stop) {
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
            warn(L"输入",L"拒绝空路径、嵌入 NUL 或设备命名空间"); continue;
        }
        if(input.find(L':',2)!=input.npos) { warn(L"输入",L"拒绝 URI 或备用数据流路径"); continue; }
        std::error_code error;
        auto root=fs::absolute(fs::path(input),error).lexically_normal();
        if(error || !root.is_absolute()) { warn(input,L"无法解析绝对路径"); continue; }
        auto attributes=GetFileAttributesW(root.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES) { warn(input,L"无法访问路径"); continue; }
        if(!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // IINA openURLs passes a single source playlist straight to mpv.
            if((inputs.size()==1 && IsPlaylistPath(root.native())) || AcceptExplicitFile(root.native())) {
                files.push_back(root.native());
                result.singleExplicitMedia=inputs.size()==1 && AcceptDiscoveredFile(root.native()) &&
                    !IsPlaylistPath(root.native()) && !(attributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) &&
                    fs::is_regular_file(root,error) && !error;
            }
            else warn(input,L"不是可打开的媒体文件；字幕请使用添加字幕操作");
            continue;
        }
        if(attributes & FILE_ATTRIBUTE_REPARSE_POINT) { warn(input,L"为避免循环，不遍历链接文件夹"); continue; }
        std::vector<std::pair<fs::path,unsigned>> pending{{root,0}};
        while(!pending.empty() && !stop.stop_requested()) {
            auto [directory,depth]=std::move(pending.back()); pending.pop_back();
            fs::directory_iterator iterator(directory,error), end;
            if(error) { warn(directory.native(),L"无法读取文件夹"); error.clear(); continue; }
            for(;iterator!=end && !stop.stop_requested();iterator.increment(error)) {
                if(error) { warn(directory.native(),L"遍历文件夹失败"); error.clear(); break; }
                auto path=iterator->path();
                const auto name=path.filename().native();
                auto attr=GetFileAttributesW(path.c_str());
                if(attr==INVALID_FILE_ATTRIBUTES) { warn(path.native(),L"无法读取文件属性"); continue; }
                // Windows hidden attributes and dot folders are consistently skipped
                // at every depth, unlike IINA's relative-prefix-only check.
                if((attr & FILE_ATTRIBUTE_HIDDEN) || (!name.empty() && name[0]==L'.')) continue;
                if(attr & FILE_ATTRIBUTE_DIRECTORY) {
                    if(attr & FILE_ATTRIBUTE_REPARSE_POINT) { warn(path.native(),L"已跳过链接文件夹"); continue; }
                    if(depth>=64) { warn(path.native(),L"已达到文件夹深度限制"); continue; }
                    pending.emplace_back(std::move(path),depth+1);
                } else if(AcceptDiscoveredFile(name)) files.push_back(path.native());
                if(files.size()+pending.size()>=100000) {
                    warn(root.native(),L"目录超过安全枚举限制，请选择较小的文件夹");
                    files.clear(); pending.clear(); return result;
                }
            }
            if(error) { warn(directory.native(),L"遍历文件夹失败"); error.clear(); }
        }
    }
    if(stop.stop_requested()) { result.cancelled=true; return result; }
    SortUniquePaths(files);
    for(auto const& file:files) result.paths.push_back(winrt::to_string(file));
    if(result.paths.empty() && result.warnings.empty()) result.warnings.push_back(L"所选位置没有可播放的媒体文件。");
    return result;
}
}
std::stop_source LocalInputResolver::Resolve(std::vector<std::wstring> inputs,
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
    std::function<void(LocalInputResult)> completion) {
    std::stop_source source;
    std::thread([inputs=std::move(inputs),dispatcher=std::move(dispatcher),completion=std::move(completion),token=source.get_token()]() mutable {
        LocalInputResult result;
        bool apartment{};
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment=true; result=Enumerate(inputs,token); }
        catch(...) { result.warnings.push_back(L"枚举本地媒体失败。"); }
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

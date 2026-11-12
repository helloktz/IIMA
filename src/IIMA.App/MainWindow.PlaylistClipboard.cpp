// IINA SidebarPlaylistPane.copyToPasteboard/pasteFromPasteboard/contextCopyURL.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <algorithm>
using namespace winrt;
using namespace Windows::ApplicationModel::DataTransfer;
namespace winrt::IIMA::implementation {
void MainWindow::WirePlaylistClipboard() {
    auto weak = get_weak();
    CopyPlaylistPathsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CopyPlaylistPaths(false);
    });
    CopyPlaylistUrlsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CopyPlaylistPaths(true);
    });
    PastePlaylistPathsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->PastePlaylistPaths();
    });
}
void MainWindow::CopyPlaylistPaths(bool networkOnly) {
    if (closing_) return;
    try {
        auto rows = SelectedPlaylistRows();
        std::string text;
        std::size_t count{};
        for (auto row : rows) {
            auto const& path = playlist_[row].path;
            if (networkOnly && !iima::IsNetworkMediaPath(path)) continue;
            if (path.empty() || path.find_first_of("\r\n") != std::string::npos ||
                path.find('\0') != std::string::npos) {
                ShowError(L"条目包含不能无损复制为单行文本的路径，未修改剪贴板。"); return;
            }
            if (++count > 1000 || text.size() + path.size() + 2 > 1024 * 1024) {
                ShowError(L"每次最多复制1000项、1MiB文本。"); return;
            }
            if (!text.empty()) text += "\r\n";
            text += path;
        }
        if (text.empty()) { ShowError(L"没有可复制的所选条目。"); return; }
        DataPackage package;
        package.RequestedOperation(DataPackageOperation::Copy);
        package.SetText(to_hstring(text));
        ClipboardContentOptions options;
        options.IsAllowedInHistory(false);
        options.IsRoamable(false);
        if (!Clipboard::SetContentWithOptions(package, options)) {
            ShowError(L"剪贴板正忙，未复制。请重试。"); return;
        }
        // No Flush: retain neither paths nor signed URLs in clipboard history.
        StatusText().Text(L"已复制所选路径文本。网络地址可能包含私密参数，请谨慎粘贴。");
    } catch (...) { ShowError(L"无法写入剪贴板。"); }
}
fire_and_forget MainWindow::PastePlaylistPaths() {
    auto lifetime = get_strong();
    if (closing_ || playlistPasteBusy_ || session_->PlaylistEditBusy() ||
        session_->Snapshot().session != iima::SessionState::Ready) co_return;
    auto rows = SelectedPlaylistRows();
    if (rows.empty() && !SelectedPlaylistIds().empty()) co_return;
    auto revision = session_->Snapshot().playlistRevision;
    auto epoch = navigationEpoch_;
    // Match IINA: insert before the first selected row, or at the beginning.
    std::size_t destination = rows.empty() ? 0 : rows.front();
    playlistPasteBusy_ = true;
    try {
        auto data = Clipboard::GetContent();
        if (!data.Contains(StandardDataFormats::Text())) {
            ShowError(L"剪贴板没有路径文本。文件对象请使用打开或拖放。");
        } else {
            auto raw = co_await data.GetTextAsync();
            if (!closing_) {
                if (epoch != navigationEpoch_ || revision != session_->Snapshot().playlistRevision ||
                    session_->PlaylistEditBusy()) {
                    ShowError(L"读取剪贴板期间播放列表已改变，请重新粘贴。");
                } else {
                    auto text = to_string(raw);
                    std::vector<std::string> paths;
                    bool valid = text.size() <= 1024 * 1024;
                    for (std::size_t offset = 0; valid && offset < text.size();) {
                        auto end = text.find('\n', offset);
                        auto line = text.substr(offset, end == std::string::npos ? end : end - offset);
                        offset = end == std::string::npos ? text.size() : end + 1;
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.empty()) continue;
                        if (std::any_of(line.begin(), line.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
                            valid = false; break;
                        }
                        if (iima::IsNetworkMediaPath(line)) {
                            auto prepared = iima::PrepareMediaUrl(line);
                            if (!prepared) { valid = false; break; }
                            Windows::Foundation::Uri uri(to_hstring(prepared->value));
                            if (uri.Host().empty()) { valid = false; break; }
                            line = to_string(uri.AbsoluteUri());
                        } else {
                            std::filesystem::path path(to_hstring(line).c_str());
                            // Never interpret relative paths against the process directory,
                            // and do not allow shell protocols or mpv pseudo-protocols.
                            if (!path.is_absolute() || line.starts_with("\\\\?\\") ||
                                line.starts_with("\\\\.\\")) { valid = false; break; }
                        }
                        paths.push_back(std::move(line));
                        if (paths.size() > 1000) valid = false;
                    }
                    if (!valid || paths.empty()) {
                        ShowError(L"请输入每行一个绝对文件路径或支持的网络URL（最多1000项/1MiB），未添加任何条目。");
                    } else {
                        session_->InsertPlaylist(std::move(paths), destination);
                        // Queued is not loaded: playlist events and command errors own feedback.
                    }
                }
            }
        }
    } catch (...) { if (!closing_) ShowError(L"无法完成剪贴板粘贴，请查看当前列表及播放器错误。"); }
    playlistPasteBusy_ = false;
}
}

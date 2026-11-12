// IINA SidebarPlaylistPane.copyToPasteboard/pasteFromPasteboard/contextCopyURL.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <algorithm>
#include <winrt/Windows.Storage.h>
using namespace winrt;
using namespace Windows::ApplicationModel::DataTransfer;
namespace winrt::IIMA::implementation {
void MainWindow::WirePlaylistClipboard() {
    auto weak = get_weak();
    CancelPlaylistPasteButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) {
            self->playlistPasteStop_.request_stop();
            ++self->playlistPasteGeneration_;
            self->playlistPasteBusy_ = false;
            self->StatusText().Text(L"已取消待处理粘贴；已经提交的播放器操作不受影响。");
        }
    });
    CutPlaylistPathsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CopyPlaylistPaths(false,true);
    });
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
void MainWindow::CopyPlaylistPaths(bool networkOnly, bool cut) {
    if ((cut && networkOnly) || closing_ || (cut && (pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ ||
        inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty()))) return;
    bool copied=false;
    try {
        auto rows = SelectedPlaylistRows();
        auto const& snapshot=session_->Snapshot();
        const auto queue=snapshot.playlist;
        const auto revision=snapshot.playlistRevision, media=snapshot.mediaGeneration, intent=snapshot.mediaOpenIntentRevision;
        const auto edit=session_->PlaylistEditIntentRevision(), input=inputGeneration_;
        const auto selected=SelectedPlaylistIds();
        std::string text;
        std::size_t count{};
        for (auto row : rows) {
            auto const& path = queue[row].path;
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
        copied=true;
        if (cut) {
            auto const& now=session_->Snapshot();
            if (closing_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ || inputResolving_ ||
                !pendingFiles_.empty() || !activationInputs_.empty() ||
                now.session!=iima::SessionState::Ready || session_->PlaylistEditBusy() ||
                now.playlistRevision!=revision || now.mediaGeneration!=media || now.mediaOpenIntentRevision!=intent ||
                session_->PlaylistEditIntentRevision()!=edit || inputGeneration_!=input ||
                SelectedPlaylistIds()!=selected || now.playlist.size()!=queue.size() ||
                !std::equal(now.playlist.begin(),now.playlist.end(),queue.begin(),
                    [](auto const& a,auto const& b) { return a.id==b.id && a.path==b.path; })) {
                ShowError(L"已复制，但队列或播放请求已变化；未移除任何条目。"); return;
            }
            // Clipboard stays pure text with Copy operation: this never asks
            // Explorer to move/delete files. Removal is the typed mpv edit plan.
            session_->RemovePlaylistItems(std::move(rows));
            if (!session_->PlaylistEditBusy()) {
                ShowError(L"已复制，但队列移除事务未开始；未确认任何条目已移除。");
                return; // Core snapshot.error retains the concrete rejection.
            }
            StatusText().Text(L"已复制并请求从队列移除，结果以列表为准；磁盘文件不受影响。网络地址可能含私密参数。");
            return;
        }
        // No Flush: retain neither paths nor signed URLs in clipboard history.
        StatusText().Text(L"已复制所选路径文本。网络地址可能包含私密参数，请谨慎粘贴。");
    } catch (...) { ShowError(copied ? L"已复制，但移除请求失败；请检查队列。" : L"无法写入剪贴板；未移除队列条目。"); }
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
    const auto operation = ++playlistPasteGeneration_;
    try {
        auto data = Clipboard::GetContent();
        if (data.Contains(StandardDataFormats::StorageItems())) {
            // Explorer's native file clipboard takes precedence over any
            // human-readable display text supplied by the same producer.
            auto items = co_await data.GetStorageItemsAsync();
            if (closing_ || operation != playlistPasteGeneration_) co_return;
            if (epoch != navigationEpoch_ || revision != session_->Snapshot().playlistRevision ||
                session_->PlaylistEditBusy()) {
                ShowError(L"读取文件剪贴板期间列表已改变，请重新粘贴。");
            } else if (items.Size() == 0 || items.Size() > 1000) {
                ShowError(L"每次粘贴1到1000个文件或文件夹。");
            } else {
                std::vector<std::wstring> inputs;
                std::size_t bytes{};
                bool valid = true;
                for (auto const& item : items) {
                    auto value = item.Path();
                    std::filesystem::path path(value.c_str());
                    auto text = to_string(value);
                    bytes += text.size();
                    if (value.empty() || !path.is_absolute() || bytes > 1024 * 1024 ||
                        text.starts_with("\\\\?\\") || text.starts_with("\\\\.\\")) {
                        valid = false; break;
                    }
                    inputs.emplace_back(value.c_str());
                }
                if (!valid) {
                    ShowError(L"文件剪贴板包含无本地路径的虚拟对象、设备路径或过多路径数据，未粘贴。");
                } else {
                    ResolvePlaylistPaste(std::move(inputs), operation, epoch, revision, destination);
                    co_return; // The resolver completion releases the paste gate.
                }
            }
        } else if (!data.Contains(StandardDataFormats::Text())) {
            ShowError(L"剪贴板没有可用文件对象或路径文本。");
        } else {
            auto raw = co_await data.GetTextAsync();
            if (!closing_ && operation == playlistPasteGeneration_) {
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
    } catch (...) {
        if (!closing_ && operation == playlistPasteGeneration_)
            ShowError(L"无法完成剪贴板粘贴，请查看当前列表及播放器错误。");
    }
    if (operation == playlistPasteGeneration_) playlistPasteBusy_ = false;
}
void MainWindow::ResolvePlaylistPaste(std::vector<std::wstring> inputs,
    std::uint64_t operation, std::uint64_t epoch, std::uint64_t revision,
    std::size_t destination) {
    playlistPasteStop_.request_stop();
    auto weak = get_weak();
    StatusText().Text(L"正在解析剪贴板文件和文件夹…可取消待处理粘贴。");
    playlistPasteStop_ = iima::LocalInputResolver::Resolve(std::move(inputs), DispatcherQueue(),
        [weak, operation, epoch, revision, destination](iima::LocalInputResult result) {
            auto self = weak.get();
            if (!self || self->closing_ || operation != self->playlistPasteGeneration_) return;
            self->playlistPasteBusy_ = false;
            if (result.cancelled) return;
            auto const& state = self->session_->Snapshot();
            if (self->navigationEpoch_ != epoch || state.playlistRevision != revision ||
                state.session != iima::SessionState::Ready || self->session_->PlaylistEditBusy()) {
                self->ShowError(L"解析文件期间播放列表已改变，未插入；请重新粘贴。"); return;
            }
            if (result.paths.empty()) {
                self->ShowError(L"剪贴板文件中没有可播放条目。"); return;
            }
            if (result.paths.size() > 1000) {
                self->ShowError(L"展开文件夹后超过1000项，未插入；请减少选择。"); return;
            }
            std::size_t bytes{};
            for (auto const& path : result.paths) bytes += path.size();
            if (bytes > 1024 * 1024) {
                self->ShowError(L"展开后的路径超过1MiB，未插入。"); return;
            }
            // Keep resolver's existing playable-file ordering and supported
            // directory policy; no second filesystem implementation here.
            self->session_->InsertPlaylist(std::move(result.paths), destination);
            if (!result.warnings.empty())
                self->ShowError(L"部分剪贴板项目无法解析或被跳过；其余条目已请求加入，是否成功以列表为准。");
        });
}

}

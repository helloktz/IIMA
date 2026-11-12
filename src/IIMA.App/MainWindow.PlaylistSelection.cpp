// IINA SidebarPlaylistPane removeBtnAction/contextMenuPlayNext selection flow.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include <algorithm>
#include <iterator>
#include <unordered_set>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
std::vector<std::int64_t> MainWindow::SelectedPlaylistIds() const {
    std::vector<std::int64_t> result;
    for (auto const& selected : PlaylistView().SelectedItems()) {
        if (auto item = selected.try_as<Controls::ListViewItem>())
            result.push_back(unbox_value<std::int64_t>(item.Tag()));
    }
    return result;
}
std::vector<std::size_t> MainWindow::SelectedPlaylistRows() {
    if (closing_ || session_->Snapshot().session != iima::SessionState::Ready ||
        session_->PlaylistEditBusy()) return {};
    auto const& current = session_->Snapshot().playlist;
    // Commands are positional at the mpv boundary. Never trust a visual row
    // retained from an older reordered queue, even for duplicate filenames.
    if (current.size() != playlist_.size() ||
        !std::equal(current.begin(), current.end(), playlist_.begin(),
            [](auto const& a, auto const& b) { return a.id == b.id; })) {
        ShowError(L"播放列表已变化，请等待更新后重新选择。");
        return {};
    }
    auto ids = SelectedPlaylistIds();
    std::unordered_set<std::int64_t> selectedIds(ids.begin(), ids.end());
    std::vector<std::size_t> rows;
    for (std::size_t i = 0; i < current.size(); ++i)
        if (selectedIds.contains(current[i].id)) rows.push_back(i);
    if (rows.size() != ids.size()) {
        ShowError(L"部分选择已失效，请重新选择。");
        return {};
    }
    return rows; // Ascending visual order, not the order of Ctrl-clicks.
}
void MainWindow::UpdatePlaylistSelection() {
    auto ids = SelectedPlaylistIds();
    const bool ready = !closing_ && session_->Snapshot().session == iima::SessionState::Ready &&
        !session_->PlaylistEditBusy();
    const bool hasSelection = ready && !ids.empty();
    const bool single = ready && ids.size() == 1;
    PlaySelectedButton().IsEnabled(hasSelection);
    PlaySelectedNewWindowButton().IsEnabled(hasSelection && userDataLoaded_ && selectedInputNewWindowRouter_ &&
        !pickerOpen_ && !interactiveCrop_.Active() && !cropOriginal_ && !inputResolving_ &&
        pendingFiles_.empty() && activationInputs_.empty());
    CutPlaylistPathsButton().IsEnabled(hasSelection && !pickerOpen_ && !interactiveCrop_.Active() &&
        !cropOriginal_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty());
    RemoveButton().IsEnabled(hasSelection);
    DeletePlaylistFilesButton().IsEnabled(hasSelection && !playlistDeletionBusy_ && !pickerOpen_);
    RevealPlaylistButton().IsEnabled(hasSelection && !revealSelectionBusy_);
    auto current = std::find_if(playlist_.begin(), playlist_.end(), [](auto const& item) { return item.current; });
    std::unordered_set<std::int64_t> selectedIds(ids.begin(), ids.end());
    const bool movable = std::any_of(playlist_.begin(), playlist_.end(), [&](auto const& item) {
        return !item.current && selectedIds.contains(item.id);
    });
    QueueSelectedNextButton().IsEnabled(hasSelection && current != playlist_.end() && movable);
    auto selected = single ? std::find_if(playlist_.begin(), playlist_.end(), [&](auto const& item) {
        return item.id == ids.front();
    }) : playlist_.end();
    MoveUpButton().IsEnabled(selected != playlist_.end() && selected != playlist_.begin());
    MoveDownButton().IsEnabled(selected != playlist_.end() && std::next(selected) != playlist_.end());
    PlaylistSelectionText().Text(ids.empty() ? L"未选择条目；Ctrl或Shift可多选" :
        L"已选 " + to_hstring(ids.size()) + L" 项；移除不会删除磁盘文件。");
    UpdatePlaylistSubtitleControls();
    UpdatePlaylistBrowserControls();
    UpdatePlaylistDurations();
}
// IINA SidebarPlaylistPane.contextMenuPlayInNewWindow. Unlike the upstream
// fileURL-only conversion, preserve existing network URLs as network inputs.
void MainWindow::PlaySelectedPlaylistInNewWindow() {
    if (closing_ || !userDataLoaded_ || !selectedInputNewWindowRouter_ || pickerOpen_ ||
        interactiveCrop_.Active() || cropOriginal_ || inputResolving_ ||
        !pendingFiles_.empty() || !activationInputs_.empty()) return;
    try {
        auto rows=SelectedPlaylistRows(); // Validates UI order against real mpv IDs.
        if (rows.empty()) return;
        if (rows.size()>1000) { ShowError(L"每次最多在新窗口打开1000个条目。"); return; }
        auto const& state=session_->Snapshot();
        std::vector<std::string> paths;
        std::size_t bytes{};
        for (auto row:rows) {
            auto const& item=state.playlist[row];
            auto const& path=item.path;
            if (item.id<0 || path.empty() || path.size()>1024*1024-bytes ||
                std::any_of(path.begin(),path.end(),[](unsigned char c) { return c<32 || c==127; })) {
                ShowError(L"所选条目包含无效路径或超过1MiB，未打开新窗口。"); return;
            }
            bytes+=path.size();
            if (iima::IsNetworkMediaPath(path)) {
                if (!iima::PrepareMediaUrl(path) || Windows::Foundation::Uri(to_hstring(path)).Host().empty()) {
                    ShowError(L"所选条目包含无效网络地址，未打开新窗口。"); return;
                }
            } else {
                std::wstring local{to_hstring(path)};
                std::replace(local.begin(),local.end(),L'/',L'\\');
                if (local.size()>32767 || !std::filesystem::path(local).is_absolute() ||
                    local.starts_with(L"\\\\?\\") || local.starts_with(L"\\\\.\\") || local.find(L':',2)!=local.npos) {
                    ShowError(L"仅支持绝对本地文件路径或支持的网络媒体地址，未打开新窗口。"); return;
                }
            }
            paths.push_back(path); // Preserve visual order and duplicate-path entries.
        }
        // No await/resolver: validated IDs and copied paths belong to this UI
        // dispatcher turn. The new window owns its pending input during startup.
        // Never change the origin's input generation, queue, or subtitle cache.
        selectedInputNewWindowRouter_(std::move(paths));
    } catch (hresult_error const&) {
        // A URL may carry private query data. Do not echo parser exceptions.
        ShowError(L"无法在新窗口打开所选条目；请检查地址及16窗口上限。");
    } catch (...) { ShowError(L"无法创建播放器窗口，原队列未修改。"); }
}
void MainWindow::RemoveSelectedPlaylistItems() {
    auto rows = SelectedPlaylistRows();
    if (!rows.empty()) session_->RemovePlaylistItems(std::move(rows));
    UpdatePlaylistSelection();
}
void MainWindow::QueueSelectedPlaylistNext() {
    auto rows = SelectedPlaylistRows();
    if (rows.empty()) return;
    // Existing plan excludes the playing item and preserves selected order.
    // Cursor waits for each command reply plus a new playlist read. A failed
    // later move does not roll back earlier completed moves.
    session_->QueuePlaylistItemsNext(std::move(rows));
    UpdatePlaylistSelection();
}
}

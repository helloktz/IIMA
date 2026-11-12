// IINA SidebarPlaylistPane removeBtnAction/contextMenuPlayNext selection flow.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
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
    RemoveButton().IsEnabled(hasSelection);
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

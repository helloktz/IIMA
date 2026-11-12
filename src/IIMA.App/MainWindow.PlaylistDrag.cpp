// IINA SidebarPlaylistPane.validateDrop/acceptDrop insertion-row semantics.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::ApplicationModel::DataTransfer;
namespace winrt::IIMA::implementation {
namespace {
constexpr wchar_t DragKey[] = L"IIMA.Playlist.LocalDrag";
bool HasPlaylistDrag(DragEventArgs const& args) {
    return args.DataView().Properties().HasKey(DragKey);
}
}
bool MainWindow::PlaylistDragCurrent(DragEventArgs const& args) const {
    if (closing_ || !playlistDragToken_ || !HasPlaylistDrag(args) ||
        session_->Snapshot().session != iima::SessionState::Ready || session_->PlaylistEditBusy()) return false;
    try {
        if (unbox_value<guid>(args.DataView().Properties().Lookup(DragKey)) != *playlistDragToken_) return false;
    } catch (...) { return false; }
    auto const& state = session_->Snapshot();
    if (state.playlistRevision != playlistDragRevision_ || state.playlist.size() != playlistDragIds_.size() ||
        playlist_.size() != playlistDragIds_.size()) return false;
    for (std::size_t i = 0; i < playlistDragIds_.size(); ++i)
        if (state.playlist[i].id != playlistDragIds_[i] || playlist_[i].id != playlistDragIds_[i]) return false;
    return !playlistDragRows_.empty();
}
std::optional<std::size_t> MainWindow::PlaylistDropRow(DragEventArgs const& args) const {
    // Ignore scrollbar surfaces rather than interpreting a scroll operation as
    // a drop onto the nearest row. No automatic ListView model reordering.
    for (auto node = args.OriginalSource().try_as<DependencyObject>(); node && node != PlaylistView();
         node = Media::VisualTreeHelper::GetParent(node))
        if (node.try_as<Controls::Primitives::ScrollBar>()) return {};
    auto point = args.GetPosition(PlaylistView());
    if (point.X < 0 || point.Y < 0 || point.X > PlaylistView().ActualWidth() ||
        point.Y > PlaylistView().ActualHeight()) return {};
    std::optional<std::size_t> lastRealized, nextRealized;
    auto panel = PlaylistView().ItemsPanelRoot();
    if (!panel) return {};
    // Inspect only realized containers, not every item across the WinRT ABI
    // on every pointer movement in a virtualized long playlist.
    for (auto const& child : panel.Children()) {
        auto item = child.try_as<FrameworkElement>();
        if (!item || item.ActualHeight() <= 0) continue;
        auto index = PlaylistView().IndexFromContainer(item);
        if (index < 0 || static_cast<std::size_t>(index) >= playlist_.size()) continue;
        auto row = static_cast<std::size_t>(index);
        auto top = item.TransformToVisual(PlaylistView()).TransformPoint({0, 0}).Y;
        if (top + item.ActualHeight() < 0 || top > PlaylistView().ActualHeight()) continue;
        // Upper half inserts before this row; lower half inserts after it.
        if (point.Y < top + item.ActualHeight() / 2) {
            if (!nextRealized || row < *nextRealized) nextRealized = row;
        } else if (!lastRealized || row > *lastRealized) lastRealized = row;
    }
    if (nextRealized) return nextRealized;
    if (lastRealized) return *lastRealized + 1;
    return {}; // No realized rows: do not guess an off-screen target.
}
void MainWindow::WirePlaylistDrag() {
    auto weak = get_weak();
    PlaylistView().DragItemsStarting([weak](auto const&, Controls::DragItemsStartingEventArgs const& args) {
        auto self = weak.get();
        if (!self) { args.Cancel(true); return; }
        self->playlistDragToken_.reset(); self->playlistDragRows_.clear(); self->playlistDragIds_.clear();
        try {
            auto rows = self->SelectedPlaylistRows();
            if (rows.empty() || rows.size() != args.Items().Size()) { args.Cancel(true); return; }
            // The drag framework and the selected UI rows must refer to exactly
            // the same entries; never silently move only a subset of a drag.
            for (auto const& dragged : args.Items()) {
                auto item = dragged.try_as<Controls::ListViewItem>();
                if (!item) { args.Cancel(true); return; }
                auto id = unbox_value<std::int64_t>(item.Tag());
                if (std::none_of(rows.begin(), rows.end(), [&](auto row) { return self->playlist_[row].id == id; })) {
                    args.Cancel(true); return;
                }
            }
            GUID token{}; check_hresult(CoCreateGuid(&token));
            self->playlistDragToken_ = guid{token};
            self->playlistDragRows_ = std::move(rows);
            for (auto const& item : self->playlist_) self->playlistDragIds_.push_back(item.id);
            self->playlistDragRevision_ = self->session_->Snapshot().playlistRevision;
            // Only an opaque, per-drag capability leaves this window. File paths
            // and row indices are not placed on the OS drag clipboard.
            args.Data().Properties().Insert(DragKey, box_value(*self->playlistDragToken_));
            args.Data().SetData(DragKey, box_value(*self->playlistDragToken_));
            args.Data().RequestedOperation(DataPackageOperation::Move);
        } catch (...) { self->playlistDragToken_.reset(); args.Cancel(true); }
    });
    PlaylistView().DragItemsCompleted([weak](auto const&, auto const&) {
        if (auto self = weak.get()) {
            self->playlistDragToken_.reset(); self->playlistDragRows_.clear(); self->playlistDragIds_.clear();
        }
    });
    PlaylistView().DragOver([weak](auto const&, DragEventArgs const& args) {
        if (!HasPlaylistDrag(args)) return; // External files retain the existing root drop behavior.
        args.Handled(true); args.AcceptedOperation(DataPackageOperation::None);
        if (auto self = weak.get(); self && self->PlaylistDragCurrent(args)) {
            if (auto row = self->PlaylistDropRow(args)) {
                args.AcceptedOperation(DataPackageOperation::Move);
                args.DragUIOverride().Caption(*row == self->playlist_.size() ? L"移动到列表末尾" :
                    L"移动到第 " + to_hstring(*row + 1) + L" 项之前");
                args.DragUIOverride().IsCaptionVisible(true);
            }
        }
    });
    PlaylistView().Drop([weak](auto const&, DragEventArgs const& args) {
        if (!HasPlaylistDrag(args)) return;
        args.Handled(true); args.AcceptedOperation(DataPackageOperation::None);
        if (auto self = weak.get()) {
            try {
                if (!self->PlaylistDragCurrent(args)) {
                    if (!self->closing_) self->ShowError(L"拖动来源或播放列表已变化，请重新拖动。");
                    return;
                }
                auto row = self->PlaylistDropRow(args);
                if (!row) return;
                auto rows = std::move(self->playlistDragRows_);
                self->playlistDragToken_.reset();
                self->session_->DropPlaylistItems(std::move(rows), *row);
                self->UpdatePlaylistSelection();
                args.AcceptedOperation(DataPackageOperation::Move);
                // Move means the plan was submitted, not that asynchronous mpv
                // edits are complete. Existing cursor reports partial failures.
            } catch (std::exception const& error) { self->ShowError(to_hstring(error.what())); }
              catch (hresult_error const& error) { self->ShowError(error.message()); }
        }
    });
}
}

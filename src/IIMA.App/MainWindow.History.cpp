// IINA HistoryWindowController search/group/remove mapped to WinUI.
// Copyright © 2017 lhc; GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/HistoryLocale.h"
#include "../IIMA.Core/Media/VideoTime.h"
#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Input.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireHistoryControls() {
    WireHistoryDeletion();
    auto weak = get_weak();
    RevealHistoryButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RevealSelection(true);
    });
    HistoryView().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->projectingHistory_) self->UpdateHistorySelection();
    });
    HistoryView().DoubleTapped([weak](auto const&, Input::DoubleTappedRoutedEventArgs const& args) {
        if (auto self = weak.get(); self && !self->closing_ && !self->projectingHistory_) {
            auto element = args.OriginalSource().try_as<DependencyObject>();
            while (element && element != self->HistoryView()) {
                if (auto item = element.try_as<Controls::ListViewItem>()) {
                    if (!item.IsEnabled()) return; // Group headers cannot play.
                    self->HistoryView().SelectedItems().Clear();
                    self->HistoryView().SelectedItems().Append(item);
                    self->OpenSelectedHistory(); args.Handled(true); break;
                }
                element = Media::VisualTreeHelper::GetParent(element);
            }
        }
    });
    HistorySearchText().TextChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->closing_) self->RefreshHistory();
    });
    HistoryGroupingBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->closing_) self->RefreshHistory();
    });
    HistorySearchScopeBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->closing_) self->RefreshHistory();
    });
    OpenHistoryButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenSelectedHistory();
    });
    RemoveHistoryButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->closing_) {
            auto rows = self->SelectedHistoryRows();
            auto identities = iima::SelectHistoryRows(self->historyProjection_, rows);
            auto removal = iima::RemoveHistoryEntries(self->userData_.history, identities);
            if (removal.removed.empty()) return;
            self->historyUndo_ = std::move(removal); self->RefreshHistory(); self->MarkUserDataDirty();
        }
    });
    ClearHistoryButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->closing_) {
            // Clearing an already-empty collection must not destroy the last
            // nonempty undo batch (e.g. a second click after removing all rows).
            if (self->userData_.history.empty()) return;
            self->historyUndo_ = iima::ClearHistoryEntries(self->userData_.history);
            self->RefreshHistory(); self->MarkUserDataDirty();
        }
    });
    UndoHistoryButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->closing_ && self->historyUndo_) {
            auto result = iima::RestoreHistoryEntries(self->userData_.history, *self->historyUndo_);
            self->historyUndo_.reset(); self->RefreshHistory(); self->MarkUserDataDirty();
            self->ShowError(L"已恢复 " + to_hstring(result.restored) + L" 条历史；跳过已有记录 " +
                to_hstring(result.alreadyPresent) + L" 条、无效记录 " + to_hstring(result.invalid) +
                L" 条、超过容量 " + to_hstring(result.capacitySkipped) + L" 条。");
        }
    });
}
std::vector<std::size_t> MainWindow::SelectedHistoryRows() const {
    std::vector<std::size_t> rows;
    if (projectingHistory_) return rows;
    for (auto const& selected : HistoryView().SelectedItems()) {
        std::uint32_t index{};
        if (HistoryView().Items().IndexOf(selected, index) && historyProjection_.EntryAt(index))
            rows.push_back(index);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}
void MainWindow::UpdateHistorySelection() {
    if (projectingHistory_) return;
    auto rows = SelectedHistoryRows();
    const bool enabled = !closing_ && userDataLoaded_ && !rows.empty();
    OpenHistoryButton().IsEnabled(enabled);
    RemoveHistoryButton().IsEnabled(enabled);
    RevealHistoryButton().IsEnabled(enabled && !revealSelectionBusy_);
    DeleteHistoryFilesButton().IsEnabled(enabled && !historyDeletionBusy_ && !playlistDeletionBusy_ && !pickerOpen_);
}
void MainWindow::OpenSelectedHistory() {
    if (closing_ || !userDataLoaded_ || projectingHistory_) return;
    auto rows = SelectedHistoryRows();
    auto identities = iima::SelectHistoryRows(historyProjection_, rows);
    auto paths = iima::PlayableHistoryPaths(historyProjection_, identities);
    if (!paths.empty()) QueueFiles(std::move(paths));
}
void MainWindow::RefreshHistory() {
    if (projectingHistory_) return;
    auto rows = SelectedHistoryRows();
    auto selection = iima::SelectHistoryRows(historyProjection_, rows);
    // Clear/rebuild raises SelectionChanged synchronously. Hold the guard until
    // both projection and native selection refer to the new rows.
    struct Guard { bool& value; explicit Guard(bool& v):value(v){value=true;} ~Guard(){value=false;} };
    {
        Guard guard(projectingHistory_);
        iima::HistoryQuery query;
        query.search = to_string(HistorySearchText().Text());
        query.grouping = HistoryGroupingBox().SelectedIndex() == 1 ? iima::HistoryGrouping::FileLocation : iima::HistoryGrouping::LastPlayed;
        query.searchScope = HistorySearchScopeBox().SelectedIndex() == 1 ? iima::HistorySearchScope::FileName : iima::HistorySearchScope::FullPath;
        historyProjection_ = iima::BuildHistoryView(userData_.history, query, iima::WindowsHistoryPresentation());
        HistoryView().Items().Clear();
        std::vector<Controls::ListViewItem> selected;
        for (std::size_t index = 0; index < historyProjection_.rows.size(); ++index) {
            Controls::ListViewItem item;
            if (auto entry = historyProjection_.EntryAt(index)) {
                auto progress = iima::VideoTime(entry->entry.position).StringRepresentation();
                item.Content(box_value(to_hstring(entry->fileName + "  " + progress)));
                Controls::ToolTipService::SetToolTip(item, box_value(to_hstring(entry->entry.path)));
                if (std::any_of(selection.begin(), selection.end(), [&](auto const& identity) {
                    return iima::MatchesHistoryIdentity(entry->entry, identity);
                })) selected.push_back(item);
            } else {
                auto const& group = historyProjection_.groups[historyProjection_.rows[index].groupIndex];
                item.Content(box_value(to_hstring(group.label))); item.IsEnabled(false);
            }
            HistoryView().Items().Append(item);
        }
        for (auto const& item : selected) HistoryView().SelectedItems().Append(item);
        UndoHistoryButton().IsEnabled(historyUndo_ && !historyUndo_->removed.empty());
        ClearHistoryButton().IsEnabled(!userData_.history.empty());
        HistoryCountText().Text(to_hstring(historyProjection_.matchedEntries) + L" / " + to_hstring(historyProjection_.totalEntries));
    }
    UpdateHistorySelection();
}
}

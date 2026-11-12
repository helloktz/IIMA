// IINA HistoryWindowController search/group/remove mapped to WinUI.
// Copyright © 2017 lhc; GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/HistoryLocale.h"
#include "../IIMA.Core/Media/VideoTime.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireHistoryControls() {
    auto weak = get_weak();
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
        if (auto self = weak.get(); self && !self->closing_) {
            auto row = self->HistoryView().SelectedIndex();
            if (row >= 0) if (auto entry = self->historyProjection_.EntryAt(static_cast<std::size_t>(row)))
                self->QueueFiles({entry->entry.path});
        }
    });
    RemoveHistoryButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->closing_) {
            auto row = self->HistoryView().SelectedIndex();
            if (row < 0) return;
            auto selected = static_cast<std::size_t>(row);
            auto identities = iima::SelectHistoryRows(self->historyProjection_, std::span(&selected, 1));
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
void MainWindow::RefreshHistory() {
    std::optional<iima::HistoryIdentity> selection;
    auto previous = HistoryView().SelectedIndex();
    if (previous >= 0) if (auto entry = historyProjection_.EntryAt(static_cast<std::size_t>(previous))) selection = entry->identity;
    iima::HistoryQuery query;
    query.search = to_string(HistorySearchText().Text());
    query.grouping = HistoryGroupingBox().SelectedIndex() == 1 ? iima::HistoryGrouping::FileLocation : iima::HistoryGrouping::LastPlayed;
    query.searchScope = HistorySearchScopeBox().SelectedIndex() == 1 ? iima::HistorySearchScope::FileName : iima::HistorySearchScope::FullPath;
    historyProjection_ = iima::BuildHistoryView(userData_.history, query, iima::WindowsHistoryPresentation());
    HistoryView().Items().Clear();
    int selected = -1;
    for (std::size_t index = 0; index < historyProjection_.rows.size(); ++index) {
        Controls::ListViewItem item;
        if (auto entry = historyProjection_.EntryAt(index)) {
            auto progress = iima::VideoTime(entry->entry.position).StringRepresentation();
            item.Content(box_value(to_hstring(entry->fileName + "  " + progress)));
            Controls::ToolTipService::SetToolTip(item, box_value(to_hstring(entry->entry.path)));
            if (selection && iima::MatchesHistoryIdentity(entry->entry, *selection)) selected = static_cast<int>(index);
        } else {
            auto const& group = historyProjection_.groups[historyProjection_.rows[index].groupIndex];
            item.Content(box_value(to_hstring(group.label))); item.IsEnabled(false);
        }
        HistoryView().Items().Append(item);
    }
    HistoryView().SelectedIndex(selected);
    UndoHistoryButton().IsEnabled(historyUndo_ && !historyUndo_->removed.empty());
    ClearHistoryButton().IsEnabled(!userData_.history.empty());
    HistoryCountText().Text(to_hstring(historyProjection_.matchedEntries) + L" / " + to_hstring(historyProjection_.totalEntries));
}
}

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
    WireHistoryAvailability();
    auto weak = get_weak();
    RevealHistoryButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RevealSelection(true);
    });
    HistoryView().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->projectingHistory_) {
            ++self->historySelectionRevision_;
            self->UpdateHistorySelection();
        }
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
    OpenHistoryNewWindowButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenSelectedHistory(true);
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
    auto identities = iima::SelectHistoryRows(historyProjection_, rows);
    OpenHistoryButton().IsEnabled(enabled && !iima::PlayableHistoryPaths(historyProjection_, identities).empty());
    OpenHistoryNewWindowButton().IsEnabled(enabled && static_cast<bool>(selectedInputNewWindowRouter_) &&
        !iima::PlayableHistoryPaths(historyProjection_, identities).empty());
    RemoveHistoryButton().IsEnabled(enabled);
    RevealHistoryButton().IsEnabled(enabled && !revealSelectionBusy_);
    DeleteHistoryFilesButton().IsEnabled(enabled && !historyDeletionBusy_ && !playlistDeletionBusy_ && !pickerOpen_);
}
// IINA HistoryWindowController.playAction/playInNewWindowAction both pass
// shouldAutoLoad:false. Do not send history through generic single-file open.
void MainWindow::OpenSelectedHistory(bool newWindow) {
    if (closing_ || !userDataLoaded_ || projectingHistory_ || pickerOpen_ ||
        interactiveCrop_.Active() || cropOriginal_ || (newWindow && !selectedInputNewWindowRouter_)) return;
    auto identities=iima::SelectHistoryRows(historyProjection_,SelectedHistoryRows());
    auto paths=iima::PlayableHistoryPaths(historyProjection_,identities);
    if (paths.empty()) return;
    if (paths.size()>1000) { ShowError(L"每次最多打开1000条历史，请减少选择。"); return; }
    std::size_t inputBytes{};
    for (auto const& path:paths) {
        if (path.size()>1024*1024-inputBytes) { ShowError(L"所选历史路径超过1MiB，未打开。"); return; }
        inputBytes+=path.size();
    }
    if (activationTimer_) activationTimer_.Stop();
    activationInputs_.clear(); activationDelayElapsed_=false;
    inputStop_.request_stop();
    const auto input=++inputGeneration_, selection=historySelectionRevision_;
    const auto media=session_->Snapshot().mediaGeneration, intent=session_->Snapshot().mediaOpenIntentRevision;
    const auto playlistRevision=session_->Snapshot().playlistRevision, editIntent=session_->PlaylistEditIntentRevision();
    pendingFiles_.clear(); pendingAutoFileMatch_=false;
    CancelAutomaticFiles(); UpdateNetworkOpen(session_->Snapshot());
    inputResolving_=true;
    try {
        std::vector<std::wstring> inputs;
        for (auto const& path:paths) inputs.emplace_back(to_hstring(path));
        auto weak=get_weak();
        inputStop_=iima::LocalInputResolver::Resolve(std::move(inputs),DispatcherQueue(),
            [weak,input,selection,media,intent,playlistRevision,editIntent,newWindow,identities=std::move(identities),paths=std::move(paths)]
            (iima::LocalInputResult result) {
                auto self=weak.get();
                if (!self || self->closing_ || self->inputGeneration_!=input) return;
                self->inputResolving_=false;
                if (result.cancelled) return;
                auto const& state=self->session_->Snapshot();
                auto selected=iima::SelectHistoryRows(self->historyProjection_,self->SelectedHistoryRows());
                const bool same=selected.size()==identities.size() && std::equal(selected.begin(),selected.end(),identities.begin(),
                    [](auto const& a,auto const& b) {
                        return a.path==b.path && a.playedAt==b.playedAt && a.position==b.position && a.duration==b.duration;
                    });
                if (!same || self->historySelectionRevision_!=selection || state.mediaGeneration!=media ||
                    state.mediaOpenIntentRevision!=intent || state.playlistRevision!=playlistRevision ||
                    self->session_->PlaylistEditIntentRevision()!=editIntent || self->session_->PlaylistEditBusy() || self->pickerOpen_ || self->interactiveCrop_.Active() ||
                    self->cropOriginal_ || !self->activationInputs_.empty() ||
                    iima::PlayableHistoryPaths(self->historyProjection_,identities)!=paths) {
                    self->ShowError(L"解析期间历史选择、文件状态或播放请求已变化，未打开；请重新选择。"); return;
                }
                if (result.paths.empty()) { self->ShowError(L"所选历史中没有可打开的文件，请刷新文件状态。"); return; }
                std::size_t bytes{};
                if (result.paths.size()>1000) { self->ShowError(L"解析后超过1000项，未打开。"); return; }
                for (auto const& path:result.paths) {
                    if (path.size()>1024*1024-bytes) { self->ShowError(L"解析后路径超过1MiB，未打开。"); return; }
                    bytes+=path.size();
                }
                try {
                    // Ignore singleExplicitMedia deliberately: history provenance
                    // survives resolution and both receiving-window routes.
                    if (newWindow) self->selectedInputNewWindowRouter_(std::move(result.paths));
                    else self->ReceiveResolvedInput(std::move(result.paths),false);
                    if (!result.warnings.empty()) self->ShowError(L"部分历史文件无法打开或被跳过；其余文件已请求打开。");
                } catch (hresult_error const& error) { self->ShowError(error.message()); }
                  catch (std::exception const& error) { self->ShowError(to_hstring(error.what())); }
            },true); // History resolves regular files only, never replacement directories/playlists.
    } catch (hresult_error const& error) { inputResolving_=false; ShowError(error.message()); }
      catch (std::exception const& error) { inputResolving_=false; ShowError(to_hstring(error.what())); }
}
void MainWindow::RefreshHistory() {
    playerRecentMenuDirty_ = true;
    RefreshPlayerMenus();
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
        auto presentation = iima::WindowsHistoryPresentation();
        presentation.availability = [this](std::string_view path) {
            auto found = historyAvailability_.find(path);
            return found == historyAvailability_.end() ? iima::HistoryAvailability::Unknown : found->second;
        };
        historyProjection_ = iima::BuildHistoryView(userData_.history, query, presentation);
        HistoryView().Items().Clear();
        std::vector<Controls::ListViewItem> selected;
        for (std::size_t index = 0; index < historyProjection_.rows.size(); ++index) {
            Controls::ListViewItem item;
            if (auto entry = historyProjection_.EntryAt(index)) {
                auto progress = iima::VideoTime(entry->entry.position).StringRepresentation();
                const bool missing = entry->availability == iima::HistoryAvailability::Missing;
                auto caption = to_hstring(entry->fileName) + (missing ? L"（上次检查缺失）" : L"  " + to_hstring(progress));
                item.Content(box_value(caption));
                // Keep missing items selectable for removal/reveal; only opening
                // filters them out. Text, not color alone, conveys the status.
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
    RequestHistoryAvailability();
}
}

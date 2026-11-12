// Saved filter manager adapted from IINA FilterWindowController/MenuController.
// Copyright (c) 2016-2017 lhc. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::LoadSavedFilters() {
    savedFilterUndo_.reset();
    auto issues = savedFilterLibrary_.Restore(userData_.settings.savedVideoFilters, userData_.settings.savedAudioFilters);
    RefreshSavedFilters();
    if (!issues.empty()) ShowError(L"部分保存的滤镜无效或快捷键重复，未载入这些条目。");
}
void MainWindow::WireSavedFilters() {
    auto weak = get_weak();
    CopySavedFilterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OrganizeSavedFilter(0);
    });
    MoveSavedFilterUpButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OrganizeSavedFilter(1);
    });
    MoveSavedFilterDownButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OrganizeSavedFilter(2);
    });
    UndoSavedFilterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OrganizeSavedFilter(3);
    });
    SaveNewFilterButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->EditSavedFilter(0); });
    UpdateSavedFilterButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->EditSavedFilter(1); });
    DeleteSavedFilterButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->EditSavedFilter(2); });
    SavedFilterView().SelectionChanged([weak](auto const&, auto const&) {
        auto self = weak.get(); if (!self || self->closing_ || self->refreshingSavedFilters_) return;
        auto row = self->SavedFilterView().SelectedIndex();
        auto const& values = self->savedFilterLibrary_.Values(self->FilterTypeBox().SelectedIndex() == 1);
        if (row < 0 || static_cast<std::size_t>(row) >= values.size()) return;
        auto const& filter = values[row];
        self->SavedFilterName().Text(to_hstring(filter.name));
        self->ChainFilterExpression().Text(to_hstring(filter.filterString));
        self->SavedFilterKey().Text(to_hstring(filter.shortcutKey));
        self->SavedFilterCtrl().IsChecked(iima::HasFilterModifier(filter.modifiers, iima::FilterShortcutModifier::Control));
        self->SavedFilterAlt().IsChecked(iima::HasFilterModifier(filter.modifiers, iima::FilterShortcutModifier::Alt));
        self->SavedFilterShift().IsChecked(iima::HasFilterModifier(filter.modifiers, iima::FilterShortcutModifier::Shift));
    });
    ToggleSavedFilterButton().Click([weak](auto const&, auto const&) {
        auto self = weak.get(); if (!self || self->closing_) return;
        auto row = self->SavedFilterView().SelectedIndex();
        if (row >= 0) self->ApplySavedFilter(self->FilterTypeBox().SelectedIndex() == 1, static_cast<std::size_t>(row));
    });
    SaveCurrentChainFilterButton().Click([weak](auto const&, auto const&) {
        auto self = weak.get(); if (!self || self->closing_) return;
        auto index = self->FilterChainView().SelectedIndex();
        if (index < 0 || static_cast<std::size_t>(index) >= self->displayedFilters_.entries.size()) return;
        auto const& row = self->displayedFilters_.entries[index];
        if (!row.filter) { self->ShowError(L"无法保存未识别的滤镜行。"); return; }
        self->ChainFilterExpression().Text(to_hstring(row.filter->StringFormat()));
        self->EditSavedFilter(0);
    });
}
void MainWindow::RefreshSavedFilters() {
    UndoSavedFilterButton().IsEnabled(!closing_ && savedFilterUndo_.has_value() &&
        savedFilterUndo_->expectedRevision == savedFilterLibrary_.Revision());
    refreshingSavedFilters_ = true;
    auto const row = SavedFilterView().SelectedIndex();
    auto audio = FilterTypeBox().SelectedIndex() == 1;
    auto const& chain = audio ? session_->Snapshot().audioFilters : session_->Snapshot().videoFilters;
    auto const& values = savedFilterLibrary_.Values(audio);
    SavedFilterView().Items().Clear();
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto state = savedFilterLibrary_.ObservedState(audio, index, chain);
        auto prefix = state == iima::SavedFilterObservedState::Enabled ? L"✓ " :
            state == iima::SavedFilterObservedState::Disabled ? L"停用 " :
            state == iima::SavedFilterObservedState::Unavailable ? L"未知 " : L"○ ";
        auto label = hstring(prefix) + to_hstring(values[index].name);
        if (!values[index].shortcutKey.empty()) label = label + L"  [" + to_hstring(values[index].ReadableShortcut()) + L"]";
        SavedFilterView().Items().Append(box_value(label));
    }
    // Preset identities are stable unless this library is edited; callers clear
    // selection before structural edits or a category change.
    if (row >= 0 && static_cast<std::size_t>(row) < values.size()) SavedFilterView().SelectedIndex(row);
    refreshingSavedFilters_ = false;
    RefreshPlayerMenus();
}
void MainWindow::EditSavedFilter(int action) {
    if (closing_ || !userDataLoaded_) return;
    auto audio = FilterTypeBox().SelectedIndex() == 1;
    auto row = SavedFilterView().SelectedIndex();
    auto before = savedFilterLibrary_;
    iima::SavedFilterLibraryResult result;
    if (action == 2) {
        if (row < 0) return;
        result = savedFilterLibrary_.Remove(audio, row);
    } else {
        iima::SavedFilter value;
        value.name = to_string(SavedFilterName().Text());
        value.filterString = to_string(ChainFilterExpression().Text());
        value.shortcutKey = to_string(SavedFilterKey().Text());
        if (SavedFilterCtrl().IsChecked().Value()) value.modifiers = value.modifiers | iima::FilterShortcutModifier::Control;
        if (SavedFilterAlt().IsChecked().Value()) value.modifiers = value.modifiers | iima::FilterShortcutModifier::Alt;
        if (SavedFilterShift().IsChecked().Value()) value.modifiers = value.modifiers | iima::FilterShortcutModifier::Shift;
        auto key = iima::SavedFilterLibrary::ShortcutKey(value);
        if (key && keyBindings_ && keyBindings_->Find(*key)) {
            ShowError(L"此快捷键已由 input.conf 使用，请修改快捷键配置或选择其他键。"); return;
        }
        if (action == 0) result = savedFilterLibrary_.Add(audio, std::move(value));
        else if (action == 1 && row >= 0) result = savedFilterLibrary_.Edit(audio, row, std::move(value));
        else return;
    }
    if (!result.success) { ShowError(to_hstring(result.error)); return; }
    CommitSavedFilterEdit(std::move(before), audio,
        action == 2 ? -1 : static_cast<int>(result.index));
    // Editing/deleting saved definitions intentionally leaves active vf/af alone.
}
void MainWindow::CommitSavedFilterEdit(iima::SavedFilterLibrary before, bool audio, int selection) {
    savedFilterUndo_ = SavedFilterUndo{std::move(before), savedFilterLibrary_.Revision(),
                                     audio, SavedFilterView().SelectedIndex()};
    userData_.settings.savedVideoFilters = savedFilterLibrary_.Values(false);
    userData_.settings.savedAudioFilters = savedFilterLibrary_.Values(true);
    SavedFilterView().SelectedIndex(-1); RefreshSavedFilters(); MarkUserDataDirty();
    SavedFilterView().SelectedIndex(selection);
}
void MainWindow::OrganizeSavedFilter(int action) {
    if (closing_ || !userDataLoaded_) return;
    if (action == 3) {
        if (!savedFilterUndo_) return;
        if (savedFilterUndo_->expectedRevision != savedFilterLibrary_.Revision()) {
            savedFilterUndo_.reset(); RefreshSavedFilters();
            ShowError(L"预设库已改变，不能覆盖新修改。"); return;
        }
        // input.conf can be changed independently after deletion of a preset.
        // Restoring a shortcut must not silently steal a newly assigned key.
        for (bool category : {false, true}) {
            for (auto const& value : savedFilterUndo_->library.Values(category)) {
                auto key = iima::SavedFilterLibrary::ShortcutKey(value);
                if (key && !key->empty() && keyBindings_ && keyBindings_->Find(*key)) {
                    ShowError(L"撤销会恢复与当前 input.conf 冲突的快捷键，请先解决冲突。"); return;
                }
            }
        }
        // Validate a complete candidate before replacing the live library.
        auto candidate = savedFilterLibrary_;
        auto issues = candidate.Restore(savedFilterUndo_->library.Values(false),
                                        savedFilterUndo_->library.Values(true));
        if (!issues.empty()) {
            ShowError(L"撤销快照未通过预设校验，当前预设保持不变。"); return;
        }
        auto undo = std::move(*savedFilterUndo_);
        savedFilterUndo_.reset();
        savedFilterLibrary_ = std::move(candidate);
        userData_.settings.savedVideoFilters = savedFilterLibrary_.Values(false);
        userData_.settings.savedAudioFilters = savedFilterLibrary_.Values(true);
        FilterTypeBox().SelectedIndex(undo.audio ? 1 : 0);
        SavedFilterView().SelectedIndex(-1); RefreshSavedFilters();
        SavedFilterView().SelectedIndex(undo.selection);
        MarkUserDataDirty();
        return;
    }
    auto row = SavedFilterView().SelectedIndex();
    auto audio = FilterTypeBox().SelectedIndex() == 1;
    auto const count = savedFilterLibrary_.Values(audio).size();
    if (row < 0 || static_cast<std::size_t>(row) >= count) return;
    auto before = savedFilterLibrary_;
    iima::SavedFilterLibraryResult result;
    if (action == 0) {
        // Copy the saved expression, not an unrelated unsaved expression draft.
        result = savedFilterLibrary_.Copy(audio, row, to_string(SavedFilterName().Text()));
    } else if (action == 1 && row > 0) {
        result = savedFilterLibrary_.Move(audio, row, row - 1);
    } else if (action == 2 && static_cast<std::size_t>(row + 1) < count) {
        result = savedFilterLibrary_.Move(audio, row, row + 1);
    } else return;
    if (!result.success) { ShowError(to_hstring(result.error)); return; }
    CommitSavedFilterEdit(std::move(before), audio, static_cast<int>(result.index));
}
void MainWindow::ApplySavedFilter(bool audio, std::size_t index) {
    if (closing_) return;
    auto const& chain = audio ? session_->Snapshot().audioFilters : session_->Snapshot().videoFilters;
    auto plan = savedFilterLibrary_.PlanToggle(audio, index, chain);
    if (!plan.error.empty()) { ShowError(to_hstring(plan.error)); return; }
    if (plan.kind == iima::SavedFilterApplyPlan::Kind::Add) session_->AddFilter(audio, plan.filterString);
    else if (plan.kind == iima::SavedFilterApplyPlan::Kind::EditChain) session_->EditFilters(audio, std::move(plan.edit));
}
}

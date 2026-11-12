// Saved filter manager adapted from IINA FilterWindowController/MenuController.
// Copyright (c) 2016-2017 lhc. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::LoadSavedFilters() {
    auto issues = savedFilterLibrary_.Restore(userData_.settings.savedVideoFilters, userData_.settings.savedAudioFilters);
    RefreshSavedFilters();
    if (!issues.empty()) ShowError(L"部分保存的滤镜无效或快捷键重复，未载入这些条目。");
}
void MainWindow::WireSavedFilters() {
    auto weak = get_weak();
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
    refreshingSavedFilters_ = true;
    auto const row = SavedFilterView().SelectedIndex();
    auto audio = FilterTypeBox().SelectedIndex() == 1;
    auto const& chain = audio ? session_->Snapshot().audioFilters : session_->Snapshot().videoFilters;
    auto const& values = savedFilterLibrary_.Values(audio);
    SavedFilterView().Items().Clear();
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto state = savedFilterLibrary_.ObservedState(audio, index, chain);
        auto prefix = state == iima::SavedFilterObservedState::Enabled ? L"✓ " : L"○ ";
        auto label = hstring(prefix) + to_hstring(values[index].name);
        if (!values[index].shortcutKey.empty()) label = label + L"  [" + to_hstring(values[index].ReadableShortcut()) + L"]";
        SavedFilterView().Items().Append(box_value(label));
    }
    // Preset identities are stable unless this library is edited; callers clear
    // selection before structural edits or a category change.
    if (row >= 0 && static_cast<std::size_t>(row) < values.size()) SavedFilterView().SelectedIndex(row);
    refreshingSavedFilters_ = false;
}
void MainWindow::EditSavedFilter(int action) {
    if (closing_ || !userDataLoaded_) return;
    auto audio = FilterTypeBox().SelectedIndex() == 1;
    auto row = SavedFilterView().SelectedIndex();
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
    userData_.settings.savedVideoFilters = savedFilterLibrary_.Values(false);
    userData_.settings.savedAudioFilters = savedFilterLibrary_.Values(true);
    SavedFilterView().SelectedIndex(-1); RefreshSavedFilters(); MarkUserDataDirty();
    // Editing/deleting saved definitions intentionally leaves active vf/af alone.
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

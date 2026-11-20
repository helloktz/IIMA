// IINA FilterWindowController current filter table operations.
// Copyright (c) 2016 lhc. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireFilterEditor() {
    WireFilterPresets();
    WireQuickLut();
    auto weak = get_weak();
    FilterTypeBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            self->ResetFilterPresetCatalog();
            self->SavedFilterView().SelectedIndex(-1);
            self->filterRevision_.reset();
            self->UpdateFilterEditor(self->session_->Snapshot());
        }
    });
    AddChainFilterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_)
            self->session_->AddFilter(self->FilterTypeBox().SelectedIndex() == 1,
                to_string(self->ChainFilterExpression().Text()));
    });
    RemoveChainFilterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->EditSelectedFilter(0);
    });
    ToggleChainFilterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->EditSelectedFilter(1);
    });
    MoveChainFilterUpButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->EditSelectedFilter(2);
    });
    MoveChainFilterDownButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->EditSelectedFilter(3);
    });
    ReplaceChainFilterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->EditSelectedFilter(4);
    });
}
void MainWindow::UpdateFilterEditor(iima::PlaybackSnapshot const& state) {
    UpdateQuickLut();
    const bool active=!closing_ && state.session==iima::SessionState::Ready && state.media==iima::MediaState::Loaded;
    auto project=[&](Microsoft::UI::Xaml::Controls::Button button,bool mirror) {
        auto status=iima::FindOrientationFilter(state.videoFilters,mirror).state;
        using S=iima::OrientationFilterState;
        button.IsEnabled(active && status!=S::Unknown && status!=S::Conflict);
        std::wstring text=mirror ? iima::loc::Text(L"LeftAndRightMirrors2") : iima::loc::Text(L"UpAndDown");
        text+=status==S::Enabled ? iima::loc::Text(L"EnabledClickToRemove") : status==S::Disabled ? iima::loc::Text(L"DisabledClickToEnable") :
            status==S::Absent ? iima::loc::Text(L"NotAddedClickEnabled") : status==S::Conflict ? iima::loc::Text(L"TabConflict") : iima::loc::Text(L"StatusUnknown");
        button.Content(box_value(text));
    };
    project(FlipVideoButton(),false); project(MirrorVideoButton(),true);
    auto const& chain = FilterTypeBox().SelectedIndex() == 1 ? state.audioFilters : state.videoFilters;
    if (filterRevision_ == chain.revision) return;
    filterRevision_ = chain.revision;
    displayedFilters_ = chain;
    // Do not preserve row index across external changes: duplicate expressions
    // and unlabelled filters make identity ambiguous.
    FilterChainView().Items().Clear();
    for (auto const& row : chain.entries) {
        auto label = row.enabled ? L"✓ " : L"○ ";
        FilterChainView().Items().Append(box_value(hstring(label) + to_hstring(row.display)));
    }
    FilterChainError().Text(to_hstring(chain.error));
    RefreshSavedFilters();
}
void MainWindow::EditSelectedFilter(int operation) {
    if (closing_) return;
    auto row = FilterChainView().SelectedIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= displayedFilters_.entries.size()) return;
    auto index = static_cast<std::size_t>(row);
    auto revision = displayedFilters_.revision;
    iima::FilterEditPlan plan;
    switch (operation) {
    case 0: plan = iima::FilterChainEditor::Remove(displayedFilters_, revision, index); break;
    case 1: plan = iima::FilterChainEditor::SetEnabled(displayedFilters_, revision, index,
        !displayedFilters_.entries[index].enabled); break;
    case 2:
        if (!index) return;
        plan = iima::FilterChainEditor::Move(displayedFilters_, revision, index, index - 1); break;
    case 3:
        if (index + 1 == displayedFilters_.entries.size()) return;
        plan = iima::FilterChainEditor::Move(displayedFilters_, revision, index, index + 1); break;
    case 4: {
        auto parsed = iima::MPVFilter::Parse(to_string(ChainFilterExpression().Text()));
        if (!parsed) { ShowError(iima::loc::Text(L"FilterExpressionIsInvalid")); return; }
        plan = iima::FilterChainEditor::Replace(displayedFilters_, revision, index, *parsed);
        break;
    }
    default: return;
    }
    session_->EditFilters(FilterTypeBox().SelectedIndex() == 1, std::move(plan));
}
}

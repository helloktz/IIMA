// IINA SidebarVideoPane.cropAction removes the managed crop before entering
// InteractiveMode. Windows adds guarded cancel restoration and async readback.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool Matches(iima::FilterNode const& node, iima::FilterChainSnapshot const& current) {
    iima::FilterEditPlan check{node,{},true,{}};
    return check.FreshMatches(current);
}
}
namespace winrt::IIMA::implementation {
void MainWindow::BeginManagedCropTransaction() {
    auto const& state = session_->Snapshot();
    auto const& chain = state.videoFilters;
    if (closing_ || pickerOpen_ || cropOriginal_ || state.session != iima::SessionState::Ready ||
        state.media != iima::MediaState::Loaded || !chain.complete || chain.entries.size() != 1 ||
        !chain.entries[0].filter || chain.entries[0].filter->Name() != (interactiveDelogo_ ? "lavfi" : "crop") ||
        chain.entries[0].filter->Label() != std::optional<std::string>{interactiveDelogo_ ? "iina_delogo" : "iina_crop"} ||
        AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay) {
        ShowError(iima::loc::Text(L"OnlyTheOnlyManagedFilterInThe")); return;
    }
    if (interactiveDelogo_) {
        auto const& params = chain.entries[0].filter->Params();
        if (!params || params->size() != 1 || !params->contains("graph") ||
            !params->at("graph").starts_with("delogo=") ||
            params->at("graph").find_first_of("[],;\\'") != std::string::npos) {
            ShowError(iima::loc::Text(L"ExistingDemarkingGraphicsAreNotASingle")); return;
        }
    }
    auto plan = iima::FilterChainEditor::Remove(chain,chain.revision,0);
    if (!plan.valid) { ShowError(to_hstring(plan.error)); return; }
    cropOriginal_ = chain; cropExpectedEmpty_ = plan.after;
    cropTransactionPhase_ = CropTransactionPhase::Removing;
    const auto generation = ++cropTransactionGeneration_;
    cropCancelRequested_ = false;
    interactiveCropEpoch_ = navigationEpoch_; interactiveCropEntry_ = state.entryId;
    interactiveCropWasPlaying_ = !state.paused;
    CancelMouseClick(); CancelThumbnailPreview(); session_->Pause(true);
    PlayerSidebarContent().IsEnabled(false);
    RefreshPlayerMenus();
    UpdateWindowAspectConstraint();
    InteractiveCropCanvas().Visibility(Visibility::Collapsed);
    InteractiveCropToolbar().Visibility(Visibility::Visible);
    InteractiveCropTitle().Text(interactiveDelogo_ ? iima::loc::Text(L"InteractToMark") : iima::loc::Text(L"InteractiveCrop"));
    InteractiveDelogoRemoveButton().Visibility(Visibility::Collapsed);
    InteractiveCropConfirmButton().IsEnabled(false);
    InteractiveCropCancelButton().IsEnabled(true);
    InteractiveCropResume().IsChecked(true);
    InteractiveCropStatus().Text(iima::loc::Text(L"TheOriginalAreaFilterIsBeingTemporarily"));
    InteractiveCropCancelButton().Focus(FocusState::Programmatic);
    UpdateControlBarPolicy();
    auto weak = get_weak();
    bool queued = session_->EditFiltersWithCompletion(false,std::move(plan),
        [weak,generation](bool success,iima::FilterChainSnapshot observed,std::string) {
            auto self = weak.get();
            if (!self || !self->cropOriginal_ || self->closing_ || generation != self->cropTransactionGeneration_) return;
            auto const& state = self->session_->Snapshot();
            if (self->navigationEpoch_ != self->interactiveCropEpoch_ || state.entryId != self->interactiveCropEntry_ ||
                state.media != iima::MediaState::Loaded || state.session != iima::SessionState::Ready) {
                self->AbandonManagedCropTransaction(iima::loc::Text(L"MediaChangedDuringTemporaryRemovalAndNo")); return;
            }
            if (!success || !Matches(self->cropExpectedEmpty_,observed)) {
                if (Matches(self->cropOriginal_->root,observed)) {
                    self->CompleteManagedCropTransaction(true);
                    self->ShowError(iima::loc::Text(L"CouldNotTemporarilyRemoveTheCroppingThe"));
                } else self->AbandonManagedCropTransaction(iima::loc::Text(L"TheTemporaryRemovalResultWasNotConfirmed"));
                return;
            }
            self->cropTransactionPhase_ = CropTransactionPhase::Preparing;
            if (self->cropCancelRequested_) self->FinishManagedCropTransaction(false);
            else self->StartInteractiveCrop();
        });
    if (!queued) {
        CompleteManagedCropTransaction(true);
        ShowError(iima::loc::Text(L"TheTemporaryRemovalRequestIsNotQueued"));
    }
}
void MainWindow::FinishManagedCropTransaction(bool commit, bool removeDelogo) {
    if (!cropOriginal_) return;
    if (cropTransactionPhase_ == CropTransactionPhase::Removing) {
        if (!commit) {
            cropCancelRequested_ = true;
            InteractiveCropStatus().Text(iima::loc::Text(L"CancelsTheRequestAndRestoresTheOriginal"));
        }
        return;
    }
    if (cropTransactionPhase_ == CropTransactionPhase::Writing) {
        InteractiveCropStatus().Text(iima::loc::Text(L"TheFinalFilterRequestHasBeenSubmitted")); return;
    }
    auto const& state = session_->Snapshot();
    if (navigationEpoch_ != interactiveCropEpoch_ || state.entryId != interactiveCropEntry_ ||
        state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded ||
        !Matches(cropExpectedEmpty_,state.videoFilters)) {
        AbandonManagedCropTransaction(iima::loc::Text(L"FiltersOrMediaHaveBeenChangedAnd")); return;
    }
    if (removeDelogo) {
        if (!interactiveDelogo_ || !commit || cropOriginal_->entries.empty() ||
            cropTransactionPhase_ != CropTransactionPhase::Editing) return;
        // Preview removal has already been acknowledged and read back. Keep
        // that empty chain instead of restoring or submitting a duplicate set.
        CompleteManagedCropTransaction(true); return;
    }
    if (!commit && cropOriginal_->entries.empty()) {
        // No pre-existing crop was removed; cancel is a read-only exit.
        CompleteManagedCropTransaction(true); return;
    }
    iima::FilterNode desired = cropOriginal_->root;
    if (commit) {
        if (!interactiveCrop_.Active() || !InteractiveCropMapping()) {
            InteractiveCropStatus().Text(iima::loc::Text(L"TheMapIsNotReadyToBe")); return;
        }
        auto candidate = interactiveCrop_;
        std::optional<iima::CropCommit> result;
        if (interactiveDelogo_ && cropMapping_) {
            auto pixels = iima::QuantizeCropRect(candidate.Selection(),
                cropMapping_->sourceWidth,cropMapping_->sourceHeight,0);
            if (!pixels || pixels->x < 1 || pixels->y < 1 ||
                pixels->x + pixels->width >= cropMapping_->sourceWidth ||
                pixels->y + pixels->height >= cropMapping_->sourceHeight) {
                InteractiveCropStatus().Text(iima::loc::Text(L"PleaseAdjustTheElectoralAreaToConfirm")); return;
            }
            result = iima::CropCommit{interactiveCropEpoch_,*pixels,false,{}};
        } else result = candidate.Confirm();
        if (!result) return;
        if (result->removeManagedCrop) {
            // The readback already confirmed the empty preview chain. Choosing
            // the full source frame intentionally removes the original crop.
            CompleteManagedCropTransaction(true); return;
        }
        auto filter = iima::MPVFilter::Crop(result->pixels.width,result->pixels.height,result->pixels.x,result->pixels.y);
        iima::MPVFilter named(filter.Name(),std::string("iina_crop"),filter.Params());
        if (interactiveDelogo_) {
            auto const& p = result->pixels;
            // Same lavfi delogo x/y/w/h semantics as IINA delogoAction, but a
            // typed graph parameter avoids raw syntax at the node boundary.
            std::string graph = "delogo=x="+std::to_string(p.x)+":y="+std::to_string(p.y)+
                ":w="+std::to_string(p.width)+":h="+std::to_string(p.height);
            named = iima::MPVFilter("lavfi",std::string("iina_delogo"),
                iima::MPVFilter::Parameters{{"graph",std::move(graph)}});
        }
        auto replace = cropOriginal_->entries.empty()
            ? iima::FilterChainEditor::CreateSingle(*cropOriginal_,named)
            : iima::FilterChainEditor::Replace(*cropOriginal_,cropOriginal_->revision,0,named);
        if (!replace.valid) { ShowError(to_hstring(replace.error)); return; }
        desired = std::move(replace.after);
        if (!cropOriginal_->entries.empty()) {
            // Replace intentionally rebuilds a generic filter row. Here the
            // filter identity is unchanged: change only crop params, preserving
            // enabled and every unknown original field for this transaction.
            auto row = cropOriginal_->root.children.front();
            auto const& replacement = desired.children.front();
            for (std::size_t n=0; n<replacement.keys.size(); ++n) {
                if (replacement.keys[n] != "params") continue;
                auto key = std::find(row.keys.begin(),row.keys.end(),"params");
                if (key == row.keys.end()) {
                    row.keys.push_back("params"); row.children.push_back(replacement.children[n]);
                } else row.children[static_cast<std::size_t>(key-row.keys.begin())] = replacement.children[n];
            }
            desired.children.front() = std::move(row);
        }
    }
    iima::FilterEditPlan plan{cropExpectedEmpty_,desired,true,{}};
    const auto generation = cropTransactionGeneration_;
    const bool restoring = !commit;
    cropPointer_.reset(); interactiveCrop_.CancelDrag();
    InteractiveCropCanvas().ReleasePointerCaptures();
    InteractiveCropCanvas().IsHitTestVisible(false);
    cropTransactionPhase_ = CropTransactionPhase::Writing;
    InteractiveCropConfirmButton().IsEnabled(false);
    InteractiveCropCancelButton().IsEnabled(false);
    InteractiveCropStatus().Text(restoring ? iima::loc::Text(L"ResumeOriginalAreaFilterAndCheckResults") : iima::loc::Text(L"ApplyingNewAreaFiltersAndCheckingResults"));
    auto weak = get_weak();
    bool queued = session_->EditFiltersWithCompletion(false,std::move(plan),
        [weak,generation,desired=std::move(desired),restoring](bool success,iima::FilterChainSnapshot observed,std::string) {
            auto self = weak.get();
            if (!self || !self->cropOriginal_ || self->closing_ || generation != self->cropTransactionGeneration_) return;
            auto const& state = self->session_->Snapshot();
            if (self->navigationEpoch_ != self->interactiveCropEpoch_ || state.entryId != self->interactiveCropEntry_ ||
                state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded) {
                self->AbandonManagedCropTransaction(iima::loc::Text(L"TheMediaChangedDuringTheFinalRequest")); return;
            }
            if (success && Matches(desired,observed)) {
                self->CompleteManagedCropTransaction(true); return;
            }
            if (Matches(self->cropOriginal_->root,observed)) {
                const bool emptyBaseline = self->cropOriginal_->entries.empty();
                self->CompleteManagedCropTransaction(true);
                self->ShowError(emptyBaseline ? iima::loc::Text(L"TheNewAreaFilterIsNotConfirmed") : iima::loc::Text(L"TheNewAreaFilterIsNotConfirmed2")); return;
            }
            if (!restoring && Matches(self->cropExpectedEmpty_,observed)) {
                self->cropTransactionPhase_ = CropTransactionPhase::Preparing;
                self->FinishManagedCropTransaction(false); // One bounded rollback attempt.
                self->ShowError(iima::loc::Text(L"TheNewAreaFilterWasNotSuccessfully")); return;
            }
            self->AbandonManagedCropTransaction(iima::loc::Text(L"UnableToConfirmTheRestorationOrApplication"));
        });
    if (!queued) {
        cropTransactionPhase_ = CropTransactionPhase::Preparing;
        InteractiveCropCancelButton().IsEnabled(true);
        InteractiveCropStatus().Text(iima::loc::Text(L"TheFinalRequestIsNotInThe"));
    }
}
void MainWindow::CompleteManagedCropTransaction(bool restorePlayback) {
    cropOriginal_.reset(); cropExpectedEmpty_ = {};
    cropTransactionPhase_ = CropTransactionPhase::None; cropCancelRequested_ = false;
    ++cropTransactionGeneration_;
    InteractiveCropCancelButton().IsEnabled(true);
    FinishInteractiveCrop(false,restorePlayback);
}
void MainWindow::AbandonManagedCropTransaction(hstring const& message) {
    if (cropOriginal_ && !cropOriginal_->entries.empty() && cropOriginal_->entries[0].filter) {
        // Recovery is explicit and reviewable. Never automatically put an old
        // filter onto a new media generation or overwrite an external edit.
        auto expression = to_hstring(cropOriginal_->entries[0].filter->StringFormat());
        VideoFilterText().Text(expression); ChainFilterExpression().Text(expression);
    }
    const bool hadOriginalCrop = cropOriginal_ && !cropOriginal_->entries.empty();
    CompleteManagedCropTransaction(false);
    if (!closing_) ShowError(hadOriginalCrop ? message : iima::loc::Text(L"AreaFilterResultsCannotBeConfirmedDue"));
}
}

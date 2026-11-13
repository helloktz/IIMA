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
        ShowError(L"只能编辑当前模式唯一的受管滤镜（iina_crop或iina_delogo）；请先移除其他滤镜、解决标签冲突并退出悬浮小窗。"); return;
    }
    if (interactiveDelogo_) {
        auto const& params = chain.entries[0].filter->Params();
        if (!params || params->size() != 1 || !params->contains("graph") ||
            !params->at("graph").starts_with("delogo=") ||
            params->at("graph").find_first_of("[],;\\'") != std::string::npos) {
            ShowError(L"已有去标志图形不是单一可识别的delogo表达式；请在滤镜列表中检查，不自动替换。"); return;
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
    InteractiveCropTitle().Text(interactiveDelogo_ ? L"交互去标志" : L"交互裁剪");
    InteractiveDelogoRemoveButton().Visibility(Visibility::Collapsed);
    InteractiveCropConfirmButton().IsEnabled(false);
    InteractiveCropCancelButton().IsEnabled(true);
    InteractiveCropResume().IsChecked(true);
    InteractiveCropStatus().Text(L"正在临时移除原区域滤镜并读取结果；取消将在结果确认后恢复。");
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
                self->AbandonManagedCropTransaction(L"临时移除期间媒体已变化，未向新媒体写回裁剪；原表达式已保留。"); return;
            }
            if (!success || !Matches(self->cropExpectedEmpty_,observed)) {
                if (Matches(self->cropOriginal_->root,observed)) {
                    self->CompleteManagedCropTransaction(true);
                    self->ShowError(L"未能临时移除裁剪；读取结果确认原滤镜仍保留。");
                } else self->AbandonManagedCropTransaction(L"临时移除结果未能确认或出现外部修改；未覆盖滤镜，原表达式已保留。");
                return;
            }
            self->cropTransactionPhase_ = CropTransactionPhase::Preparing;
            if (self->cropCancelRequested_) self->FinishManagedCropTransaction(false);
            else self->StartInteractiveCrop();
        });
    if (!queued) {
        CompleteManagedCropTransaction(true);
        ShowError(L"临时移除请求未排入队列，原区域滤镜未由本操作修改。");
    }
}
void MainWindow::FinishManagedCropTransaction(bool commit, bool removeDelogo) {
    if (!cropOriginal_) return;
    if (cropTransactionPhase_ == CropTransactionPhase::Removing) {
        if (!commit) {
            cropCancelRequested_ = true;
            InteractiveCropStatus().Text(L"取消已请求，等待移除结果后恢复原区域滤镜。关闭播放器将直接释放本会话。");
        }
        return;
    }
    if (cropTransactionPhase_ == CropTransactionPhase::Writing) {
        InteractiveCropStatus().Text(L"最终滤镜请求已提交，正在等待结果；请勿把等待当作恢复完成。"); return;
    }
    auto const& state = session_->Snapshot();
    if (navigationEpoch_ != interactiveCropEpoch_ || state.entryId != interactiveCropEntry_ ||
        state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded ||
        !Matches(cropExpectedEmpty_,state.videoFilters)) {
        AbandonManagedCropTransaction(L"滤镜或媒体已改变，未覆盖其他操作；原区域滤镜表达式已保留。"); return;
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
            InteractiveCropStatus().Text(L"映射尚未就绪，不能确认。取消可恢复原区域滤镜。"); return;
        }
        auto candidate = interactiveCrop_;
        std::optional<iima::CropCommit> result;
        if (interactiveDelogo_ && cropMapping_) {
            auto pixels = iima::QuantizeCropRect(candidate.Selection(),
                cropMapping_->sourceWidth,cropMapping_->sourceHeight,0);
            if (!pixels || pixels->x < 1 || pixels->y < 1 ||
                pixels->x + pixels->width >= cropMapping_->sourceWidth ||
                pixels->y + pixels->height >= cropMapping_->sourceHeight) {
                InteractiveCropStatus().Text(L"去标志区域必须位于画面内部，四边至少保留1像素。请调整选区后确认。"); return;
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
    InteractiveCropStatus().Text(restoring ? L"正在恢复原区域滤镜并核对结果…" : L"正在应用新区域滤镜并核对结果…");
    auto weak = get_weak();
    bool queued = session_->EditFiltersWithCompletion(false,std::move(plan),
        [weak,generation,desired=std::move(desired),restoring](bool success,iima::FilterChainSnapshot observed,std::string) {
            auto self = weak.get();
            if (!self || !self->cropOriginal_ || self->closing_ || generation != self->cropTransactionGeneration_) return;
            auto const& state = self->session_->Snapshot();
            if (self->navigationEpoch_ != self->interactiveCropEpoch_ || state.entryId != self->interactiveCropEntry_ ||
                state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded) {
                self->AbandonManagedCropTransaction(L"最终请求期间媒体已变化，结果未按原媒体确认；原表达式已保留。"); return;
            }
            if (success && Matches(desired,observed)) {
                self->CompleteManagedCropTransaction(true); return;
            }
            if (Matches(self->cropOriginal_->root,observed)) {
                const bool emptyBaseline = self->cropOriginal_->entries.empty();
                self->CompleteManagedCropTransaction(true);
                self->ShowError(emptyBaseline ? L"新区域滤镜未确认；读取结果仍为空链。" : L"新区域滤镜未确认；读取结果显示原区域滤镜仍在。"); return;
            }
            if (!restoring && Matches(self->cropExpectedEmpty_,observed)) {
                self->cropTransactionPhase_ = CropTransactionPhase::Preparing;
                self->FinishManagedCropTransaction(false); // One bounded rollback attempt.
                self->ShowError(L"新区域滤镜未成功确认，已请求恢复原区域滤镜；恢复结果仍需等待。"); return;
            }
            self->AbandonManagedCropTransaction(L"无法确认恢复或应用结果，未重复覆盖当前滤镜；原区域滤镜表达式已保留供手动恢复。");
        });
    if (!queued) {
        cropTransactionPhase_ = CropTransactionPhase::Preparing;
        InteractiveCropCancelButton().IsEnabled(true);
        InteractiveCropStatus().Text(L"最终请求未入队。可再次点击取消退出或恢复原区域滤镜。");
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
    if (!closing_) ShowError(hadOriginalCrop ? message : L"区域滤镜结果因媒体或滤镜变化而无法确认；未覆盖当前状态，也未自动恢复播放。");
}
}

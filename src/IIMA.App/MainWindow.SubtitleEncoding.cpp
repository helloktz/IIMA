// IINA MainMenuActions.menuSetSubEncoding / CharEncoding.list.
// Copyright (c) 2016 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Subtitles/CharacterEncoding.h"
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireSubtitleEncoding() {
    for (auto const& encoding : iima::SubtitleEncodings) {
        Microsoft::UI::Xaml::Controls::ComboBoxItem item;
        item.Content(box_value(to_hstring(encoding.title)));
        item.Tag(box_value(to_hstring(encoding.code)));
        SubtitleEncodingBox().Items().Append(item);
    }
    SubtitleEncodingBox().SelectedIndex(0);
    auto weak = get_weak();
    ApplySubtitleEncodingButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            auto item = self->SubtitleEncodingBox().SelectedItem().try_as<Microsoft::UI::Xaml::Controls::ComboBoxItem>();
            if (!item) return;
            self->StartSubtitleReload(to_string(unbox_value<hstring>(item.Tag())));
        }
    });
    CancelSubtitleReloadButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->closing_ && self->session_->SubtitleReloadBusy()) {
            self->session_->CancelSubtitleReload();
            self->UpdateSubtitleReload();
        }
    });
}
void MainWindow::UpdateSubtitleEncoding(iima::PlaybackSnapshot const& state) {
    const auto value = state.subtitleStyleValues.find("sub-codepage");
    SubtitleEncodingObservedText().Text(value == state.subtitleStyleValues.end() ?
        L"当前编码：未知" : hstring{L"当前编码：" + std::wstring(to_hstring(value->second))});
    ApplySubtitleEncodingButton().IsEnabled(CanStartSubtitleReload());
    UpdateSubtitleReload();
    // Keep user's candidate separate from observed codepage: periodic time-pos
    // must not overwrite a choice that has not yet been explicitly applied.
}
}

namespace winrt::IIMA::implementation {
bool MainWindow::CanStartSubtitleReload() {
    auto const& state=session_->Snapshot();
    return !closing_ && !subtitleReloadStarting_ && userDataLoaded_ && !pickerOpen_ && !inputResolving_ &&
        pendingFiles_.empty() && activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ &&
        !session_->PlaylistEditBusy() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->ExternalVideoLoadBusy() && !session_->SubtitleReloadBusy() && !state.mediaOriginPending &&
        state.session==iima::SessionState::Ready && state.media==iima::MediaState::Loaded;
}
void MainWindow::StartSubtitleReload(std::optional<std::string> encoding) {
    if(!CanStartSubtitleReload()) {if(!closing_) ShowError(L"当前无法重载字幕：请等待输入、媒体切换或字幕操作完成。");return;}
    auto const& state=session_->Snapshot();
    subtitleReloadRevision_=session_->SubtitleReload().revision+1;
    subtitleReloadGeneration_=state.mediaGeneration;
    subtitleReloadIntent_=state.mediaOpenIntentRevision;
    subtitleReloadInput_=inputGeneration_;
    subtitleReloadObservedInput_=inputGeneration_;
    subtitleReloadStarting_=true;
    bool threw=false;
    try {
        if(encoding) session_->SetSubtitleEncoding(std::move(*encoding));
        else session_->ReloadSubtitles();
    } catch(...) {threw=true;}
    subtitleReloadStarting_=false;
    if(threw && !closing_) ShowError(L"字幕操作提交时出现错误；请检查事务状态，已提交操作不能撤回。");
    // An accepted transaction can fail its first worker submission and return
    // false while already owning a new revision. Retain that failure report.
    if(session_->SubtitleReload().revision!=subtitleReloadRevision_) {
        subtitleReloadRevision_=0;
        if(!closing_ && !threw) ShowError(L"字幕操作未提交；请检查媒体、轨道及当前事务状态。");
    }
    UpdateSubtitleReload();
    RefreshPlayerMenus();
}
void MainWindow::InvalidateSubtitleReload() {
    subtitleReloadRevision_=0;
    subtitleReloadObservedInput_=inputGeneration_;
    session_->CancelSubtitleReload();
    SubtitleReloadStatusText().Text(L"");
    CancelSubtitleReloadButton().IsEnabled(false);
}
void MainWindow::UpdateSubtitleReload() {
    if(subtitleReloadUpdating_ || subtitleReloadStarting_) return;
    struct Guard {bool& value;~Guard(){value=false;}} guard{subtitleReloadUpdating_};
    subtitleReloadUpdating_=true;
    using Phase=iima::SubtitleReloadStatus::Phase;
    auto const& state=session_->Snapshot();
    const bool changedInput=subtitleReloadObservedInput_!=inputGeneration_;
    subtitleReloadObservedInput_=inputGeneration_;
    const bool wrongMedia=subtitleReloadRevision_ && (subtitleReloadGeneration_!=state.mediaGeneration ||
        subtitleReloadIntent_!=state.mediaOpenIntentRevision || subtitleReloadInput_!=inputGeneration_);
    if(closing_ || changedInput || wrongMedia || inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty()) {
        subtitleReloadRevision_=0;
        if(session_->SubtitleReloadBusy() && session_->SubtitleReload().phase!=Phase::Cancelled)
            session_->CancelSubtitleReload();
    }
    const auto status=session_->SubtitleReload();
    const bool busy=session_->SubtitleReloadBusy();
    const bool owned=subtitleReloadRevision_ && subtitleReloadRevision_==status.revision &&
        subtitleReloadGeneration_==state.mediaGeneration && subtitleReloadIntent_==state.mediaOpenIntentRevision && subtitleReloadInput_==inputGeneration_;
    ReloadSubtitlesButton().IsEnabled(CanStartSubtitleReload());
    ApplySubtitleEncodingButton().IsEnabled(CanStartSubtitleReload());
    CancelSubtitleReloadButton().IsEnabled(!closing_ && busy && status.phase!=Phase::Cancelled);
    if(closing_) {SubtitleReloadStatusText().Text(L"");return;}
    if(!owned) {
        SubtitleReloadStatusText().Text(busy ? L"字幕操作仍在处理已提交请求；等待回复后才能开始下一笔。" : L"");
        return;
    }
    hstring phase;
    switch(status.phase) {
    case Phase::Idle: phase=L"尚未开始";break;
    case Phase::Encoding: phase=L"正在设置字幕编码";break;
    case Phase::Reading: phase=L"正在读取最新字幕轨道";break;
    case Phase::Reloading: phase=L"正在串行重载外部字幕";break;
    case Phase::Restoring: phase=L"正在恢复原字幕选择";break;
    case Phase::Completed: phase=L"字幕事务已结束";break;
    case Phase::Cancelled: phase=busy ? L"已取消后续操作，等待已提交请求回复" : L"后续操作已取消";break;
    case Phase::Failed: phase=L"字幕事务失败";break;
    }
    hstring text=phase+L"；重载回复成功 "+to_hstring(status.completed)+L"，失败 "+to_hstring(status.failed)+
        L"，跳过 "+to_hstring(status.skipped)+L"，剩余 "+to_hstring(status.remaining)+L"（计划 "+to_hstring(status.total)+L"）。";
    if(!status.error.empty()) text+=L"\n"+to_hstring(status.error.substr(0,512));
    text+=L"\n计数仅表示命令回复，不保证画面已刷新。取消不撤回已提交的重载或编码变更；失败可能已移除原字幕。";
    if(SubtitleReloadStatusText().Text()!=text) SubtitleReloadStatusText().Text(text);
}
}

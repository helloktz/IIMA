// IINA AppData.swift command allowlist / MPVCommand.swift track-reload entries.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Windows explicit inspector action, not an upstream inspector reload panel.
// mpv cmd_track_reload removes/re-adds a track; no source-file write or deletion.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
hstring SafeReloadText(std::string const& value,std::size_t limit) {
    try {
    auto wide=to_hstring(value); std::wstring result;
    for(wchar_t ch:wide) {
        if(result.size()>=limit) break;
        result.push_back(ch<32 || ch==127 || (ch>=0x2028 && ch<=0x202e) || (ch>=0x2066 && ch<=0x2069) ? L'?' : ch);
    }
    if(!result.empty() && result.back()>=0xd800 && result.back()<=0xdbff) result.pop_back();
    return hstring(result);
    } catch(...) {return L"无法显示该文本";}
}
}
namespace winrt::IIMA::implementation {
bool MainWindow::CanReloadInspectedTrack() {
    const auto track=SelectedRemovableTrack();
    return !closing_ && userDataLoaded_ && !pickerOpen_ && !externalReloadStarting_ &&
        !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
        !session_->ExternalTrackReloadBusy() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalAVLoadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy() &&
        session_->Snapshot().session==iima::SessionState::Ready && session_->Snapshot().media==iima::MediaState::Loaded && track && (track->type=="audio" || track->type=="video");
}
winrt::fire_and_forget MainWindow::ReloadInspectedTrack() {
    auto lifetime=get_strong();
    if(!CanReloadInspectedTrack()) co_return;
    const auto track=*SelectedRemovableTrack();
    externalReloadRevision_=0;
    std::string description=track.title;
    if(description.empty()) {
        auto path=track.externalPath.substr(0,track.externalPath.find_first_of("?#"));
        auto slash=path.find_last_of("/\\");
        description=slash==std::string::npos ? path : path.substr(slash+1);
    }
    externalReloadLabel_=to_hstring(track.type)+L" #"+to_hstring(track.id)+L" · "+SafeReloadText(description,256);
    auto const& state=session_->Snapshot();
    externalReloadGeneration_=state.mediaGeneration; externalReloadIntent_=state.mediaOpenIntentRevision;
    externalReloadInput_=inputGeneration_; externalReloadObservedInput_=inputGeneration_;
    externalReloadSelection_=trackInspectionSelectionRevision_;externalReloadTracks_=state.tracksRevision;
    externalReloadTrackIntent_=session_->TrackEditIntentRevision();
    auto reportCurrent=[&] {auto const& s=session_->Snapshot();return !closing_ && inputGeneration_==externalReloadInput_ &&
        s.mediaGeneration==externalReloadGeneration_ && s.mediaOpenIntentRevision==externalReloadIntent_;};
    auto current=[&] {
        const auto& s=session_->Snapshot();
        auto selected=SelectedRemovableTrack();
        return !closing_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
            !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
            s.session==iima::SessionState::Ready && !s.mediaOriginPending && s.media==iima::MediaState::Loaded && s.mediaGeneration==externalReloadGeneration_ &&
            s.mediaOpenIntentRevision==externalReloadIntent_ && inputGeneration_==externalReloadInput_ &&
            s.tracksRevision==externalReloadTracks_ && session_->TrackEditIntentRevision()==externalReloadTrackIntent_ && trackInspectionSelectionRevision_==externalReloadSelection_ &&
            selected && selected->type==track.type && selected->id==track.id && selected->externalPath==track.externalPath;
    };
    pickerOpen_=true;
    try {
        Controls::ContentDialog dialog;
        externalReloadDialog_=dialog;
        dialog.XamlRoot(Root().XamlRoot());dialog.Title(box_value(L"重新载入所选外部音视频轨道？"));
        Controls::TextBlock text;text.TextWrapping(TextWrapping::Wrap);
        text.Text(L"轨道 "+externalReloadLabel_+
            L"\n仅重读源文件，不修改或删除磁盘文件。播放器可能先移除再添加轨道，轨道ID可能变化；会尝试恢复原音频或视频选择。失败时原轨道可能已不在，恢复选择并不是回滚。\n操作不主动切换播放／暂停，不保证首帧或声音已呈现。已提交操作无法撤回。");
        dialog.Content(text);dialog.PrimaryButtonText(L"重载轨道");dialog.CloseButtonText(L"取消");
        dialog.DefaultButton(Controls::ContentDialogButton::Close);
        dialog.PrimaryButtonClick([&](auto const&,Controls::ContentDialogButtonClickEventArgs const& args) {
            if(!current() || session_->ExternalTrackReloadBusy() || session_->ExternalTrackRemovalBusy() || session_->ExternalAVLoadBusy() || session_->SubtitleReloadBusy() || session_->SubtitleLoadBusy()) args.Cancel(true);
        });
        UpdateExternalTrackReload();
        const auto result=co_await dialog.ShowAsync();
        externalReloadDialog_=nullptr;pickerOpen_=false;
        if(closing_) co_return;
        if(result==Controls::ContentDialogResult::Primary && current() && !session_->ExternalTrackReloadBusy() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalAVLoadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy()) {
            externalReloadRevision_=session_->ExternalTrackReload().revision+1;
            externalReloadStarting_=true;
            try {session_->ReloadExternalTrack(track.type,track.id,track.externalPath);}
            catch(...) {if(reportCurrent()) ShowError(L"轨道重载提交发生错误；已提交操作不能撤回，请检查当前轨道。");}
            externalReloadStarting_=false;
            if(session_->ExternalTrackReload().revision!=externalReloadRevision_) {
                externalReloadRevision_=0;if(reportCurrent()) ShowError(L"轨道或播放器状态已变化，未开始重载；请重新选择并确认。");
            }
        }
    } catch(...) {
        externalReloadDialog_=nullptr;pickerOpen_=false;externalReloadStarting_=false;
        if(reportCurrent()) ShowError(L"无法显示或完成轨道重载确认，未自动重试。");
    }
    if(!closing_) {UpdateExternalTrackReload();OpenPendingFiles();FlushActivation();}
}
void MainWindow::InvalidateExternalTrackReload() {
    externalReloadRevision_=0;externalReloadObservedInput_=inputGeneration_;
    session_->CancelExternalTrackReload();
    if(externalReloadDialog_) externalReloadDialog_.Hide();
    ExternalReloadStatus().Text(L"");CancelExternalReloadButton().IsEnabled(false);
}
void MainWindow::UpdateExternalTrackReload() {
    if(externalReloadUpdating_ || externalReloadStarting_) return;
    struct Guard{bool& value;~Guard(){value=false;}} guard{externalReloadUpdating_};externalReloadUpdating_=true;
    auto const& s=session_->Snapshot();
    const bool wrongMedia=s.mediaGeneration!=externalReloadGeneration_ || s.mediaOpenIntentRevision!=externalReloadIntent_ || inputGeneration_!=externalReloadInput_;
    const bool inputChanged=inputGeneration_!=externalReloadObservedInput_;
    externalReloadObservedInput_=inputGeneration_;
    const bool newInput=inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty();
    if(closing_ || inputChanged || newInput || (externalReloadRevision_ && wrongMedia)) {
        externalReloadRevision_=0;session_->CancelExternalTrackReload();
    }
    const bool unavailable=s.session!=iima::SessionState::Ready || s.media!=iima::MediaState::Loaded || s.mediaOriginPending ||
        interactiveCrop_.Active() || cropOriginal_ || session_->PlaylistEditBusy();
    if(externalReloadDialog_ && (closing_ || unavailable || wrongMedia || newInput || s.tracksRevision!=externalReloadTracks_ || session_->TrackEditIntentRevision()!=externalReloadTrackIntent_ ||
        trackInspectionSelectionRevision_!=externalReloadSelection_ || !TrackInspectorExpander().IsExpanded())) externalReloadDialog_.Hide();
    if(externalReloadDialog_) externalReloadDialog_.IsPrimaryButtonEnabled(!unavailable && !wrongMedia && !newInput &&
        s.tracksRevision==externalReloadTracks_ && session_->TrackEditIntentRevision()==externalReloadTrackIntent_ && trackInspectionSelectionRevision_==externalReloadSelection_ &&
        !session_->ExternalTrackReloadBusy() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalAVLoadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy());
    const auto status=session_->ExternalTrackReload();
    const bool busy=session_->ExternalTrackReloadBusy();
    ReloadInspectedTrackButton().IsEnabled(CanReloadInspectedTrack());
    CancelExternalReloadButton().IsEnabled(!closing_ && busy && status.phase!=iima::ExternalTrackReloadStatus::Phase::Cancelled);
    if(closing_ || !externalReloadRevision_ || externalReloadRevision_!=status.revision || wrongMedia) {
        ExternalReloadStatus().Text(!closing_ && busy ? L"正在等待已提交的轨道操作结束；不能开始另一笔重载。" : L"");return;
    }
    using Phase=iima::ExternalTrackReloadStatus::Phase;
    hstring text;
    switch(status.phase) {
    case Phase::Idle: break;
    case Phase::Reading:text=L"正在读取轨道与原选择…";break;
    case Phase::Reloading:text=L"正在重新载入外部轨道…";break;
    case Phase::Restoring:text=L"正在恢复并确认原轨道选择…";break;
    case Phase::Completed:text=L"重载命令已确认，重新读取已确认目标轨道存在且原选择已恢复。轨道ID可能改变；不代表首帧或声音已呈现。";break;
    case Phase::Cancelled:text=busy ? L"已取消后续操作，等待在途请求结束；已提交动作不能撤回。" :
        !status.reloadSubmitted ? L"已取消，未提交重载命令。" : L"已取消后续操作；重载已提交，不能撤回。";break;
    case Phase::Failed:text=L"重载未完整完成；未自动重试。原轨道可能已不在，请检查当前轨道与选择。";break;
    }
    if(status.phase==Phase::Cancelled || status.phase==Phase::Failed) {
        if(status.reloadAcknowledged) text = text + L" 重载命令已确认。";
        if(status.targetPresentConfirmed) text = text + L" 重新读取已确认目标轨道存在。";
        if(status.selectionRestoredConfirmed) text = text + L" 重新读取已确认原选择已恢复。";
        else if(status.reloadSubmitted && !busy) text = text + L" 原选择恢复未确认。";
    }
    if(!text.empty()) text=externalReloadLabel_+L"："+text;
    if(!status.error.empty()) text = text + L"\n"+SafeReloadText(status.error,1024);
    if(ExternalReloadStatus().Text()!=text) ExternalReloadStatus().Text(text);
}
}

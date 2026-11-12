// IINA InspectorWindowController.TracksVC.updateInfo/updateTrack.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireTrackInspector() {
    auto weak=get_weak();
    TrackInspectorExpander().Expanding([weak](auto const&,auto const&) {
        if(auto self=weak.get()) self->UpdateTrackInspector(true);
    });
    TrackInspectorExpander().Collapsed([weak](auto const&,auto const&) {
        if(auto self=weak.get()) self->ClearTrackInspector();
    });
    TrackInspectorBox().SelectionChanged([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->trackInspectorProjecting_ && !self->closing_) {
            ++self->trackInspectionSelectionRevision_; self->ShowInspectedTrack();
        }
    });
    ReloadInspectedTrackButton().Click([weak](auto const&,auto const&) {if(auto self=weak.get()) self->ReloadInspectedTrack();});
    CancelExternalReloadButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->closing_) {self->session_->CancelExternalTrackReload();self->UpdateExternalTrackReload();}
    });
    RemoveInspectedTrackButton().Click([weak](auto const&,auto const&) {if(auto self=weak.get()) self->RemoveInspectedTrack();});
    CancelExternalRemovalButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->closing_) {self->session_->CancelExternalTrackRemoval();self->UpdateExternalTrackRemoval();}
    });
    CopyTrackInspectionButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get()) self->CopyTrackInspection();
    });
}
void MainWindow::ClearTrackInspector() {
    trackInspectorProjecting_=true;
    TrackInspectorBox().Items().Clear();TrackInspectorDetails().Text(L"");
    CopyTrackInspectionButton().IsEnabled(false);
    RemoveInspectedTrackButton().IsEnabled(false);
    ReloadInspectedTrackButton().IsEnabled(false);
    inspectedTracks_.clear();trackInspectorRevision_.reset();
    trackInspectorProjecting_=false;
}
bool MainWindow::TrackInspectorCurrent() const {
    auto const& state=session_->Snapshot();
    return !closing_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
        state.session==iima::SessionState::Ready && !state.mediaOriginPending &&
        (state.media==iima::MediaState::Loaded || state.media==iima::MediaState::Ended) &&
        trackInspectorRevision_==state.tracksRevision && trackInspectorGeneration_==state.mediaGeneration &&
        trackInspectorIntent_==state.mediaOpenIntentRevision && trackInspectorInput_==inputGeneration_;
}
void MainWindow::UpdateTrackInspector(bool force) {
    UpdateExternalTrackRemoval();
    UpdateExternalTrackReload();
    auto const& state=session_->Snapshot();
    const bool active=!closing_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
        state.session==iima::SessionState::Ready && !state.mediaOriginPending &&
        (state.media==iima::MediaState::Loaded || state.media==iima::MediaState::Ended);
    if(!active || (!force && !TrackInspectorExpander().IsExpanded())) {
        if(trackInspectorRevision_ || !inspectedTracks_.empty()) ClearTrackInspector();
        return;
    }
    if(!force && TrackInspectorCurrent()) return;
    std::optional<iima::TrackIdentity> previous;
    auto oldIndex=TrackInspectorBox().SelectedIndex();
    const bool sameMedia=trackInspectorGeneration_==state.mediaGeneration && trackInspectorIntent_==state.mediaOpenIntentRevision && trackInspectorInput_==inputGeneration_;
    if(sameMedia && oldIndex>=0 && static_cast<std::size_t>(oldIndex)<inspectedTracks_.size()) previous=inspectedTracks_[oldIndex].identity;
    auto projection=iima::InspectTracks(state.tracks);
    ClearTrackInspector();
    trackInspectorGeneration_=state.mediaGeneration;trackInspectorIntent_=state.mediaOpenIntentRevision;
    trackInspectorRevision_=state.tracksRevision;trackInspectorInput_=inputGeneration_;
    if(!projection) {
        TrackInspectorDetails().Text(L"轨道列表过大或身份不明确，未显示部分结果。请等待完整的播放器观察。");return;
    }
    inspectedTracks_=std::move(*projection);
    trackInspectorProjecting_=true;
    int selected=-1;
    for(std::size_t index=0;index<inspectedTracks_.size();++index) {
        auto const& track=inspectedTracks_[index];
        TrackInspectorBox().Items().Append(box_value(to_hstring(track.label)));
        if(previous && *previous==track.identity) selected=static_cast<int>(index);
    }
    // A removed track must not silently become the different track at its row.
    if(!previous && !inspectedTracks_.empty()) selected=0;
    TrackInspectorBox().SelectedIndex(selected);trackInspectorProjecting_=false;
    ShowInspectedTrack();
}
void MainWindow::ShowInspectedTrack() {
    if(!TrackInspectorCurrent()) {ClearTrackInspector();return;}
    auto index=TrackInspectorBox().SelectedIndex();
    bool valid=index>=0 && static_cast<std::size_t>(index)<inspectedTracks_.size();
    CopyTrackInspectionButton().IsEnabled(valid);
    TrackInspectorDetails().Text(valid ? to_hstring(inspectedTracks_[index].Report()) :
        hstring{L"请选择轨道查看；查看不会改变实际播放轨道。"});
    UpdateExternalTrackRemoval();
    UpdateExternalTrackReload();
}
void MainWindow::CopyTrackInspection() {
    if(!TrackInspectorExpander().IsExpanded() || !TrackInspectorCurrent()) {UpdateTrackInspector();return;}
    auto index=TrackInspectorBox().SelectedIndex();
    if(index<0 || static_cast<std::size_t>(index)>=inspectedTracks_.size()) return;
    // Re-project the current observed identity, never export the stale row text.
    auto track=iima::InspectTrack(session_->Snapshot().tracks,inspectedTracks_[index].identity);
    if(!track) {ClearTrackInspector();return;}
    const auto generation=trackInspectorGeneration_,intent=trackInspectorIntent_;
    const auto revision=trackInspectorRevision_;
    const auto identity=track->identity;
    auto sameSelection=[&] {
        auto row=TrackInspectorBox().SelectedIndex();
        return TrackInspectorCurrent() && row>=0 && static_cast<std::size_t>(row)<inspectedTracks_.size() &&
            inspectedTracks_[row].identity==identity && trackInspectorGeneration_==generation &&
            trackInspectorIntent_==intent && trackInspectorRevision_==revision;
    };
    try {
        using namespace Windows::ApplicationModel::DataTransfer;
        DataPackage package;package.RequestedOperation(DataPackageOperation::Copy);package.SetText(to_hstring(track->Report()));
        ClipboardContentOptions options;options.IsAllowedInHistory(false);options.IsRoamable(false);
        if(!sameSelection()) return;
        bool copied=Clipboard::SetContentWithOptions(package,options);
        if(!closing_ && sameSelection())
            ShowError(copied ? L"已复制所选轨道报告，禁用剪贴板历史及漫游；分享前请检查外部路径等私密信息。" : L"剪贴板正忙，未复制轨道报告。");
    } catch(...) {if(!closing_) ShowError(L"无法复制轨道报告。");}
}
}

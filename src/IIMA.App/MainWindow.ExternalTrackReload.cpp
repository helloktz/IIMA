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
    } catch(...) {return iima::loc::Text(L"CouldNotShowText");}
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
        dialog.XamlRoot(Root().XamlRoot());dialog.Title(box_value(iima::loc::Text(L"ReloadSelectedExternalAudioAndVideoTracks")));
        Controls::TextBlock text;text.TextWrapping(TextWrapping::Wrap);
        text.Text(iima::loc::Text(L"Orbital2")+externalReloadLabel_+
            iima::loc::Text(L"RereadTheSourceFileOnlyAndDo"));
        dialog.Content(text);dialog.PrimaryButtonText(iima::loc::Text(L"ReloadOrbit"));dialog.CloseButtonText(iima::loc::Text(L"Cancel"));
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
            catch(...) {if(reportCurrent()) ShowError(iima::loc::Text(L"ThereWasAnErrorInTheOrbital"));}
            externalReloadStarting_=false;
            if(session_->ExternalTrackReload().revision!=externalReloadRevision_) {
                externalReloadRevision_=0;if(reportCurrent()) ShowError(iima::loc::Text(L"OrbitalOrPlayerStateHasChangedAnd"));
            }
        }
    } catch(...) {
        externalReloadDialog_=nullptr;pickerOpen_=false;externalReloadStarting_=false;
        if(reportCurrent()) ShowError(iima::loc::Text(L"UnableToShowOrCompleteReloadConfirmation"));
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
        ExternalReloadStatus().Text(!closing_ && busy ? iima::loc::Text(L"WaitingForTheCompletionOfTheSubmitted") : L"");return;
    }
    using Phase=iima::ExternalTrackReloadStatus::Phase;
    hstring text;
    switch(status.phase) {
    case Phase::Idle: break;
    case Phase::Reading:text=iima::loc::Text(L"ReadingOrbitsAndOriginalSelection");break;
    case Phase::Reloading:text=iima::loc::Text(L"ReloadingExternalOrbits");break;
    case Phase::Restoring:text=iima::loc::Text(L"RehabilitatingAndConfirmingTheOriginalOrbitalOption");break;
    case Phase::Completed:text=iima::loc::Text(L"TheReloadCommandHasConfirmedThatThe");break;
    case Phase::Cancelled:text=busy ? iima::loc::Text(L"FollowUpOperationsHaveBeenCancelledPending") :
        !status.reloadSubmitted ? iima::loc::Text(L"CanceledNoReloadingOrderWasSubmitted") : iima::loc::Text(L"FollowUpHasBeenCancelledReloadingHas");break;
    case Phase::Failed:text=iima::loc::Text(L"ReloadingIsIncompleteNoAutomaticRetestingIs");break;
    }
    if(status.phase==Phase::Cancelled || status.phase==Phase::Failed) {
        if(status.reloadAcknowledged) text = text + iima::loc::Text(L"ReloadCommandConfirmed");
        if(status.targetPresentConfirmed) text = text + iima::loc::Text(L"RereadingConfirmedTargetOrbit");
        if(status.selectionRestoredConfirmed) text = text + iima::loc::Text(L"RereadConfirmedOriginalSelectionIsRestored");
        else if(status.reloadSubmitted && !busy) text = text + iima::loc::Text(L"TheOriginalChoiceIsNotConfirmed");
    }
    if(!text.empty()) text=externalReloadLabel_+L"："+text;
    if(!status.error.empty()) text = text + L"\n"+SafeReloadText(status.error,1024);
    if(ExternalReloadStatus().Text()!=text) ExternalReloadStatus().Text(text);
}
}

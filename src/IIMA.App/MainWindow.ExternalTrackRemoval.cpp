// IINA AppData.swift command allowlist / MPVCommand.swift track-remove entries.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Windows explicit inspector action, not an upstream inspector deletion panel.
// mpv cmd_track_remove only detaches an external track; no filesystem deletion.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
hstring SafeRemovalText(std::string const& value,std::size_t limit) {
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
std::optional<iima::MediaTrack> MainWindow::SelectedRemovableTrack() {
    if(!TrackInspectorExpander().IsExpanded() || !TrackInspectorCurrent()) return {};
    const auto index=TrackInspectorBox().SelectedIndex();
    if(index<0 || static_cast<std::size_t>(index)>=inspectedTracks_.size()) return {};
    auto const& identity=inspectedTracks_[index].identity;
    auto const& tracks=session_->Snapshot().tracks;
    auto found=std::find_if(tracks.begin(),tracks.end(),[&](auto const& track){return track.type==identity.type && track.id==identity.id;});
    if(found==tracks.end() || !found->external || found->id<=0 || found->externalPath.empty() ||
        (found->type!="audio" && found->type!="video" && found->type!="sub")) return {};
    return *found;
}
bool MainWindow::CanRemoveInspectedTrack() {
    return !closing_ && userDataLoaded_ && !pickerOpen_ && !externalRemovalStarting_ &&
        !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
        !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->ExternalAVLoadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy() &&
        session_->Snapshot().session==iima::SessionState::Ready && session_->Snapshot().media==iima::MediaState::Loaded && SelectedRemovableTrack().has_value();
}
winrt::fire_and_forget MainWindow::RemoveInspectedTrack() {
    auto lifetime=get_strong();
    if(!CanRemoveInspectedTrack()) co_return;
    const auto track=*SelectedRemovableTrack();
    externalRemovalRevision_=0;
    std::string description=track.title;
    if(description.empty()) {
        auto path=track.externalPath.substr(0,track.externalPath.find_first_of("?#"));
        auto slash=path.find_last_of("/\\");
        description=slash==std::string::npos ? path : path.substr(slash+1);
    }
    externalRemovalLabel_=to_hstring(track.type)+L" #"+to_hstring(track.id)+L" · "+SafeRemovalText(description,256);
    auto const& state=session_->Snapshot();
    externalRemovalGeneration_=state.mediaGeneration; externalRemovalIntent_=state.mediaOpenIntentRevision;
    externalRemovalInput_=inputGeneration_; externalRemovalObservedInput_=inputGeneration_;
    externalRemovalSelection_=trackInspectionSelectionRevision_;externalRemovalTracks_=state.tracksRevision;
    externalRemovalTrackIntent_=session_->TrackEditIntentRevision();
    auto current=[&] {
        const auto& s=session_->Snapshot();
        auto selected=SelectedRemovableTrack();
        return !closing_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
            !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
            s.media==iima::MediaState::Loaded && s.mediaGeneration==externalRemovalGeneration_ &&
            s.mediaOpenIntentRevision==externalRemovalIntent_ && inputGeneration_==externalRemovalInput_ &&
            s.tracksRevision==externalRemovalTracks_ && session_->TrackEditIntentRevision()==externalRemovalTrackIntent_ && trackInspectionSelectionRevision_==externalRemovalSelection_ &&
            selected && selected->type==track.type && selected->id==track.id && selected->externalPath==track.externalPath;
    };
    pickerOpen_=true;
    try {
        Controls::ContentDialog dialog;
        externalRemovalDialog_=dialog;
        dialog.XamlRoot(Root().XamlRoot());dialog.Title(box_value(iima::loc::Text(L"RemoveExternalTracksFromThePlayer")));
        Controls::TextBlock text;text.TextWrapping(TextWrapping::Wrap);
        text.Text(iima::loc::Text(L"Orbital2")+externalRemovalLabel_+
            iima::loc::Text(L"RemovesOnlyFromTheCurrentPlayerAnd"));
        dialog.Content(text);dialog.PrimaryButtonText(iima::loc::Text(L"RemoveTrack"));dialog.CloseButtonText(iima::loc::Text(L"Cancel"));
        dialog.DefaultButton(Controls::ContentDialogButton::Close);
        dialog.PrimaryButtonClick([&](auto const&,Controls::ContentDialogButtonClickEventArgs const& args) {
            if(!current() || session_->ExternalTrackRemovalBusy() || session_->SubtitleReloadBusy() || session_->SubtitleLoadBusy()) args.Cancel(true);
        });
        UpdateExternalTrackRemoval();
        const auto result=co_await dialog.ShowAsync();
        externalRemovalDialog_=nullptr;pickerOpen_=false;
        if(closing_) co_return;
        if(result==Controls::ContentDialogResult::Primary && current() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->ExternalAVLoadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy()) {
            externalRemovalRevision_=session_->ExternalTrackRemoval().revision+1;
            externalRemovalStarting_=true;
            try {session_->RemoveExternalTrack(track.type,track.id,track.externalPath);}
            catch(...) {ShowError(iima::loc::Text(L"AnErrorOccurredInTheOrbitalRemoval"));}
            externalRemovalStarting_=false;
            if(session_->ExternalTrackRemoval().revision!=externalRemovalRevision_) {
                externalRemovalRevision_=0;ShowError(iima::loc::Text(L"OrbitalOrPlayerStatusHasChangedAnd"));
            }
        }
    } catch(...) {
        externalRemovalDialog_=nullptr;pickerOpen_=false;externalRemovalStarting_=false;
        if(!closing_) ShowError(iima::loc::Text(L"UnableToShowOrCompleteTrackRemoval"));
    }
    if(!closing_) {UpdateExternalTrackRemoval();OpenPendingFiles();FlushActivation();}
}
void MainWindow::InvalidateExternalTrackRemoval() {
    externalRemovalRevision_=0;externalRemovalObservedInput_=inputGeneration_;
    session_->CancelExternalTrackRemoval();
    if(externalRemovalDialog_) externalRemovalDialog_.Hide();
    ExternalRemovalStatus().Text(L"");CancelExternalRemovalButton().IsEnabled(false);
}
void MainWindow::UpdateExternalTrackRemoval() {
    if(externalRemovalUpdating_ || externalRemovalStarting_) return;
    struct Guard{bool& value;~Guard(){value=false;}} guard{externalRemovalUpdating_};externalRemovalUpdating_=true;
    auto const& s=session_->Snapshot();
    const bool wrongMedia=s.mediaGeneration!=externalRemovalGeneration_ || s.mediaOpenIntentRevision!=externalRemovalIntent_ || inputGeneration_!=externalRemovalInput_;
    const bool inputChanged=inputGeneration_!=externalRemovalObservedInput_;
    externalRemovalObservedInput_=inputGeneration_;
    const bool newInput=inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty();
    if(closing_ || inputChanged || newInput || (externalRemovalRevision_ && wrongMedia)) {
        externalRemovalRevision_=0;session_->CancelExternalTrackRemoval();
    }
    if(externalRemovalDialog_ && (closing_ || wrongMedia || newInput || s.tracksRevision!=externalRemovalTracks_ || session_->TrackEditIntentRevision()!=externalRemovalTrackIntent_ ||
        trackInspectionSelectionRevision_!=externalRemovalSelection_ || !TrackInspectorExpander().IsExpanded())) externalRemovalDialog_.Hide();
    if(externalRemovalDialog_) externalRemovalDialog_.IsPrimaryButtonEnabled(!wrongMedia && !newInput &&
        s.tracksRevision==externalRemovalTracks_ && session_->TrackEditIntentRevision()==externalRemovalTrackIntent_ && trackInspectionSelectionRevision_==externalRemovalSelection_ &&
        !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->ExternalAVLoadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy());
    const auto status=session_->ExternalTrackRemoval();
    const bool busy=session_->ExternalTrackRemovalBusy();
    RemoveInspectedTrackButton().IsEnabled(CanRemoveInspectedTrack());
    CancelExternalRemovalButton().IsEnabled(!closing_ && busy && status.phase!=iima::ExternalTrackRemovalStatus::Phase::Cancelled);
    if(closing_ || !externalRemovalRevision_ || externalRemovalRevision_!=status.revision || wrongMedia) {
        ExternalRemovalStatus().Text(!closing_ && busy ? iima::loc::Text(L"WaitingForTheCompletionOfTheSubmitted2") : L"");return;
    }
    using Phase=iima::ExternalTrackRemovalStatus::Phase;
    hstring text;
    switch(status.phase) {
    case Phase::Idle: break;
    case Phase::Removing:text=iima::loc::Text(L"RemovingAndRereadingTrackLists");break;
    case Phase::Completed:text=iima::loc::Text(L"TheOrbitalListHasBeenRereadTo");break;
    case Phase::Cancelled:text=busy ? iima::loc::Text(L"FollowUpConfirmationHasBeenCancelledPending2") : status.removedConfirmed ?
        iima::loc::Text(L"TheRemovalBeforeTheCancellationHasBeen") : status.submitted ?
        iima::loc::Text(L"FollowUpConfirmationsHaveBeenCancelledRemoval") : iima::loc::Text(L"CanceledNoRemovalOrderSubmittedToPlayer");break;
    case Phase::Failed:text=status.removedConfirmed ? iima::loc::Text(L"TheCommandReportIsWrongButRereading") :
        iima::loc::Text(L"OrbitalOperationReportErrorNoAutomaticRe");break;
    }
    if(!text.empty()) text=externalRemovalLabel_+L"："+text;
    if(!status.error.empty()) text = text + L"\n"+SafeRemovalText(status.error,1024);
    if(ExternalRemovalStatus().Text()!=text) ExternalRemovalStatus().Text(text);
}
}

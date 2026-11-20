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
        iima::loc::Text(L"CurrentEncodingUnknown") : hstring{iima::loc::Text(L"CurrentEncoding") + std::wstring(to_hstring(value->second))});
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
        !session_->PlaylistEditBusy() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->ExternalAVLoadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy() && !state.mediaOriginPending &&
        state.session==iima::SessionState::Ready && state.media==iima::MediaState::Loaded;
}
void MainWindow::StartSubtitleReload(std::optional<std::string> encoding) {
    if(!CanStartSubtitleReload()) {if(!closing_) ShowError(iima::loc::Text(L"CanNotReloadSubtitlesAtThisTime"));return;}
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
    if(threw && !closing_) ShowError(iima::loc::Text(L"AnErrorOccurredWhileSubmittingTheSubtitle"));
    // An accepted transaction can fail its first worker submission and return
    // false while already owning a new revision. Retain that failure report.
    if(session_->SubtitleReload().revision!=subtitleReloadRevision_) {
        subtitleReloadRevision_=0;
        if(!closing_ && !threw) ShowError(iima::loc::Text(L"SubtitleOperationNotSubmittedCheckMediaTrack"));
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
        SubtitleReloadStatusText().Text(busy ? iima::loc::Text(L"SubtitleOperationsAreStillProcessingTheSubmitted") : L"");
        return;
    }
    hstring phase;
    switch(status.phase) {
    case Phase::Idle: phase=iima::loc::Text(L"NotStarted");break;
    case Phase::Encoding: phase=iima::loc::Text(L"SettingSubtitleEncoding");break;
    case Phase::Reading: phase=iima::loc::Text(L"ReadingTheLatestSubtitleTrack");break;
    case Phase::Reloading: phase=iima::loc::Text(L"ReloadingExternalSubtitlesInSerials");break;
    case Phase::Restoring: phase=iima::loc::Text(L"RestoreOriginalSubtitleSelection");break;
    case Phase::Completed: phase=iima::loc::Text(L"SubtitleServiceIsOver");break;
    case Phase::Cancelled: phase=busy ? iima::loc::Text(L"FollowUpCancelledPendingSubmissionOfRequest") : iima::loc::Text(L"FollowUpAborted");break;
    case Phase::Failed: phase=iima::loc::Text(L"SubtitleServiceFailed");break;
    }
    hstring text=phase+iima::loc::Text(L"ReloadingSuccessfully")+to_hstring(status.completed)+iima::loc::Text(L"Failure")+to_hstring(status.failed)+
        iima::loc::Text(L"Skip")+to_hstring(status.skipped)+iima::loc::Text(L"WhatSLeft")+to_hstring(status.remaining)+iima::loc::Text(L"Planned")+to_hstring(status.total)+L"）。";
    if(!status.error.empty()) text = text + L"\n"+to_hstring(status.error.substr(0,512));
    text = text + iima::loc::Text(L"CountsOnlyIndicateAResponseToThe");
    if(SubtitleReloadStatusText().Text()!=text) SubtitleReloadStatusText().Text(text);
}
}

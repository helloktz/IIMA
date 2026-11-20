// IINA DraggingDetect.openFromPasteboard quick iina_quickl3d LUT.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Filters/QuickLut.h"
#include <algorithm>
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
using namespace winrt;
namespace {
bool HasVideo(iima::PlaybackSnapshot const& s) {
    return s.videoId && *s.videoId>0 && std::any_of(s.tracks.begin(),s.tracks.end(),[&](auto const& track) {
        return track.type=="video" && track.id==*s.videoId && !track.albumArt;
    });
}
bool SafeLut(std::wstring const& path) {
    if (path.empty() || path.size()>16384 || !std::filesystem::path(path).is_absolute() ||
        path.find(L':',2)!=path.npos || std::any_of(path.begin(),path.end(),[](auto c){return c<32 || c==127;})) return false;
    auto normalized=path;
    std::replace(normalized.begin(),normalized.end(),L'/',L'\\');
    if (normalized.starts_with(L"\\\\?\\") || normalized.starts_with(L"\\\\.\\")) return false;
    auto ext=std::filesystem::path(path).extension().native();
    for (auto& c:ext) if (c>=L'A' && c<=L'Z') c+=L'a'-L'A';
    if (ext!=L".cube" && ext!=L".3dl" && ext!=L".dat" && ext!=L".m3d") return false;
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES &&
        !(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) &&
        std::filesystem::is_regular_file(path);
}
}
namespace winrt::IIMA::implementation {
bool MainWindow::QuickLutAvailable() const {
    auto const& s=session_->Snapshot();
    return !closing_ && userDataLoaded_ && !pickerOpen_ && !quickLutBusy_ && !inputResolving_ &&
        pendingFiles_.empty() && activationInputs_.empty() && !session_->PlaylistEditBusy() &&
        !interactiveCrop_.Active() && !cropOriginal_ && s.session==iima::SessionState::Ready &&
        s.media==iima::MediaState::Loaded && s.mediaPlaybackRestarted && HasVideo(s) &&
        s.videoFilters.complete;
}
void MainWindow::WireQuickLut() {
    auto weak=get_weak();
    ChooseQuickLutButton().Click([weak](auto const&,auto const&){if(auto self=weak.get()) self->ImportQuickLut();});
    RemoveQuickLutButton().Click([weak](auto const&,auto const&){
        if(auto self=weak.get();self && self->QuickLutAvailable()) self->SubmitQuickLut(std::nullopt);
    });
    UpdateQuickLut();
}
fire_and_forget MainWindow::ImportQuickLut(std::wstring path) {
    auto lifetime=get_strong();
    if (!QuickLutAvailable()) co_return;
    auto const generation=session_->Snapshot().mediaGeneration;
    auto const intent=session_->Snapshot().mediaOpenIntentRevision;
    auto const input=inputGeneration_;
    auto const revision=session_->Snapshot().videoFilters.revision;
    auto const playlistRevision=session_->Snapshot().playlistRevision;
    auto const editIntent=session_->PlaylistEditIntentRevision();
    auto const drop=dropReadGeneration_;
    auto current=[&] {
        auto const& s=session_->Snapshot();
        return !closing_ && userDataLoaded_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
            !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
            s.session==iima::SessionState::Ready && s.media==iima::MediaState::Loaded && HasVideo(s) &&
            s.mediaGeneration==generation && s.mediaOpenIntentRevision==intent && inputGeneration_==input &&
            s.videoFilters.revision==revision && s.playlistRevision==playlistRevision &&
            session_->PlaylistEditIntentRevision()==editIntent && dropReadGeneration_==drop;
    };
    quickLutBusy_=true;
    bool ownsPicker=path.empty();
    if(ownsPicker) pickerOpen_=true;
    UpdateQuickLut();
    std::exception_ptr failure;
    bool selected=!path.empty(), valid{};
    try {
        if (ownsPicker) {
            Windows::Storage::Pickers::FileOpenPicker picker;
            HWND hwnd{}; check_hresult(GetNativeWindowHandle(&hwnd));
            check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
            for(auto ext:{L".cube",L".3dl",L".dat",L".m3d"}) picker.FileTypeFilter().Append(ext);
            auto file=co_await picker.PickSingleFileAsync();
            if(file) {path=file.Path();selected=true;}
        }
        if(selected && current()) {
            apartment_context ui;
            co_await resume_background();
            try { valid=SafeLut(path); } catch(...) { failure=std::current_exception(); }
            try { co_await ui; } catch(...) { co_return; }
        }
    } catch(...) { failure=std::current_exception(); }
    const bool stillCurrent=current();
    if(ownsPicker) pickerOpen_=false;
    quickLutBusy_=false;
    if(stillCurrent && selected) {
        if(failure || !valid) ShowError(iima::loc::Text(L"SelectTheNormal3DLUTFile"));
        else SubmitQuickLut(to_string(path));
    }
    if(!closing_) {UpdateQuickLut();OpenPendingFiles();FlushActivation();}
}
void MainWindow::UpdateQuickLut() {
    auto const info=iima::FindQuickLutFilter(session_->Snapshot().videoFilters);
    using S=iima::QuickLutState;
    bool known=info.state!=S::Unknown && info.state!=S::Conflict;
    ChooseQuickLutButton().IsEnabled(QuickLutAvailable() && known);
    RemoveQuickLutButton().IsEnabled(QuickLutAvailable() && (info.state==S::Enabled || info.state==S::Disabled));
    std::wstring text=info.state==S::Unknown ? iima::loc::Text(L"FastLUTStatusUnknown") :
        info.state==S::Conflict ? iima::loc::Text(L"FastLUTTabConflictCheckInFilterChain") :
        info.state==S::Absent ? iima::loc::Text(L"FastLUTNotAdded") :
        info.state==S::Enabled ? iima::loc::Text(L"FastLUTEnabled") : iima::loc::Text(L"FastLUTDisabled");
    if(!info.path.empty()) text+=L"\n"+std::wstring(to_hstring(info.path));
    if(quickLutBusy_) text+=iima::loc::Text(L"IsBeingProcessedPleaseWaitForThe");
    QuickLutStatus().Text(text);
}
void MainWindow::SubmitQuickLut(std::optional<std::string> path) {
    if(!QuickLutAvailable()) return;
    auto const& state=session_->Snapshot();
    auto plan=iima::PlanQuickLut(state.videoFilters,state.videoFilters.revision,path);
    if(!plan.valid) {ShowError(to_hstring(plan.error));return;}
    auto expected=plan.after;
    const auto generation=state.mediaGeneration, intent=state.mediaOpenIntentRevision, input=inputGeneration_;
    const auto playlist=state.playlistRevision, edit=session_->PlaylistEditIntentRevision();
    quickLutBusy_=true;
    auto weak=get_weak();
    bool queued=session_->EditFiltersWithCompletion(false,std::move(plan),
        [weak,generation,intent,input,playlist,edit,expected=std::move(expected)]
        (bool success,iima::FilterChainSnapshot observed,std::string) {
            auto self=weak.get();if(!self) return;
            if(self->closing_) {self->quickLutBusy_=false;return;}
            auto const& s=self->session_->Snapshot();
            if(s.mediaGeneration!=generation || s.mediaOpenIntentRevision!=intent || self->inputGeneration_!=input ||
                s.playlistRevision!=playlist || self->session_->PlaylistEditIntentRevision()!=edit) {
                self->quickLutBusy_=false;self->UpdateQuickLut();return;
            }
            iima::FilterEditPlan check{expected,{},true,{}};
            if(success && check.FreshMatches(observed)) self->ShowError(iima::loc::Text(L"TheFastTrackLUTFilterUpdateHas"));
            else self->ShowError(iima::loc::Text(L"TheFastLUTUpdateFailedOrThe"));
            self->quickLutBusy_=false;
            if(!self->closing_) self->UpdateQuickLut();
        });
    if(queued && !closing_) UpdateQuickLut();
    if(!queued) {
        quickLutBusy_=false;UpdateQuickLut();
        ShowError(iima::loc::Text(L"FastLUTRequestsHaveNotBeenSubmittedFilter"));
    }
}

}

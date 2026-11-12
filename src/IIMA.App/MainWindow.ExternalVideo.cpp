// IINA JavascriptAPICore.TrackAPI.loadTrack -> PlayerCore.loadExternalVideoFile.
// Copyright © 2016, 2018 lhc. GPL-3.0; references/iina/LICENSE.
// Windows adds an explicit native picker; upstream entry is the plugin TrackAPI.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <algorithm>
#include <filesystem>
using namespace winrt;
namespace {
bool RegularLocalVideo(std::wstring const& value) noexcept {
    try {
        auto path=std::filesystem::path(value).lexically_normal();
        auto current=path.root_path();
        const auto drive=GetDriveTypeW(current.c_str());
        if(drive!=DRIVE_FIXED && drive!=DRIVE_REMOVABLE) return false;
        std::size_t depth{};
        for(auto const& part:path.relative_path()) {
            if(++depth>1024 || part.native().find(L':')!=std::wstring::npos) return false;
            current/=part;
            const auto attributes=GetFileAttributesW(current.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES ||
                (attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE|FILE_ATTRIBUTE_OFFLINE))) return false;
            if((attributes&FILE_ATTRIBUTE_DIRECTORY)!=(current==path ? 0u : FILE_ATTRIBUTE_DIRECTORY)) return false;
        }
        return depth!=0;
    } catch(...) {return false;}
}
}
namespace winrt::IIMA::implementation {
bool MainWindow::CanLoadExternalVideo() {
    auto const& s=session_->Snapshot();
    return !closing_ && !externalVideoStarting_ && userDataLoaded_ && !pickerOpen_ && !inputResolving_ && pendingFiles_.empty() &&
        activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
        !session_->ExternalVideoLoadBusy() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->SubtitleReloadBusy() &&
        s.session==iima::SessionState::Ready && s.media==iima::MediaState::Loaded && !s.mediaOriginPending && s.entryId>=0 && !s.path.empty();
}
fire_and_forget MainWindow::LoadExternalVideo() {
    auto lifetime=get_strong();
    if(!CanLoadExternalVideo()) co_return;
    auto const& initial=session_->Snapshot();
    const auto generation=initial.mediaGeneration,intent=initial.mediaOpenIntentRevision,input=inputGeneration_;
    const auto trackIntent=session_->TrackEditIntentRevision();
    const auto entry=initial.entryId;const auto path=initial.path;
    pickerOpen_=true;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{};check_hresult(as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.CommitButtonText(L"载入并选择视频");picker.FileTypeFilter().Append(L"*");
        auto file=co_await picker.PickSingleFileAsync();
        pickerOpen_=false;
        auto current=[&] {auto const& now=session_->Snapshot();return !closing_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
            !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() && !session_->ExternalVideoLoadBusy() &&
            !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->SubtitleReloadBusy() && now.session==iima::SessionState::Ready &&
            now.media==iima::MediaState::Loaded && !now.mediaOriginPending && generation==now.mediaGeneration && intent==now.mediaOpenIntentRevision &&
            input==inputGeneration_ && trackIntent==session_->TrackEditIntentRevision() && entry==now.entryId && path==now.path;};
        if(file && current()) {
            auto value=file.Path();
            std::wstring normalized(value);std::replace(normalized.begin(),normalized.end(),L'/',L'\\');
            if(value.size()<3 || normalized[1]!=L':' || normalized[2]!=L'\\' ||
                !((normalized[0]>=L'A' && normalized[0]<=L'Z') || (normalized[0]>=L'a' && normalized[0]<=L'z')) || value.size()>32767 || normalized.starts_with(L"\\\\?\\") || normalized.starts_with(L"\\\\.\\") ||
                std::any_of(value.begin(),value.end(),[](wchar_t c){return c<32 || c==127;})) {
                ShowError(L"所选视频没有受支持的普通文件路径。");
            } else {
                pickerOpen_=true; // Keep the input picker lane owned during validation.
                auto ui=apartment_context();
                co_await resume_background();
                // Point-in-time check only; mpv later reopens this path.
                const bool regular=RegularLocalVideo(normalized);
                try { co_await ui; } catch(...) {co_return;}
                pickerOpen_=false;
                if(!current()) {if(!closing_){UpdateExternalVideoLoad();OpenPendingFiles();FlushActivation();}co_return;}
                if(!regular) {ShowError(L"请选择本地普通视频文件；不支持目录、链接或脱机文件。");UpdateExternalVideoLoad();OpenPendingFiles();FlushActivation();co_return;}
                externalVideoGeneration_=generation;externalVideoIntent_=intent;externalVideoInput_=input;
                externalVideoObservedInput_=input;externalVideoRevision_=session_->ExternalVideoLoad().revision+1;
                externalVideoStarting_=true;
                try { session_->LoadExternalVideo(to_string(value)); }
                catch(...) { externalVideoStarting_=false;throw; }
                externalVideoStarting_=false;
                if(session_->ExternalVideoLoad().revision!=externalVideoRevision_) {
                    externalVideoRevision_=0;ShowError(L"未开始载入外部视频；播放器可能正在切换或处理另一笔轨道操作。");
                }
            }
        }
        // A stale picker is discarded silently; never report it in a newer media.
    } catch(hresult_error const& error) {
        if(!closing_ && input==inputGeneration_ && generation==session_->Snapshot().mediaGeneration && intent==session_->Snapshot().mediaOpenIntentRevision)
            ShowError(error.message());
    } catch(...) {
        if(!closing_ && input==inputGeneration_ && generation==session_->Snapshot().mediaGeneration && intent==session_->Snapshot().mediaOpenIntentRevision)
            ShowError(L"无法完成外部视频选择或提交；已提交操作不能撤回。");
    }
    pickerOpen_=false;externalVideoStarting_=false;
    if(!closing_) {UpdateExternalVideoLoad();OpenPendingFiles();FlushActivation();}
}
void MainWindow::InvalidateExternalVideoLoad() {
    externalVideoRevision_=0;externalVideoObservedInput_=inputGeneration_;
    session_->CancelExternalVideoLoad();
    ExternalVideoStatus().Text(L"");
}
void MainWindow::UpdateExternalVideoLoad() {
    if(externalVideoUpdating_ || externalVideoStarting_) return;
    struct Guard {bool& value;~Guard(){value=false;}} guard{externalVideoUpdating_};externalVideoUpdating_=true;
    auto const& s=session_->Snapshot();
    const bool wrong=externalVideoGeneration_!=s.mediaGeneration || externalVideoIntent_!=s.mediaOpenIntentRevision || externalVideoInput_!=inputGeneration_;
    const bool inputChanged=externalVideoObservedInput_!=inputGeneration_;externalVideoObservedInput_=inputGeneration_;
    if(closing_ || inputChanged || inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty() || (externalVideoRevision_ && wrong)) {
        externalVideoRevision_=0;session_->CancelExternalVideoLoad();
    }
    const auto status=session_->ExternalVideoLoad();const bool busy=session_->ExternalVideoLoadBusy();
    LoadExternalVideoButton().IsEnabled(CanLoadExternalVideo());
    CancelExternalVideoButton().IsEnabled(!closing_ && busy && status.phase!=iima::ExternalVideoLoadStatus::Phase::Cancelled);
    if(closing_ || wrong || !externalVideoRevision_ || externalVideoRevision_!=status.revision) {
        ExternalVideoStatus().Text(!closing_ && busy ? L"正在等待已提交的视频轨道操作结束…" : L"");return;
    }
    using Phase=iima::ExternalVideoLoadStatus::Phase;
    hstring text;
    switch(status.phase) {
    case Phase::Idle:break;
    case Phase::Loading:text=L"正在载入并确认所选外部视频轨道…";break;
    case Phase::Completed:text=L"已重新读取并确认目标文件的外部视频轨道现已选择。此载入操作不主动切换播放／暂停；画面以实际呈现为准。";break;
    case Phase::Cancelled:text=busy ? L"已取消后续确认，等待在途请求结束；载入可能已提交。" : status.selectedConfirmed ?
        L"取消前已提交载入；重新读取确认外部视频轨道已选择，不能撤回。" : status.submitted ?
        L"已取消后续确认；载入已提交但选择结果未确认，请检查当前轨道。" : L"已取消，未提交视频载入命令。";break;
    case Phase::Failed:text=status.selectedConfirmed ? L"命令报告错误，但重新读取确认外部视频轨道已选择；未自动重试。" :
        L"外部视频操作失败或选择结果未确认；未自动重试，已提交操作不能撤回。";break;
    }
    if(!status.error.empty()) {
        try {
            auto wide=to_hstring(status.error);std::wstring safe;
            for(auto c:wide) {if(safe.size()>=1024) break;safe+=c<32 || c==127 || (c>=0x2028 && c<=0x202e) || (c>=0x2066 && c<=0x2069) ? L'?' : c;}
            if(!safe.empty() && safe.back()>=0xd800 && safe.back()<=0xdbff) safe.pop_back();
            text+=L"\n"+hstring(safe);
        } catch(...) {}
    }
    if(ExternalVideoStatus().Text()!=text) ExternalVideoStatus().Text(text);
}
}

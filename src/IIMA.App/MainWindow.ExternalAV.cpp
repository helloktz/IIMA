// IINA SidebarAudioPane.loadExternalAudioAction / JavascriptAPICore.TrackAPI.loadTrack
// -> PlayerCore.loadExternalAudioFile / loadExternalVideoFile.
// Copyright © 2016, 2018 lhc and contributors. GPL-3.0; references/iina/LICENSE.
// Shared Windows picker/transaction projection; video native entry is an adaptation.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <algorithm>
#include <filesystem>
using namespace winrt;
namespace winrt::IIMA::implementation {
bool MainWindow::RegularLocalExternalFile(std::wstring const& value) noexcept {
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
bool MainWindow::CanLoadExternalAV() {
    auto const& s=session_->Snapshot();
    return !closing_ && !externalAVStarting_ && userDataLoaded_ && !pickerOpen_ && !inputResolving_ && pendingFiles_.empty() &&
        activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
        !session_->ExternalAVLoadBusy() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy() &&
        s.session==iima::SessionState::Ready && s.media==iima::MediaState::Loaded && !s.mediaOriginPending && s.entryId>=0 && !s.path.empty();
}
fire_and_forget MainWindow::LoadExternalAV(bool video) {
    const hstring kind=video ? L"视频" : L"音频";
    const std::string type=video ? "video" : "audio";
    auto lifetime=get_strong();
    if(!CanLoadExternalAV()) co_return;
    auto const& initial=session_->Snapshot();
    const auto generation=initial.mediaGeneration,intent=initial.mediaOpenIntentRevision,input=inputGeneration_;
    const auto trackIntent=session_->TrackEditIntentRevision();
    const auto entry=initial.entryId;const auto path=initial.path;
    pickerOpen_=true;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{};check_hresult(GetNativeWindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.CommitButtonText(L"载入并选择"+kind);picker.FileTypeFilter().Append(L"*");
        auto file=co_await picker.PickSingleFileAsync();
        pickerOpen_=false;
        auto current=[&] {auto const& now=session_->Snapshot();return !closing_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
            !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() && !session_->ExternalAVLoadBusy() &&
            !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() && !session_->SubtitleReloadBusy() && !session_->SubtitleLoadBusy() && now.session==iima::SessionState::Ready &&
            now.media==iima::MediaState::Loaded && !now.mediaOriginPending && generation==now.mediaGeneration && intent==now.mediaOpenIntentRevision &&
            input==inputGeneration_ && trackIntent==session_->TrackEditIntentRevision() && entry==now.entryId && path==now.path;};
        if(file && current()) {
            auto value=file.Path();
            std::wstring normalized(value);std::replace(normalized.begin(),normalized.end(),L'/',L'\\');
            if(value.size()<3 || normalized[1]!=L':' || normalized[2]!=L'\\' ||
                !((normalized[0]>=L'A' && normalized[0]<=L'Z') || (normalized[0]>=L'a' && normalized[0]<=L'z')) || value.size()>32767 || normalized.starts_with(L"\\\\?\\") || normalized.starts_with(L"\\\\.\\") ||
                std::any_of(value.begin(),value.end(),[](wchar_t c){return c<32 || c==127;})) {
                ShowError(L"所选"+kind+L"没有受支持的普通文件路径。");
            } else {
                pickerOpen_=true; // Keep the input picker lane owned during validation.
                auto ui=apartment_context();
                co_await resume_background();
                // Point-in-time check only; mpv later reopens this path.
                const bool regular=RegularLocalExternalFile(normalized);
                try { co_await ui; } catch(...) {co_return;}
                pickerOpen_=false;
                if(!current()) {if(!closing_){UpdateExternalAVLoad();OpenPendingFiles();FlushActivation();}co_return;}
                if(!regular) {ShowError(L"请选择本地普通"+kind+L"文件；不支持目录、链接或脱机文件。");UpdateExternalAVLoad();OpenPendingFiles();FlushActivation();co_return;}
                externalAVType_=type;externalAVGeneration_=generation;externalAVIntent_=intent;externalAVInput_=input;
                externalAVObservedInput_=input;externalAVRevision_=session_->ExternalAVLoad().revision+1;
                externalAVStarting_=true;
                try { if(video) session_->LoadExternalVideo(to_string(value)); else session_->LoadExternalAudio(to_string(value)); }
                catch(...) { externalAVStarting_=false;throw; }
                externalAVStarting_=false;
                if(session_->ExternalAVLoad().revision!=externalAVRevision_) {
                    externalAVRevision_=0;ShowError(L"未开始载入外部"+kind+L"；播放器可能正在切换或处理另一笔轨道操作。");
                }
            }
        }
        // A stale picker is discarded silently; never report it in a newer media.
    } catch(hresult_error const& error) {
        if(!closing_ && input==inputGeneration_ && generation==session_->Snapshot().mediaGeneration && intent==session_->Snapshot().mediaOpenIntentRevision)
            ShowError(error.message());
    } catch(...) {
        if(!closing_ && input==inputGeneration_ && generation==session_->Snapshot().mediaGeneration && intent==session_->Snapshot().mediaOpenIntentRevision)
            ShowError(L"无法完成外部"+kind+L"选择或提交；已提交操作不能撤回。");
    }
    pickerOpen_=false;externalAVStarting_=false;
    if(!closing_) {UpdateExternalAVLoad();OpenPendingFiles();FlushActivation();}
}
void MainWindow::InvalidateExternalAVLoad() {
    externalAVRevision_=0;externalAVObservedInput_=inputGeneration_;
    session_->CancelExternalAVLoad();
    ExternalVideoStatus().Text(L"");ExternalAudioStatus().Text(L"");
}
void MainWindow::UpdateExternalAVLoad() {
    if(externalAVUpdating_ || externalAVStarting_) return;
    struct Guard {bool& value;~Guard(){value=false;}} guard{externalAVUpdating_};externalAVUpdating_=true;
    auto const& s=session_->Snapshot();
    const bool wrong=externalAVGeneration_!=s.mediaGeneration || externalAVIntent_!=s.mediaOpenIntentRevision || externalAVInput_!=inputGeneration_;
    const bool inputChanged=externalAVObservedInput_!=inputGeneration_;externalAVObservedInput_=inputGeneration_;
    if(closing_ || inputChanged || inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty() || (externalAVRevision_ && wrong)) {
        externalAVRevision_=0;session_->CancelExternalAVLoad();
    }
    const auto status=session_->ExternalAVLoad();const bool busy=session_->ExternalAVLoadBusy();
    const bool canLoad=CanLoadExternalAV();
    LoadExternalVideoButton().IsEnabled(canLoad);LoadExternalAudioButton().IsEnabled(canLoad);
    const bool cancel=!closing_ && busy && status.phase!=iima::ExternalAVLoadStatus::Phase::Cancelled;
    CancelExternalVideoButton().IsEnabled(cancel && status.type=="video");
    CancelExternalAudioButton().IsEnabled(cancel && status.type=="audio");
    auto project=[&](hstring const& text) {
        const auto video=status.type=="video" ? text : hstring{};
        const auto audio=status.type=="audio" ? text : hstring{};
        if(ExternalVideoStatus().Text()!=video) ExternalVideoStatus().Text(video);
        if(ExternalAudioStatus().Text()!=audio) ExternalAudioStatus().Text(audio);
    };
    if(closing_ || wrong || !externalAVRevision_ || externalAVRevision_!=status.revision || externalAVType_!=status.type) {
        project(!closing_ && busy ? hstring(L"正在等待已提交的轨道载入操作结束…") : hstring{});return;
    }
    const hstring kind=status.type=="audio" ? L"音频" : L"视频";
    using Phase=iima::ExternalAVLoadStatus::Phase;
    hstring text;
    switch(status.phase) {
    case Phase::Idle:break;
    case Phase::Loading:text=L"正在载入并确认所选外部"+kind+L"轨道…";break;
    case Phase::Completed:text=L"已重新读取并确认目标文件的外部"+kind+L"轨道现已选择。此载入操作不主动切换播放／暂停；不代表声音或画面已呈现。";break;
    case Phase::Cancelled:text=busy ? L"已取消后续确认，等待在途请求结束；载入可能已提交。" : status.selectedConfirmed ?
        L"取消前已提交载入；重新读取确认目标外部轨道已选择，不能撤回。" : status.submitted ?
        L"已取消后续确认；载入已提交但选择结果未确认，请检查当前轨道。" : L"已取消；未收到载入提交确认，请检查当前轨道。";break;
    case Phase::Failed:text=status.selectedConfirmed ? L"载入事务未完整完成，但重新读取已确认目标外部轨道已选择；未自动重试。" : status.commandAcknowledged ?
        L"载入命令已确认，但后续轨道选择未确认；未自动重试，已提交操作不能撤回。" :
        L"外部轨道载入失败或结果未确认；未自动重试，已提交操作不能撤回。";break;
    }
    if(status.phase==Phase::Cancelled && status.commandAcknowledged) text = text + L" 载入命令已确认。";
    if(!status.error.empty()) {
        try {
            auto wide=to_hstring(status.error);std::wstring safe;
            for(auto c:wide) {if(safe.size()>=1024) break;safe+=c<32 || (c>=0x7f && c<=0x9f) || c==0x061c || c==0x200e || c==0x200f || (c>=0x2028 && c<=0x202e) || (c>=0x2066 && c<=0x2069) ? L'?' : c;}
            if(!safe.empty() && safe.back()>=0xd800 && safe.back()<=0xdbff) safe.pop_back();
            text = text + L"\n"+hstring(safe);
        } catch(...) {}
    }
    project(text);
}
}

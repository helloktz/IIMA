// IINA SidebarSubtitlesPane / PlayerCore.loadExternalSubFile.
// Copyright © 2016 lhc and contributors. GPL-3.0; references/iina/LICENSE.
// Windows picker and one bounded, observed subtitle-loading transaction.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <algorithm>
using namespace winrt;
namespace winrt::IIMA::implementation {
bool MainWindow::CanStartSubtitleLoad() const {
    auto const& s=session_->Snapshot();
    return !closing_ && userDataLoaded_ && !pickerOpen_ && !subtitleLoadStarting_ && !inputResolving_ &&
        pendingFiles_.empty() && activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ &&
        !session_->PlaylistEditBusy() && !session_->SubtitleLoadBusy() && !session_->SubtitleReloadBusy() &&
        !session_->ExternalAVLoadBusy() && !session_->ExternalTrackRemovalBusy() && !session_->ExternalTrackReloadBusy() &&
        s.session==iima::SessionState::Ready && s.media==iima::MediaState::Loaded && !s.mediaOriginPending && s.entryId>=0 && !s.path.empty();
}
bool MainWindow::StartSubtitleLoad(std::vector<iima::SubtitleLoadItem> items) {
    if(!CanStartSubtitleLoad() || items.empty()) return false;
    auto const& s=session_->Snapshot();
    subtitleLoadGeneration_=s.mediaGeneration;subtitleLoadIntent_=s.mediaOpenIntentRevision;
    subtitleLoadInput_=inputGeneration_;subtitleLoadObservedInput_=inputGeneration_;
    const auto expected=session_->SubtitleLoad().revision+1;
    subtitleLoadRevision_=expected;
    subtitleLoadStarting_=true;
    bool accepted{};
    try {
        if(items.size()==1) accepted=session_->AddSubtitle(std::move(items[0].path),items[0].select);
        else accepted=session_->AddSubtitles(std::move(items));
    } catch(...) {
        subtitleLoadStarting_=false;
        const bool queued=session_->SubtitleLoad().revision==expected && session_->SubtitleLoadBusy();
        if(!queued) subtitleLoadRevision_=0;
        UpdateSubtitleLoad();return queued; // Notify may throw after the worker request was accepted.
    }
    subtitleLoadStarting_=false;
    if(session_->SubtitleLoad().revision!=subtitleLoadRevision_) subtitleLoadRevision_=0;
    UpdateSubtitleLoad();return accepted;
}
fire_and_forget MainWindow::OpenSubtitle() {
    auto lifetime=get_strong();
    if(!CanStartSubtitleLoad()) co_return;
    const auto generation=session_->Snapshot().mediaGeneration,intent=session_->Snapshot().mediaOpenIntentRevision,input=inputGeneration_;
    const auto track=session_->TrackEditIntentRevision();const auto entry=session_->Snapshot().entryId;const auto path=session_->Snapshot().path;
    auto same=[&] {auto const& s=session_->Snapshot();return !closing_ && generation==s.mediaGeneration && intent==s.mediaOpenIntentRevision &&
        input==inputGeneration_ && entry==s.entryId && path==s.path && track==session_->TrackEditIntentRevision();};
    pickerOpen_=true;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{};check_hresult(as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.CommitButtonText(L"载入并选择字幕");picker.FileTypeFilter().Append(L"*");
        auto file=co_await picker.PickSingleFileAsync();
        pickerOpen_=false;
        if(file && same() && CanStartSubtitleLoad()) {
            auto value=file.Path();std::wstring local(value);std::replace(local.begin(),local.end(),L'/',L'\\');
            if(local.size()<4 || local.size()>32767 || local[1]!=L':' || local[2]!=L'\\' ||
                !((local[0]>=L'A' && local[0]<=L'Z') || (local[0]>=L'a' && local[0]<=L'z')) ||
                std::any_of(local.begin(),local.end(),[](wchar_t c){return c<32 || c==127;})) {
                ShowError(L"请选择具有普通本地盘符路径的字幕文件。");
            } else {
                pickerOpen_=true;auto ui=apartment_context();
                co_await resume_background();
                const bool regular=RegularLocalExternalFile(local);
                try {co_await ui;} catch(...) {co_return;}
                pickerOpen_=false;
                if(same() && CanStartSubtitleLoad()) {
                    if(!regular) ShowError(L"请选择本地普通字幕文件；不支持目录、链接或脱机文件。");
                    else if(!StartSubtitleLoad({{to_string(value),true}})) ShowError(L"字幕加载未确认提交；请检查载入状态，已提交操作不能撤回，未自动重试。");
                }
            }
        }
    } catch(...) {if(same()) ShowError(L"无法完成字幕选择或提交；已提交操作不能撤回。");}
    pickerOpen_=false;
    if(!closing_) {UpdateSubtitleLoad();OpenPendingFiles();FlushActivation();}
}
void MainWindow::InvalidateSubtitleLoad() {
    subtitleLoadRevision_=0;subtitleLoadObservedInput_=inputGeneration_;
    session_->CancelSubtitleLoad();SubtitleLoadStatusText().Text(L"");CancelSubtitleLoadButton().IsEnabled(false);
}
void MainWindow::UpdateSubtitleLoad() {
    if(subtitleLoadStarting_ || subtitleLoadUpdating_) return;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{subtitleLoadUpdating_};subtitleLoadUpdating_=true;
    auto const& s=session_->Snapshot();
    const bool wrong=s.mediaGeneration!=subtitleLoadGeneration_ || s.mediaOpenIntentRevision!=subtitleLoadIntent_ || inputGeneration_!=subtitleLoadInput_;
    const bool changed=inputGeneration_!=subtitleLoadObservedInput_;subtitleLoadObservedInput_=inputGeneration_;
    if(closing_ || changed || inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty() || (subtitleLoadRevision_ && wrong)) {
        subtitleLoadRevision_=0;session_->CancelSubtitleLoad();
    }
    const auto status=session_->SubtitleLoad();const bool busy=session_->SubtitleLoadBusy();
    SubtitleButton().IsEnabled(CanStartSubtitleLoad());
    CancelSubtitleLoadButton().IsEnabled(!closing_ && busy && status.phase!=iima::SubtitleLoadStatus::Phase::Cancelled);
    if(closing_ || wrong || !subtitleLoadRevision_ || subtitleLoadRevision_!=status.revision) {
        SubtitleLoadStatusText().Text(!closing_ && busy ? L"等待在途字幕载入结束…" : L"");return;
    }
    using Phase=iima::SubtitleLoadStatus::Phase;hstring text;
    switch(status.phase) {
    case Phase::Idle:break;
    case Phase::Loading:text=L"正在逐项载入并确认字幕…";break;
    case Phase::Completed:text=L"字幕批次处理完成；确认仅指轨道事实，不代表字幕已绘制。";break;
    case Phase::Cancelled:text=L"已取消后续字幕载入；已提交项不能撤回。";break;
    case Phase::Failed:text=L"字幕载入未完整完成；未自动重试，已加载项不回滚。";break;
    }
    if(!text.empty()) text+=L" 已处理 "+to_hstring(status.processed)+L"/"+to_hstring(status.total)+
        L"，提交 "+to_hstring(status.submitted)+L"，确认 "+to_hstring(status.confirmed)+L"（含已有跳过），已有而跳过 "+to_hstring(status.skipped)+L"，失败 "+to_hstring(status.failed)+L"，未结项 "+to_hstring(status.total>status.processed ? status.total-status.processed : 0)+L"。";
    if(!status.error.empty()) try {
        auto wide=to_hstring(status.error);std::wstring safe;
        for(auto c:wide) {if(safe.size()>=1024) break;safe+=c<32 || (c>=0x7f && c<=0x9f) || c==0x061c || c==0x200e || c==0x200f || (c>=0x2028 && c<=0x202e) || (c>=0x2066 && c<=0x2069) ? L'?' : c;}
        if(!safe.empty() && safe.back()>=0xd800 && safe.back()<=0xdbff) safe.pop_back();text+=L"\n"+hstring(safe);
    } catch(...) {}
    if(SubtitleLoadStatusText().Text()!=text) SubtitleLoadStatusText().Text(text);
}
}

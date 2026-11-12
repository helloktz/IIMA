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
            HWND hwnd{}; check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
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
        if(failure || !valid) ShowError(L"请选择有效绝对路径下的普通3D LUT文件；不接受设备、链接或目录。");
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
    std::wstring text=info.state==S::Unknown ? L"快捷LUT：状态未知" :
        info.state==S::Conflict ? L"快捷LUT：标签冲突，请在滤镜链中检查；不会自动覆盖" :
        info.state==S::Absent ? L"快捷LUT：未添加" :
        info.state==S::Enabled ? L"快捷LUT：已启用" : L"快捷LUT：已停用";
    if(!info.path.empty()) text+=L"\n"+std::wstring(to_hstring(info.path));
    if(quickLutBusy_) text+=L"\n正在处理，请等待播放器读取结果。";
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
            if(success && check.FreshMatches(observed)) self->ShowError(L"快捷LUT滤镜链更新已由播放器读回确认。");
            else self->ShowError(L"快捷LUT更新失败或结果未能确认，未自动重试；已提交操作不能撤回，请检查当前滤镜链。");
            self->quickLutBusy_=false;
            if(!self->closing_) self->UpdateQuickLut();
        });
    if(queued && !closing_) UpdateQuickLut();
    if(!queued) {
        quickLutBusy_=false;UpdateQuickLut();
        ShowError(L"快捷LUT请求未提交；滤镜链或播放器状态已变化，请重新操作。");
    }
}

}

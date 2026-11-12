// IINA InitialWindowController.loadLastPlaybackInfo/reloadData/openRecentItemFromTable.
// Copyright © 2017 lhc. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Input/LocalInputPolicy.h"
#include "../IIMA.Core/Media/VideoTime.h"
#include <algorithm>
#include <filesystem>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool SameHistory(iima::HistoryEntry const& a,iima::HistoryEntry const& b) {
    return a.path==b.path && a.position==b.position && a.duration==b.duration && a.playedAt==b.playedAt;
}
}
namespace winrt::IIMA::implementation {
bool MainWindow::InitialWindowEligible() const {
    return userDataLoaded_ && userData_.settings.recordHistory &&
        CanReuseForInput() && !interactiveCrop_.Active() && !cropOriginal_;
}
void MainWindow::WireInitialWindow() {
    auto weak=get_weak();
    InitialOpenFileButton().Click([weak](auto const&,auto const&) {if(auto self=weak.get()) self->OpenFile();});
    InitialOpenUrlButton().Click([weak](auto const&,auto const&) {if(auto self=weak.get()) self->OpenNetworkUrl();});
}
void MainWindow::UpdateInitialWindow(bool invalidate) {
    if(invalidate) ++initialWindowEpoch_;
    if(!InitialWindowEligible()) {
        if(initialWindowVisible_) ++initialWindowEpoch_;
        initialWindowVisible_=false;
        initialWindowEntries_.clear();
        InitialRecentItems().Children().Clear();
        InitialWindowPanel().Visibility(Visibility::Collapsed);
        auto media=session_->Snapshot().media;
        EmptyLabel().Visibility(media==iima::MediaState::Loaded || media==iima::MediaState::Ended ? Visibility::Collapsed : Visibility::Visible);
        return;
    }
    std::vector<iima::HistoryEntry> entries;
    // Share the existing bounded, cancellable local fixed-disk availability
    // worker. Unknown is not missing and never becomes a clickable startup row.
    for(std::size_t i=0;i<std::min<std::size_t>(12,userData_.history.size());++i) {
        auto const& entry=userData_.history[i];
        auto available=historyAvailability_.find(entry.path);
        if(available==historyAvailability_.end() || available->second!=iima::HistoryAvailability::Available) continue;
        try {
            std::wstring path{to_hstring(entry.path)};
            if(iima::AcceptExplicitFile(path) && !iima::IsPlaylistPath(path)) entries.push_back(entry);
        } catch(...) { /* Invalid historical UTF-8 is not a startup action. */ }
    }
    bool resume=userData_.settings.resumePosition;
    bool unchanged=initialWindowVisible_ && initialWindowResume_==resume && entries.size()==initialWindowEntries_.size() &&
        std::equal(entries.begin(),entries.end(),initialWindowEntries_.begin(),SameHistory);
    InitialWindowPanel().Visibility(Visibility::Visible);
    EmptyLabel().Visibility(Visibility::Collapsed);
    if(unchanged && !invalidate) return;
    ++initialWindowEpoch_;
    initialWindowVisible_=true;initialWindowResume_=resume;initialWindowEntries_=entries;
    InitialRecentItems().Children().Clear();
    auto weak=get_weak();const auto epoch=initialWindowEpoch_;
    for(auto const& entry:entries) {
        Controls::Button button;
        auto name=std::filesystem::path(std::wstring(to_hstring(entry.path))).filename().native();
        std::wstring caption=name;
        // Never promote an older surviving entry to "last played" when the
        // newest entry is missing. Resume is owned by ApplyUserData, not a seek here.
        if(!userData_.history.empty() && SameHistory(entry,userData_.history.front())) {
            if(auto position=iima::ResumePosition(userData_,entry.path))
                caption=L"继续上次播放："+name+L"  "+std::wstring(to_hstring(iima::VideoTime(*position).StringRepresentation()));
        }
        Controls::TextBlock label;label.Text(caption);label.TextWrapping(TextWrapping::Wrap);
        button.Content(label);button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.Click([weak,entry,epoch](auto const&,auto const&) {
            if(auto self=weak.get()) self->OpenInitialHistory(entry,epoch);
        });
        InitialRecentItems().Children().Append(button);
    }
    InitialRecentHint().Text(entries.empty() ? L"暂无已确认可用的本地最近项目。可从侧栏查看完整历史或刷新文件状态。" :
        L"最近12条记录中的可用本地文件。恢复位置遵循设置；完整历史在侧栏。 ");
}
void MainWindow::OpenInitialHistory(iima::HistoryEntry entry,std::uint64_t epoch) {
    if(!InitialWindowEligible() || epoch!=initialWindowEpoch_ ||
        std::none_of(initialWindowEntries_.begin(),initialWindowEntries_.end(),[&](auto const& e){return SameHistory(e,entry);})) return;
    inputStop_.request_stop();
    auto input=++inputGeneration_;
    InvalidateSubtitleReload();
    const auto media=session_->Snapshot().mediaGeneration,intent=session_->Snapshot().mediaOpenIntentRevision;
    const auto playlist=session_->Snapshot().playlistRevision,edit=session_->PlaylistEditIntentRevision();
    const auto resume=userData_.settings.resumePosition;
    inputResolving_=true;
    UpdateInitialWindow();
    const auto operation=initialWindowEpoch_;
    auto weak=get_weak();
    try {
        std::vector<std::wstring> paths{std::wstring(to_hstring(entry.path))};
        inputStop_=iima::LocalInputResolver::Resolve(std::move(paths),DispatcherQueue(),
            [weak,input,media,intent,playlist,edit,resume,operation,entry=std::move(entry)](iima::LocalInputResult result) {
                auto self=weak.get();
                if(!self || self->closing_ || self->inputGeneration_!=input) return;
                self->inputResolving_=false;
                auto const& state=self->session_->Snapshot();
                if(result.cancelled || self->initialWindowEpoch_!=operation || !self->userData_.settings.recordHistory ||
                    self->userData_.settings.resumePosition!=resume || self->pickerOpen_ || self->inputAssigned_ ||
                    self->interactiveCrop_.Active() || self->cropOriginal_ || !self->pendingFiles_.empty() ||
                    !self->activationInputs_.empty() || self->session_->PlaylistEditBusy() ||
                    state.mediaGeneration!=media || state.mediaOpenIntentRevision!=intent ||
                    state.playlistRevision!=playlist || self->session_->PlaylistEditIntentRevision()!=edit ||
                    std::none_of(self->userData_.history.begin(),self->userData_.history.end(),[&](auto const& e){return SameHistory(e,entry);})) {self->UpdateInitialWindow();return;}
                bool samePath=false;
                try {
                    samePath=result.paths.size()==1 &&
                        std::filesystem::path(std::wstring(to_hstring(result.paths.front()))).lexically_normal()==
                        std::filesystem::path(std::wstring(to_hstring(entry.path))).lexically_normal();
                } catch(...) {}
                if(!samePath) {
                    self->ShowError(L"该最近项目已不可用或不是普通媒体文件；未打开，请使用打开文件重新选择。");self->UpdateInitialWindow();return;
                }
                // Deliberate history provenance: no sibling auto expansion,
                // no global new-window policy, no manual resume seek.
                self->ReceiveResolvedInput({entry.path},false);
            },true);
    } catch(...) {inputResolving_=false;ShowError(L"无法解析最近项目，未打开。");UpdateInitialWindow();}
}
}

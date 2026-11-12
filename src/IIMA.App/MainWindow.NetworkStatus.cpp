// IINA OpenURLWindowController loading, stop and retry, owned by the actual
// receiving player rather than a global URL dialog. Copyright (c) 2018 lhc
// and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Network/MediaUrl.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireNetworkOpen() {
    auto weak=get_weak();
    CancelNetworkOpenButton().Click([weak](auto const&,auto const&) { if(auto self=weak.get()) self->CancelNetworkOpen(); });
    RetryNetworkOpenButton().Click([weak](auto const&,auto const&) { if(auto self=weak.get()) self->RetryNetworkOpen(); });
    EditNetworkOpenButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get()) self->OpenNetworkUrl(false,true);
    });
    DismissNetworkOpenButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get(); self && self->networkOpenFailed_) {
            self->networkOpenUrl_.clear(); self->UpdateNetworkOpen(self->session_->Snapshot());
        }
    });
}
void MainWindow::BeginNetworkOpen(std::vector<std::string> const& paths) {
    networkOpenUrl_.clear(); networkOpenPending_=false; networkOpenFailed_=false; networkOpenIntent_=0;
    if(paths.size()==1 && iima::IsNetworkMediaPath(paths.front())) {
        networkOpenUrl_=paths.front();
        networkOpenInputRevision_=inputGeneration_;
        networkOpenBaselineIntent_=session_->Snapshot().mediaOpenIntentRevision;
        networkOpenPending_=true;
    }
    UpdateNetworkOpen(session_->Snapshot());
}
void MainWindow::BindNetworkOpenAttempt(std::vector<std::string> const& paths,bool append) {
    if (networkOpenUrl_.empty()) return;
    if (append || paths.size()!=1 || paths.front()!=networkOpenUrl_ || inputGeneration_!=networkOpenInputRevision_) {
        networkOpenUrl_.clear(); return;
    }
    networkOpenPending_=false;
    networkOpenIntent_=session_->Snapshot().mediaOpenIntentRevision+1;
}
void MainWindow::UpdateNetworkOpen(iima::PlaybackSnapshot const& state) {
    if(musicVolumeOpen_ && !MusicVolumeContextValid()) CloseMusicVolume();
    if(trackInspectorRevision_ && trackInspectorInput_!=inputGeneration_) ClearTrackInspector();
    UpdateScrubbing();
    UpdateInitialWindow();
    using Phase=iima::OpenAttemptPhase;
    if (closing_ || inputGeneration_!=networkOpenInputRevision_) networkOpenUrl_.clear();
    bool waiting=networkOpenPending_, redirect=false;
    if (!networkOpenUrl_.empty()) {
        if ((networkOpenPending_ || !networkOpenIntent_) && state.mediaOpenIntentRevision!=networkOpenBaselineIntent_) {
            networkOpenUrl_.clear();
        } else if (networkOpenPending_) {
            if (state.session==iima::SessionState::Failed || state.session==iima::SessionState::Closing ||
                state.session==iima::SessionState::Closed ||
                (state.session==iima::SessionState::Ready && !state.compositionAvailable)) {
                networkOpenPending_=false; networkOpenFailed_=true; waiting=false;
            }
        } else if (networkOpenIntent_) {
            auto const& attempt=state.networkOpenAttempt;
            if (state.mediaOpenIntentRevision>networkOpenIntent_ ||
                (attempt.intent==networkOpenIntent_ && (attempt.phase==Phase::Cancelled || attempt.phase==Phase::Superseded || attempt.phase==Phase::Loaded))) {
                networkOpenUrl_.clear();
            } else if (attempt.intent==networkOpenIntent_) {
                networkOpenFailed_=attempt.phase==Phase::Failed;
                waiting=attempt.phase==Phase::Pending || attempt.phase==Phase::Loading || attempt.phase==Phase::RedirectWaiting;
                redirect=attempt.phase==Phase::RedirectWaiting;
            } else {
                networkOpenFailed_=true; // Rejected before an attempt existed.
                networkOpenIntent_=0;
            }
        }
    }
    const bool visible=!networkOpenUrl_.empty();
    NetworkOpenPanel().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    if (!visible) return;
    NetworkOpenStatus().Text(networkOpenFailed_ ? L"网络媒体打开失败。可重试原地址或编辑后在此窗口重试。" :
        redirect ? L"网络媒体正在重定向或等待确认，可取消。" : L"正在此窗口打开网络媒体…");
    const bool allowed=!pickerOpen_ && !interactiveCrop_.Active() && !cropOriginal_;
    CancelNetworkOpenButton().Visibility(waiting ? Visibility::Visible : Visibility::Collapsed);
    CancelNetworkOpenButton().IsEnabled(allowed);
    RetryNetworkOpenButton().Visibility(networkOpenFailed_ ? Visibility::Visible : Visibility::Collapsed);
    RetryNetworkOpenButton().IsEnabled(allowed && state.session==iima::SessionState::Ready && state.compositionAvailable);
    EditNetworkOpenButton().Visibility(networkOpenFailed_ ? Visibility::Visible : Visibility::Collapsed);
    EditNetworkOpenButton().IsEnabled(allowed && state.session==iima::SessionState::Ready && state.compositionAvailable);
    DismissNetworkOpenButton().Visibility(networkOpenFailed_ ? Visibility::Visible : Visibility::Collapsed);
}
void MainWindow::CancelNetworkOpen() {
    if (closing_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ || networkOpenUrl_.empty() ||
        inputGeneration_!=networkOpenInputRevision_ || networkOpenFailed_) return;
    if (networkOpenPending_) {
        // Not submitted: remove only this request. Do not stop previous playback.
        if (pendingFiles_.size()!=1 || pendingFiles_.front()!=networkOpenUrl_) return;
        pendingFiles_.clear(); ++inputGeneration_;
        networkOpenUrl_.clear(); networkOpenPending_=false;
    } else {
        // Core compares the expected intent atomically on this UI dispatcher.
        if (!session_->CancelNetworkOpen(networkOpenIntent_)) {
            ShowError(L"打开请求或媒体身份已变化，未停止当前媒体。请使用播放器停止操作。");
            return;
        }
        networkOpenUrl_.clear();
    }
    UpdateNetworkOpen(session_->Snapshot());
}
void MainWindow::RetryNetworkOpen() {
    if (closing_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ || !networkOpenFailed_ ||
        networkOpenUrl_.empty() || inputGeneration_!=networkOpenInputRevision_ ||
        session_->Snapshot().session!=iima::SessionState::Ready || !session_->Snapshot().compositionAvailable) return;
    if (session_->Snapshot().mediaOpenIntentRevision!=(networkOpenIntent_ ? networkOpenIntent_ : networkOpenBaselineIntent_)) return;
    auto url=networkOpenUrl_;
    ReceiveResolvedInput({std::move(url)}); // Never reroute a retry into yet another window.
}
}

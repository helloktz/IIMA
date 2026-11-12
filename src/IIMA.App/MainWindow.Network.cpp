// IINA OpenURLWindowController URL entry adapted to a WinUI ContentDialog.
// Copyright © 2018 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Network/MediaUrl.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
// Share exact validation between first opens, retry edits and final submission.
// URLs may contain private parameters; never include the input in diagnostics.
std::optional<std::string> ValidatedNetworkUrl(hstring const& text) {
    if (text.empty() || text.size()>32767) return {};
    try {
        auto prepared=iima::PrepareMediaUrl(to_string(text));
        if (!prepared) return {};
        Windows::Foundation::Uri uri(to_hstring(prepared->value));
        if (uri.Host().empty()) return {};
        return to_string(uri.AbsoluteUri());
    } catch (...) { return {}; }
}
}
namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::OpenNetworkUrl(bool invertWindowPolicy, bool editFailedAttempt) {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    const auto inputRevision=inputGeneration_;
    const auto intent=session_->Snapshot().mediaOpenIntentRevision;
    const auto mediaGeneration=session_->Snapshot().mediaGeneration;
    const auto mediaState=session_->Snapshot().media;
    const auto playlistRevision=session_->Snapshot().playlistRevision;
    const auto playlistIntent=session_->PlaylistEditIntentRevision();
    const auto attemptIntent=session_->Snapshot().networkOpenAttempt.intent;
    const auto attemptPhase=session_->Snapshot().networkOpenAttempt.phase;
    const auto failedIntent=networkOpenIntent_;
    const auto baselineIntent=networkOpenBaselineIntent_;
    const auto originalUrl=editFailedAttempt ? networkOpenUrl_ : std::string{};
    auto editCurrent=[this, inputRevision, intent, mediaGeneration, mediaState, attemptIntent,
                      attemptPhase, failedIntent, baselineIntent, originalUrl, playlistRevision, playlistIntent] {
        auto const& state=session_->Snapshot();
        return !closing_ && userDataLoaded_ && !interactiveCrop_.Active() && !cropOriginal_ &&
            !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
            networkOpenFailed_ && !networkOpenPending_ && !originalUrl.empty() && networkOpenUrl_==originalUrl &&
            inputGeneration_==inputRevision && networkOpenInputRevision_==inputRevision &&
            networkOpenIntent_==failedIntent && networkOpenBaselineIntent_==baselineIntent &&
            state.session==iima::SessionState::Ready && state.compositionAvailable &&
            state.mediaOpenIntentRevision==intent && intent==(failedIntent ? failedIntent : baselineIntent) &&
            state.mediaGeneration==mediaGeneration && state.media==mediaState &&
            state.playlistRevision==playlistRevision && session_->PlaylistEditIntentRevision()==playlistIntent &&
            !session_->PlaylistEditBusy() &&
            state.networkOpenAttempt.intent==attemptIntent && state.networkOpenAttempt.phase==attemptPhase;
    };
    if (editFailedAttempt && !editCurrent()) co_return;
    pickerOpen_ = true;
    UpdateNetworkOpen(session_->Snapshot());
    try {
        Controls::ContentDialog dialog;
        openUrlDialog_ = dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.Title(box_value(editFailedAttempt ? L"编辑失败的网络地址" : L"打开网络媒体"));
        dialog.PrimaryButtonText(editFailedAttempt ? L"在此窗口重试" : L"打开"); dialog.CloseButtonText(L"取消");
        dialog.DefaultButton(Controls::ContentDialogButton::Primary);
        Controls::StackPanel content;
        content.Spacing(8);
        Controls::TextBox input;
        if (editFailedAttempt) input.Text(to_hstring(originalUrl));
        input.IsSpellCheckEnabled(false);
        input.IsTextPredictionEnabled(false);
        input.PlaceholderText(L"https://example.com/video.mp4");
        Controls::TextBlock message;
        message.Text(L"支持 HTTP(S)、RTSP(S)、RTMP(S)、FTP、SFTP。网络地址不会写入播放历史。");
        message.TextWrapping(TextWrapping::Wrap);
        Controls::TextBlock destination;
        const bool newWindow = preferences_.Boolean("alwaysOpenInNewWindow") != invertWindowPolicy;
        destination.Text(editFailedAttempt ? L"只在此失败窗口重试，不创建或切换到其他窗口。" : newWindow ? L"打开到空白窗口或新窗口，不替换正在播放的窗口。" : L"在当前窗口打开，替换当前队列。");
        destination.TextWrapping(TextWrapping::Wrap);
        content.Children().Append(input); content.Children().Append(message);
        content.Children().Append(destination);
        dialog.Content(content);
        dialog.PrimaryButtonClick([input, message, editFailedAttempt, editCurrent](auto const&, Controls::ContentDialogButtonClickEventArgs const& args) {
            if (editFailedAttempt && !editCurrent()) {
                args.Cancel(true);
                message.Text(L"打开请求或媒体已变化，此草稿不会提交。请取消后重新操作。");
                return;
            }
            if (!ValidatedNetworkUrl(input.Text())) {
                args.Cancel(true); message.Text(L"请输入有效的网络媒体地址（最多32767字符）。");
            }
        });
        auto result = co_await dialog.ShowAsync();
        if (!closing_ && result == Controls::ContentDialogResult::Primary &&
            (!editFailedAttempt || editCurrent())) {
            if (auto url=ValidatedNetworkUrl(input.Text())) {
                inputStop_.request_stop(); ++inputGeneration_; inputResolving_ = false;
                pendingFiles_.clear();
                if (activationTimer_) activationTimer_.Stop();
                activationInputs_.clear(); activationDelayElapsed_ = false;
                UpdateNetworkOpen(session_->Snapshot());
                // The modal picker has finished. Release its reuse guard before
                // App selects a pristine window; otherwise a blank origin would
                // be incorrectly skipped. URL menus use activeOrNew, not the
                // file openURLs duplicate suppression path in IINA.
                pickerOpen_ = false;
                std::vector<std::string> paths{std::move(*url)};
                if (editFailedAttempt) ReceiveResolvedInput(std::move(paths));
                else if (openInputRouter_) openInputRouter_(std::move(paths), newWindow, true, true, false);
                else ReceiveResolvedInput(std::move(paths));
            }
        }
    } catch (...) { if (!closing_) ShowError(L"打开网络媒体失败。"); }
    openUrlDialog_ = nullptr;
    pickerOpen_ = false;
    if (!closing_) { UpdateNetworkOpen(session_->Snapshot()); OpenPendingFiles(); FlushActivation(); }
}
}

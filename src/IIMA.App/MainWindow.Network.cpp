// IINA OpenURLWindowController URL entry adapted to a WinUI ContentDialog.
// Copyright © 2018 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Network/MediaUrl.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::OpenNetworkUrl() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    pickerOpen_ = true;
    try {
        Controls::ContentDialog dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.Title(box_value(L"打开网络媒体"));
        dialog.PrimaryButtonText(L"打开"); dialog.CloseButtonText(L"取消");
        dialog.DefaultButton(Controls::ContentDialogButton::Primary);
        Controls::StackPanel content;
        content.Spacing(8);
        Controls::TextBox input;
        input.PlaceholderText(L"https://example.com/video.mp4");
        Controls::TextBlock message;
        message.Text(L"支持 HTTP(S)、RTSP(S)、RTMP(S)、FTP、SFTP。网络地址不会写入播放历史。");
        message.TextWrapping(TextWrapping::Wrap);
        content.Children().Append(input); content.Children().Append(message);
        dialog.Content(content);
        dialog.PrimaryButtonClick([input, message](auto const&, Controls::ContentDialogButtonClickEventArgs const& args) {
            auto prepared = iima::PrepareMediaUrl(to_string(input.Text()));
            if (!prepared) { args.Cancel(true); message.Text(L"请输入有效的网络媒体地址。"); return; }
            try {
                Windows::Foundation::Uri uri(to_hstring(prepared->value));
                if (uri.Host().empty()) throw hresult_invalid_argument();
            } catch (...) { args.Cancel(true); message.Text(L"无法解析此网络地址。"); }
        });
        auto result = co_await dialog.ShowAsync();
        if (!closing_ && result == Controls::ContentDialogResult::Primary) {
            auto prepared = iima::PrepareMediaUrl(to_string(input.Text()));
            if (prepared) {
                Windows::Foundation::Uri uri(to_hstring(prepared->value));
                inputStop_.request_stop(); ++inputGeneration_; inputResolving_ = false;
                pendingFiles_.clear();
                inputAssigned_ = true;
                session_->Open(to_string(uri.AbsoluteUri()));
            }
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    catch (...) { if (!closing_) ShowError(L"打开网络媒体失败。"); }
    pickerOpen_ = false;
    if (!closing_) OpenPendingFiles();
}
}

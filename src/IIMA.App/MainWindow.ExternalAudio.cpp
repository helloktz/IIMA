// IINA SidebarAudioPane.loadExternalAudioAction and PlayerCore.loadExternalAudioFile.
// Copyright (c) 2016 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
using namespace winrt;
namespace winrt::IIMA::implementation {
fire_and_forget MainWindow::LoadExternalAudio() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || session_->Snapshot().session != iima::SessionState::Ready ||
        session_->Snapshot().media != iima::MediaState::Loaded) co_return;
    pickerOpen_ = true;
    const auto epoch = navigationEpoch_;
    const auto entry = session_->Snapshot().entryId;
    const auto path = session_->Snapshot().path;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.CommitButtonText(L"载入外置音频");
        // mpv can demux audio from video containers too. Do not pretend that an
        // extension filter proves decodability or excludes all valid containers.
        picker.FileTypeFilter().Append(L"*");
        auto file = co_await picker.PickSingleFileAsync();
        if (file && !closing_) {
            auto const& state = session_->Snapshot();
            if (epoch != navigationEpoch_ || state.entryId != entry ||
                state.path != path || state.media != iima::MediaState::Loaded) {
                ShowError(L"媒体已变化，未载入外置音频。请重新选择。");
            } else if (file.Path().empty()) {
                ShowError(L"所选文件没有可供播放器读取的路径。");
            } else if (!session_->LoadExternalAudio(to_string(file.Path()))) {
                ShowError(L"播放器正在切换或关闭，未提交外置音频。");
            }
            // No synthetic track or success banner: actual track-list/aid
            // events and asynchronous command errors remain authoritative.
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    catch (...) { if (!closing_) ShowError(L"无法选择外置音频文件。"); }
    pickerOpen_ = false;
    if (!closing_) { OpenPendingFiles(); FlushActivation(); }
}
}

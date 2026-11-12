// IINA MainMenuActions.menuSavePlaylist mapped to Windows save picker.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Playlist/M3uWriter.h"
#include "Platform/AtomicTextFile.h"
#include <ShObjIdl_core.h>
#include <exception>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.Pickers.h>
using namespace winrt;
namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::SavePlaylist() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    auto text = iima::SerializeM3u8(session_->Snapshot().playlist);
    if (!text) { ShowError(to_hstring(text.error)); co_return; }
    pickerOpen_ = true;
    winrt::apartment_context ui;
    try {
        Windows::Storage::Pickers::FileSavePicker picker;
        HWND window{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&window));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(window));
        picker.SuggestedFileName(L"IIMA-playlist");
        picker.FileTypeChoices().Insert(L"UTF-8 播放列表", single_threaded_vector<hstring>({L".m3u8"}));
        auto file = co_await picker.PickSaveFileAsync();
        if (file && !closing_) {
            auto path = std::filesystem::path(file.Path().c_str());
            // Save the queue snapshot captured when the action began, not a
            // different queue that changed while the picker was displayed.
            co_await winrt::resume_background();
            std::exception_ptr failure;
            try { iima::WriteUtf8Atomically(path, text.utf8); }
            catch (...) { failure = std::current_exception(); }
            co_await ui;
            if (failure) std::rethrow_exception(failure);
            if (!closing_) StatusText().Text(L"已保存播放列表。");
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    catch (...) { if (!closing_) ShowError(L"保存播放列表失败。"); }
    pickerOpen_ = false;
    if (!closing_) OpenPendingFiles();
}
}

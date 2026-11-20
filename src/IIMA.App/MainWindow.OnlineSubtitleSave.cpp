// IINA MainMenuActions.saveDownloadedSub, Windows save-picker adaptation.
// Copyright (c) lhc and contributors. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <exception>
using namespace winrt;
namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::SaveOnlineSubtitle() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    const auto index = DownloadedSubtitleBox().SelectedIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= downloadedSubtitles_.size()) {
        OnlineSubtitleStatusText().Text(iima::loc::Text(L"PleaseSelectADownloadedSubtitleFirst")); co_return;
    }
    // Copy identity and shared ownership before showing a picker. Changing media
    // or closing the player cannot change which file this action saves.
    const auto selected = downloadedSubtitles_[static_cast<std::size_t>(index)];
    pickerOpen_ = true;
    apartment_context ui;
    try {
        Windows::Storage::Pickers::FileSavePicker picker;
        HWND window{};
        check_hresult(GetNativeWindowHandle(&window));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(window));
        picker.SuggestedFileName(hstring(selected.path.stem().wstring()));
        picker.FileTypeChoices().Insert(iima::loc::Text(L"SubtitleFiles"), single_threaded_vector<hstring>(
            {hstring(selected.path.extension().wstring())}));
        auto file = co_await picker.PickSaveFileAsync();
        if (file && !closing_) {
            const auto destination = std::filesystem::path(file.Path().c_str());
            co_await resume_background();
            std::exception_ptr failure;
            try { selected.owner->Publish(selected.path, destination); }
            catch (...) { failure = std::current_exception(); }
            co_await ui;
            if (failure) std::rethrow_exception(failure);
            if (!closing_) OnlineSubtitleStatusText().Text(iima::loc::Text(L"SubtitlesHaveBeenSaved"));
        }
    } catch (hresult_error const&) {
        if (!closing_) OnlineSubtitleStatusText().Text(iima::loc::Text(L"CouldNotSaveSubtitlesCheckTheDestination"));
    } catch (...) {
        if (!closing_) OnlineSubtitleStatusText().Text(iima::loc::Text(L"SubtitlesFailedToBeKeptAndThe"));
    }
    pickerOpen_ = false;
    if (!closing_) OpenPendingFiles();
}
}

// IINA ThumbnailCache.clearThumbnailCache adapted to explicit native confirmation.
// Copyright (c) 2017 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/ThumbnailDiskCache.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
winrt::fire_and_forget MainWindow::ClearThumbnailCache() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || clearingThumbnailCache_) co_return;
    pickerOpen_ = true;
    bool confirmed{};
    try {
        Controls::ContentDialog dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.Title(box_value(iima::loc::Text(L"EmptyThumbnailCache")));
        dialog.Content(box_value(iima::loc::Text(L"DeletesOnlyTheDiskThumbnailCacheOf")));
        dialog.PrimaryButtonText(iima::loc::Text(L"ClearCache")); dialog.CloseButtonText(iima::loc::Text(L"Cancel"));
        dialog.DefaultButton(Controls::ContentDialogButton::Close);
        confirmed = (co_await dialog.ShowAsync()) == Controls::ContentDialogResult::Primary;
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    pickerOpen_ = false;
    if (!confirmed || closing_) co_return;
    clearingThumbnailCache_ = true;
    ClearThumbnailCacheButton().IsEnabled(false);
    CancelThumbnailPreview(); thumbnailMemory_.clear();
    ThumbnailCacheStatusText().Text(iima::loc::Text(L"ClearingCache"));
    auto apartment = apartment_context();
    co_await resume_background();
    std::wstring status;
    try {
        auto result = iima::ClearThumbnailDiskCache();
        status = iima::loc::Text(L"Deleted") + std::to_wstring(result.deletedFiles) + iima::loc::Text(L"ACacheFile");
        if (result.failedFiles) status += iima::loc::Text(L"ThereSMore") + std::to_wstring(result.failedFiles) + iima::loc::Text(L"FilesCouldNotBeDeleted");
        if (!result.error.empty()) status += L" " + result.error;
    } catch (hresult_error const& error) { status = iima::loc::Text(L"ClearCacheFailed") + std::wstring(error.message()); }
      catch (...) { status = iima::loc::Text(L"ClearCacheFailed2"); }
    try { co_await apartment; } catch (...) { co_return; }
    clearingThumbnailCache_ = false;
    if (!closing_) {
        ClearThumbnailCacheButton().IsEnabled(true);
        ThumbnailCacheStatusText().Text(status);
    }
}
}

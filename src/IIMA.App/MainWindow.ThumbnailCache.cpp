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
        dialog.Title(box_value(L"清空缩略图缓存？"));
        dialog.Content(box_value(L"只删除 IIMA 的磁盘缩略图缓存，不删除视频文件。之后悬停会重新生成预览。"));
        dialog.PrimaryButtonText(L"清空缓存"); dialog.CloseButtonText(L"取消");
        dialog.DefaultButton(Controls::ContentDialogButton::Close);
        confirmed = (co_await dialog.ShowAsync()) == Controls::ContentDialogResult::Primary;
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    pickerOpen_ = false;
    if (!confirmed || closing_) co_return;
    clearingThumbnailCache_ = true;
    ClearThumbnailCacheButton().IsEnabled(false);
    CancelThumbnailPreview(); thumbnailMemory_.clear();
    ThumbnailCacheStatusText().Text(L"正在清空缓存…");
    auto apartment = apartment_context();
    co_await resume_background();
    std::wstring status;
    try {
        auto result = iima::ClearThumbnailDiskCache();
        status = L"已删除 " + std::to_wstring(result.deletedFiles) + L" 个缓存文件。";
        if (result.failedFiles) status += L"另有 " + std::to_wstring(result.failedFiles) + L" 个文件未能删除。";
        if (!result.error.empty()) status += L" " + result.error;
    } catch (hresult_error const& error) { status = L"清空缓存失败：" + std::wstring(error.message()); }
      catch (...) { status = L"清空缓存失败。"; }
    try { co_await apartment; } catch (...) { co_return; }
    clearingThumbnailCache_ = false;
    if (!closing_) {
        ClearThumbnailCacheButton().IsEnabled(true);
        ThumbnailCacheStatusText().Text(status);
    }
}
}

// IINA PlayerCore.screenshotCallback / ScreenshotOSDView.setImage projection.
// Copyright © 2016-2020 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <robuffer.h>
#include <winrt/Windows.Storage.Streams.h>
#include <chrono>
#include <cstring>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::HideScreenshotPreview() {
    if (screenshotPreviewTimer_) screenshotPreviewTimer_.Stop();
    ScreenshotPreviewBorder().Visibility(Visibility::Collapsed);
    ScreenshotPreviewImage().Source(nullptr);
}
void MainWindow::ShowScreenshotPreview(iima::ScreenshotPixels const& preview) {
    if (closing_) return;
    if (!preview.width || !preview.height || preview.width > 300 || preview.height > 200 ||
        preview.stride != preview.width * 4 || preview.bgra.size() != preview.stride * preview.height)
        throw hresult_invalid_argument(L"Invalid screenshot preview");
    Media::Imaging::WriteableBitmap bitmap(static_cast<int>(preview.width), static_cast<int>(preview.height));
    auto buffer = bitmap.PixelBuffer();
    if (buffer.Capacity() < preview.bgra.size()) throw hresult_error(E_FAIL, L"Screenshot preview buffer is too small");
    auto access = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
    byte* data{};
    check_hresult(access->Buffer(&data));
    if (!data) throw hresult_error(E_POINTER);
    std::memcpy(data, preview.bgra.data(), preview.bgra.size());
    bitmap.Invalidate();
    if (!screenshotPreviewTimer_) {
        screenshotPreviewTimer_ = DispatcherQueue().CreateTimer();
        screenshotPreviewTimer_.IsRepeating(false);
        screenshotPreviewTimer_.Interval(std::chrono::seconds(5));
        auto weak = get_weak();
        screenshotPreviewTimer_.Tick([weak](auto const&, auto const&) {
            if (auto self = weak.get()) self->HideScreenshotPreview();
        });
    }
    screenshotPreviewTimer_.Stop();
    ScreenshotPreviewImage().Width(preview.width);
    ScreenshotPreviewImage().Height(preview.height);
    ScreenshotPreviewImage().Source(bitmap);
    ScreenshotPreviewBorder().Visibility(Visibility::Visible);
    screenshotPreviewTimer_.Start();
}
}

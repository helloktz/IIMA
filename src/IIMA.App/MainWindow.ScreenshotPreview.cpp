// IINA PlayerCore.screenshotCallback / ScreenshotOSDView.setImage projection.
// Copyright © 2016-2020 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/ShellFileActions.h"
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <robuffer.h>
#include <winrt/Windows.Storage.Streams.h>
#include <chrono>
#include <cstring>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::HideScreenshotPreview() {
    ++screenshotPreviewGeneration_;
    screenshotPreviewPath_.reset();
    screenshotPreviewHovered_ = screenshotPreviewFocused_ = false;
    if (screenshotPreviewTimer_) screenshotPreviewTimer_.Stop();
    ScreenshotPreviewBorder().Visibility(Visibility::Collapsed);
    ScreenshotPreviewImage().Source(nullptr);
}
void MainWindow::ShowScreenshotPreview(iima::ScreenshotPixels const& preview,
    std::optional<std::filesystem::path> destination) {
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
            if (auto self = weak.get(); self && !self->screenshotPreviewHovered_ && !self->screenshotPreviewFocused_) self->HideScreenshotPreview();
        });
    }
    screenshotPreviewTimer_.Stop();
    ++screenshotPreviewGeneration_;
    screenshotPreviewPath_ = std::move(destination);
    ScreenshotPreviewActions().Visibility(screenshotPreviewPath_ ? Visibility::Visible : Visibility::Collapsed);
    OpenScreenshotPreviewButton().IsEnabled(true);
    RevealScreenshotPreviewButton().IsEnabled(true);
    ScreenshotPreviewImage().Width(preview.width);
    ScreenshotPreviewImage().Height(preview.height);
    ScreenshotPreviewImage().Source(bitmap);
    ScreenshotPreviewBorder().Visibility(Visibility::Visible);
    screenshotPreviewTimer_.Start();
}
// Only explicit clicks can leave the player. Clipboard-only captures never
// expose their short-lived staging file as a saved screenshot.
void MainWindow::WireScreenshotPreview() {
    auto weak = get_weak();
    OpenScreenshotPreviewButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenScreenshotPreviewFile(false);
    });
    RevealScreenshotPreviewButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OpenScreenshotPreviewFile(true);
    });
    DismissScreenshotPreviewButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->HideScreenshotPreview();
    });
    // Pause expiry during interaction, then grant a fresh five-second interval.
    ScreenshotPreviewBorder().GotFocus([weak](auto const&, auto const&) {
        if (auto self = weak.get()) { self->screenshotPreviewFocused_ = true; if(self->screenshotPreviewTimer_) self->screenshotPreviewTimer_.Stop(); }
    });
    ScreenshotPreviewBorder().LostFocus([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->screenshotPreviewFocused_ = false;
        if (auto self = weak.get(); self && !self->closing_ && self->screenshotPreviewTimer_ &&
            self->ScreenshotPreviewBorder().Visibility() == Visibility::Visible)
            self->screenshotPreviewTimer_.Start();
    });
    ScreenshotPreviewBorder().PointerEntered([weak](auto const&, auto const&) {
        if (auto self = weak.get()) { self->screenshotPreviewHovered_ = true; if(self->screenshotPreviewTimer_) self->screenshotPreviewTimer_.Stop(); }
    });
    ScreenshotPreviewBorder().PointerExited([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->screenshotPreviewHovered_ = false;
        if (auto self = weak.get(); self && !self->closing_ && self->screenshotPreviewTimer_ &&
            self->ScreenshotPreviewBorder().Visibility() == Visibility::Visible)
            self->screenshotPreviewTimer_.Start();
    });
}
winrt::fire_and_forget MainWindow::OpenScreenshotPreviewFile(bool reveal) {
    auto lifetime = get_strong();
    if (closing_ || !screenshotPreviewPath_ || !OpenScreenshotPreviewButton().IsEnabled()) co_return;
    const auto path = *screenshotPreviewPath_;
    const auto generation = screenshotPreviewGeneration_;
    OpenScreenshotPreviewButton().IsEnabled(false);
    RevealScreenshotPreviewButton().IsEnabled(false);
    if (screenshotPreviewTimer_) screenshotPreviewTimer_.Stop();
    try {
        // Resolve as a file, not URI or command text. The user can move/delete a
        // saved file after capture; that produces an error, not another target.
        auto file = co_await Windows::Storage::StorageFile::GetFileFromPathAsync(path.native());
        if (closing_ || generation != screenshotPreviewGeneration_) co_return;
        if (reveal) iima::RevealFile(path);
        else if (!(co_await Windows::System::Launcher::LaunchFileAsync(file)))
            throw hresult_error(E_FAIL, L"Windows未能打开截图，请检查默认图片应用。");
        if (!closing_ && generation == screenshotPreviewGeneration_) HideScreenshotPreview();
    } catch (hresult_error const& error) {
        if (!closing_ && generation == screenshotPreviewGeneration_) ShowError(error.message());
    } catch (std::exception const& error) {
        if (!closing_ && generation == screenshotPreviewGeneration_) ShowError(to_hstring(error.what()));
    }
    if (!closing_ && generation == screenshotPreviewGeneration_) {
        OpenScreenshotPreviewButton().IsEnabled(true);
        RevealScreenshotPreviewButton().IsEnabled(true);
        if (screenshotPreviewTimer_) screenshotPreviewTimer_.Start();
    }
}
}

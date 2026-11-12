// IINA MainWindowController.setWindowScale and PlayerCore.videoSizeForDisplay.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Geometry/WindowGeometry.h"
#include <microsoft.ui.xaml.window.h>
#include <charconv>
#include <cmath>
using namespace winrt;
namespace {
std::optional<double> VideoSizeNumber(iima::MediaProperties const& properties, char const* key) {
    auto found = properties.find(key);
    if (found == properties.end()) return {};
    double result{};
    auto const& value = found->second;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(result)) return {};
    return result;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireVideoSize() {
    auto weak = get_weak();
    auto wire = [weak](Microsoft::UI::Xaml::Controls::Button const& button, std::optional<double> scale) {
        button.Click([weak, scale](auto const&, auto const&) {
            if (auto self = weak.get()) self->ResizeToVideo(scale);
        });
    };
    wire(HalfVideoSizeButton(), 0.5); wire(OriginalVideoSizeButton(), 1.0);
    wire(OneHalfVideoSizeButton(), 1.5); wire(DoubleVideoSizeButton(), 2.0);
    wire(FitVideoSizeButton(), {});
}
void MainWindow::ResizeToVideo(std::optional<double> scale) {
    if (closing_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ || musicMode_.IsMusic()) return;
    try {
        auto window = AppWindow();
        auto presenter = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        if (!presenter || presenter.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored) {
            ShowError(L"请先返回普通窗口模式，再按视频尺寸缩放。"); return;
        }
        auto const& state = session_->Snapshot();
        if (state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded || !state.voConfigured) {
            ShowError(L"当前没有已配置的播放画面，无法按视频尺寸缩放。"); return;
        }
        auto const& output = state.videoOutputParameters;
        auto width = VideoSizeNumber(output, "dw"), height = VideoSizeNumber(output, "dh");
        auto rotation = VideoSizeNumber(output, "rotate");
        if (!width || !height || !rotation || *width <= 0 || *height <= 0 ||
            *width > 65536 || *height > 65536 || *rotation < 0 || *rotation >= 360 ||
            std::fmod(*rotation, 90.0) != 0) {
            ShowError(L"视频显示尺寸或直角旋转尚未确认，未修改窗口。"); return;
        }
        // dw/dh include filters and pixel-aspect correction but the VO handles
        // remaining right-angle rotation (mpv video/out/aspect.c).
        if (std::fmod(std::abs(*rotation), 180.0) == 90.0) std::swap(*width, *height);
        // Built-in source cropping is separate from filtered display dimensions;
        // refuse it until that transform is incorporated, rather than guessing.
        if (output.contains("crop-w") || output.contains("crop-h")) {
            if (VideoSizeNumber(output, "crop-w") != VideoSizeNumber(output, "w") ||
                VideoSizeNumber(output, "crop-h") != VideoSizeNumber(output, "h") ||
                VideoSizeNumber(output, "crop-x") != std::optional<double>{0} ||
                VideoSizeNumber(output, "crop-y") != std::optional<double>{0}) {
                ShowError(L"内建非全幅裁剪的显示尺寸尚不支持，请先关闭该裁剪。"); return;
            }
        }
        auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
            Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
        if (!display) { ShowError(L"无法取得当前屏幕工作区。"); return; }
        HWND hwnd{}; check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        const auto dpi = GetDpiForWindow(hwnd) / 96.0;
        const auto panelWidth = VideoPanel().ActualWidth() * VideoPanel().CompositionScaleX();
        const auto panelHeight = VideoPanel().ActualHeight() * VideoPanel().CompositionScaleY();
        const auto size = window.Size(); const auto position = window.Position(); const auto work = display.WorkArea();
        const auto extraWidth = size.Width - panelWidth, extraHeight = size.Height - panelHeight;
        if (panelWidth <= 0 || panelHeight <= 0 || extraWidth < 0 || extraHeight < 0) {
            ShowError(L"窗口布局仍在变化，请稍后重试视频尺寸缩放。"); return;
        }
        auto bounds = iima::ScaledVideoWindow({*width, *height},
            {double(position.X), double(position.Y), double(size.Width), double(size.Height)},
            {double(work.X), double(work.Y), double(work.Width), double(work.Height)},
            {extraWidth, extraHeight}, {640 * dpi, 360 * dpi}, scale,
            preferences_.Boolean("usePhysicalResolution"), dpi);
        if (!bounds) { ShowError(L"当前布局无法容纳所请求的视频尺寸。"); return; }
        // Round dimensions down so rounding cannot spill over the work area.
        window.MoveAndResize({static_cast<int>(std::ceil(bounds->x)), static_cast<int>(std::ceil(bounds->y)),
            static_cast<int>(std::floor(bounds->width)), static_cast<int>(std::floor(bounds->height))});
        CapturePlacement();
    } catch (hresult_error const& error) { ShowError(error.message()); }
}
}

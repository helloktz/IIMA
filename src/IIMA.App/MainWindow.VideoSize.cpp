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
std::optional<iima::GeometrySize> MainWindow::VideoDisplaySize(iima::MediaProperties const& output) {
    auto width = VideoSizeNumber(output, "dw"), height = VideoSizeNumber(output, "dh");
    auto rotation = VideoSizeNumber(output, "rotate");
    if (!width || !height || !rotation || *width <= 0 || *height <= 0 ||
        *width > 65536 || *height > 65536 || *rotation < 0 || *rotation >= 360 ||
        std::fmod(*rotation, 90.0) != 0) {
        return {};
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
            return {};
        }
    }
    return iima::GeometrySize{*width, *height};
}
void MainWindow::WireVideoSize() {
    auto weak = get_weak();
    VideoPanel().SizeChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->UpdateAutomaticVideoSize(true);
    });
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
    CancelAutomaticVideoSize();
    TryResizeToVideo(scale, false);
}
MainWindow::VideoResizeResult MainWindow::TryResizeToVideo(std::optional<double> scale, bool automatic) {
    auto report = [this, automatic](hstring const& message) { if (!automatic) ShowError(message); };
    if (closing_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ || musicMode_.IsMusic()) return VideoResizeResult::Unsupported;
    try {
        auto window = AppWindow();
        auto presenter = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        if (!presenter || presenter.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored) {
            report(L"请先返回普通窗口模式，再按视频尺寸缩放。"); return VideoResizeResult::Unsupported;
        }
        auto const& state = session_->Snapshot();
        if (state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded || !state.voConfigured) {
            report(L"当前没有已配置的播放画面，无法按视频尺寸缩放。"); return VideoResizeResult::Retry;
        }
        const auto video = VideoDisplaySize(state.videoOutputParameters);
        if (!video) {
            report(L"视频尺寸、旋转或内建裁剪尚不支持，未修改窗口。");
            return VideoResizeResult::Retry;
        }
        auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
            Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
        if (!display) { report(L"无法取得当前屏幕工作区。"); return VideoResizeResult::Retry; }
        HWND hwnd{}; check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        const auto dpi = GetDpiForWindow(hwnd) / 96.0;
        const auto panelWidth = VideoPanel().ActualWidth() * VideoPanel().CompositionScaleX();
        const auto panelHeight = VideoPanel().ActualHeight() * VideoPanel().CompositionScaleY();
        const auto size = window.Size(); const auto position = window.Position(); const auto work = display.WorkArea();
        const auto extraWidth = size.Width - panelWidth, extraHeight = size.Height - panelHeight;
        if (panelWidth <= 0 || panelHeight <= 0 || extraWidth < 0 || extraHeight < 0) {
            report(L"窗口布局仍在变化，请稍后重试视频尺寸缩放。"); return VideoResizeResult::Retry;
        }
        auto bounds = iima::ScaledVideoWindow(*video,
            {double(position.X), double(position.Y), double(size.Width), double(size.Height)},
            {double(work.X), double(work.Y), double(work.Width), double(work.Height)},
            {extraWidth, extraHeight}, {640 * dpi, 360 * dpi}, scale,
            automatic ? automaticResizePhysical_ : preferences_.Boolean("usePhysicalResolution"), dpi);
        if (automatic && automaticResizeKeepArea_) {
            bounds = iima::AreaPreservingVideoWindow(*video,
                {double(position.X), double(position.Y), double(size.Width), double(size.Height)},
                {double(work.X), double(work.Y), double(work.Width), double(work.Height)},
                {extraWidth, extraHeight}, {640 * dpi, 360 * dpi});
        }
        if (automatic && automaticResizeInitial_ && bounds) {
            // The startup geometry was only a provisional frame before a video
            // aspect existed. Resolve the saved startup text, not later edits.
            if (!initialVideoGeometry_.empty()) {
                auto geometry = iima::WindowGeometry::Parse(initialVideoGeometry_);
                if (!geometry) return VideoResizeResult::Unsupported;
                bounds = iima::InitialVideoWindow(*geometry, *video, *bounds,
                    {double(work.X), double(work.Y), double(work.Width), double(work.Height)},
                    {extraWidth, extraHeight}, {640 * dpi, 360 * dpi});
            } else {
                bounds->x = work.X + (work.Width - bounds->width) / 2;
                bounds->y = work.Y + (work.Height - bounds->height) / 2;
            }
        }
        if (!bounds) { report(L"当前布局无法容纳所请求的视频尺寸。"); return VideoResizeResult::Retry; }
        if (automatic && automaticResizeStart_ && !automaticResizeInitial_ &&
            !automaticResizeKeepArea_ && automaticResizeOption_ == 0 && bounds) {
            bounds->x = work.X + (work.Width - bounds->width) / 2;
            bounds->y = work.Y + (work.Height - bounds->height) / 2;
        }
        // Round dimensions down so rounding cannot spill over the work area.
        window.MoveAndResize({static_cast<int>(std::ceil(bounds->x)), static_cast<int>(std::ceil(bounds->y)),
            static_cast<int>(std::floor(bounds->width)), static_cast<int>(std::floor(bounds->height))});
        CapturePlacement();
        return VideoResizeResult::Applied;
    } catch (hresult_error const& error) { report(error.message()); return VideoResizeResult::Unsupported; }
}
}

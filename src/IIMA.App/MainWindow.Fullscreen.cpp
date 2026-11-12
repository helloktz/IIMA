// IINA MainWindowController.FullScreenState.priorWindowedFrame and transitions.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <cmath>

namespace winrt::IIMA::implementation {
void MainWindow::ToggleFullscreen() {
    if (closing_ || fullscreenTransition_) return;
    if (fullscreen_) {
        if (ExitFullscreen() && preferences_.Boolean("pauseWhenLeavingFullScreen") &&
            session_->Snapshot().media == iima::MediaState::Loaded && !session_->Snapshot().paused)
            session_->Pause(true);
        return;
    }
    try {
        auto window = AppWindow();
        FullscreenRestoreState saved;
        saved.presenter = window.Presenter();
        if (auto normal = saved.presenter.try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
            if (normal.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Minimized) return;
            saved.maximized = normal.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Maximized;
            if (!saved.maximized) CapturePlacement();
            saved.geometry = windowPlacement_;
            saved.videoSizingEligible = !musicMode_.IsMusic();
            if (musicMode_.IsMusic()) {
                auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
                    Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
                HWND hwnd{}; winrt::check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
                if (display) {
                    auto work = display.WorkArea(); auto point = window.Position(); auto extent = window.Size();
                    saved.geometry = iima::WindowPlacementSnapshot{
                        {double(point.X), double(point.Y), double(extent.Width), double(extent.Height)},
                        {double(work.X), double(work.Y), double(work.Width), double(work.Height)}, double(GetDpiForWindow(hwnd))};
                } else saved.geometry.reset();
            }
            const auto size = window.Size();
            saved.chrome = {size.Width - VideoPanel().ActualWidth() * VideoPanel().CompositionScaleX(),
                size.Height - VideoPanel().ActualHeight() * VideoPanel().CompositionScaleY()};
            if (saved.videoSizingEligible && saved.geometry) {
                auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
                    Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
                HWND hwnd{}; winrt::check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
                if (display) {
                    auto work = display.WorkArea(); const auto dpi = GetDpiForWindow(hwnd);
                    auto bounds = iima::RestoreWindowPlacement(*saved.geometry,
                        {double(work.X), double(work.Y), double(work.Width), double(work.Height)}, dpi,
                        {640 * dpi / 96.0, 360 * dpi / 96.0});
                    if (bounds) saved.geometry = iima::WindowPlacementSnapshot{*bounds,
                        {double(work.X), double(work.Y), double(work.Width), double(work.Height)}, double(dpi)};
                    else saved.geometry.reset();
                } else saved.geometry.reset();
            }
            if (!std::isfinite(saved.chrome.width) || !std::isfinite(saved.chrome.height) ||
                saved.chrome.width < 0 || saved.chrome.height < 0) saved.geometry.reset();
        }
        // Preserve the actual presenter object, including maximized/overlay
        // state. A fullscreen rectangle must never become a normal restore size.
        windowScaleExpectedPanelWidth_.reset();
        fullscreenTransition_ = true;
        fullscreenRestore_ = std::move(saved);
        window.SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen);
        fullscreen_ = true;
        fullscreenTransition_ = false;
        FullscreenButton().Content(winrt::box_value(L"退出全屏"));
        ApplyWindowPreferences();
        if (preferences_.Boolean("playWhenEnteringFullScreen")) session_->Pause(false);
        UpdateAutomaticVideoSize();
    } catch (winrt::hresult_error const& error) {
        fullscreenTransition_ = false;
        fullscreen_ = AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen;
        if (!fullscreen_) fullscreenRestore_.reset();
        ShowError(error.message());
    }
}
bool MainWindow::ExitFullscreen(bool restoreGeometry) {
    if (!fullscreen_) return true;
    if (fullscreenTransition_) return false;
    try {
        auto window = AppWindow();
        // Choose the destination while still fullscreen. Reattaching the old
        // presenter can itself move back to the pre-fullscreen monitor.
        auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
            Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
        HWND hwnd{}; winrt::check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        const auto restoreDpi = GetDpiForWindow(hwnd);
        fullscreenTransition_ = true;
        restoringPlacement_ = true;
        if (fullscreenRestore_ && fullscreenRestore_->presenter) window.SetPresenter(fullscreenRestore_->presenter);
        else window.SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
        fullscreen_ = false;
        if (restoreGeometry && fullscreenRestore_ && fullscreenRestore_->geometry) {
            auto normal = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
            if (normal && display) {
                auto work = display.WorkArea(); const auto dpi = restoreDpi;
                auto bounds = iima::RestoreWindowPlacement(*fullscreenRestore_->geometry,
                    {double(work.X), double(work.Y), double(work.Width), double(work.Height)}, dpi,
                    {640 * dpi / 96.0, 360 * dpi / 96.0});
                if (bounds) {
                    normal.Restore();
                    placementProgrammaticBounds_ = Windows::Graphics::RectInt32{
                        static_cast<int>(std::ceil(bounds->x)), static_cast<int>(std::ceil(bounds->y)),
                        static_cast<int>(std::floor(bounds->width)), static_cast<int>(std::floor(bounds->height))};
                    if (fullscreenRestore_->videoSizingEligible)
                        windowScaleExpectedPanelWidth_ = std::floor(bounds->width) -
                            fullscreenRestore_->chrome.width * dpi / fullscreenRestore_->geometry->dpi;
                    window.MoveAndResize(*placementProgrammaticBounds_);
                    CapturePlacement();
                }
                if (fullscreenRestore_->maximized) normal.Maximize();
            }
        }
        fullscreenRestore_.reset();
        restoringPlacement_ = false;
        fullscreenTransition_ = false;
        FullscreenButton().Content(winrt::box_value(L"全屏"));
        ApplyWindowPreferences();
        QueueWindowScaleFeedback();
        return true;
    } catch (winrt::hresult_error const& error) {
        restoringPlacement_ = false;
        fullscreenTransition_ = false;
        fullscreen_ = AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen;
        if (!fullscreen_) fullscreenRestore_.reset();
        FullscreenButton().Content(winrt::box_value(fullscreen_ ? L"退出全屏" : L"全屏"));
        ShowError(error.message());
        return false;
    }
}
}

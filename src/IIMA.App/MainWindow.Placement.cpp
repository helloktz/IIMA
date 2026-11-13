// IINA MainWindowLastPosition/geometry adapted to Windows monitor work areas.
// Copyright (c) 2017 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <thread>
#include <cmath>
#include "../IIMA.Core/Geometry/WindowGeometry.h"

namespace winrt::IIMA::implementation {
void MainWindow::WireWindowPlacement()
{
    auto weak = get_weak();
    AppWindow().Changed([weak](auto const&, auto const& args) {
        if (auto self = weak.get(); self && !self->closing_ && !self->restoringPlacement_ &&
            !self->musicTransition_ && !self->compactTransition_ && !self->compactLayoutPending_ && !self->compactRestore_ &&
            self->AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped && !self->fullscreenTransition_ && !self->fullscreen_ && (args.DidPositionChange() || args.DidSizeChange())) {
            if (self->placementProgrammaticBounds_) {
                auto expected = *self->placementProgrammaticBounds_;
                auto current = self->AppWindow(); auto point = current.Position(); auto size = current.Size();
                if (point.X == expected.X && point.Y == expected.Y &&
                    size.Width == expected.Width && size.Height == expected.Height) return;
                self->placementProgrammaticBounds_.reset();
            }
            self->CancelAutomaticVideoSize();
            self->placementTouched_ = true;
            self->CapturePlacement();
        }
    });
}
void MainWindow::CapturePlacement()
{
    if (closing_ || compactTransition_ || compactLayoutPending_ || compactRestore_ || musicMode_.IsMusic() || musicTransition_) return;
    try {
        auto window = AppWindow();
        auto presenter = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        if (!presenter || presenter.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored) return;
        auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
            Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
        if (!display) return;
        HWND hwnd{}; check_hresult(GetNativeWindowHandle(&hwnd));
        auto p = window.Position(); auto size = window.Size(); auto work = display.WorkArea();
        auto capture = iima::CaptureWindowPlacement(
            {double(p.X),double(p.Y),double(size.Width),double(size.Height)},
            {double(work.X),double(work.Y),double(work.Width),double(work.Height)},
            GetDpiForWindow(hwnd), iima::WindowPlacementMode::Normal);
        if (capture) windowPlacement_ = *capture;
    } catch (...) { OutputDebugStringW(L"IIMA: could not capture window position.\n"); }
}
void MainWindow::RestorePlacement(std::optional<iima::WindowPlacementSnapshot> saved)
{
    if (closing_ || compactTransition_ || compactLayoutPending_ || compactRestore_ || placementTouched_ || !saved || musicMode_.IsMusic()) return;
    auto normal=AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
    if(!normal || normal.State()!=Microsoft::UI::Windowing::OverlappedPresenterState::Restored) return;
    try {
        auto window = AppWindow();
        auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
            Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
        if (!display) return;
        HWND hwnd{}; check_hresult(GetNativeWindowHandle(&hwnd));
        auto work = display.WorkArea();
        auto rect = iima::RestoreWindowPlacement(*saved,
            {double(work.X),double(work.Y),double(work.Width),double(work.Height)},
            GetDpiForWindow(hwnd), {640,360});
        if (!rect) return;
        restoringPlacement_ = true;
        placementProgrammaticBounds_ = Windows::Graphics::RectInt32{static_cast<int>(rect->x), static_cast<int>(rect->y),
            static_cast<int>(rect->width), static_cast<int>(rect->height)};
        window.MoveAndResize(*placementProgrammaticBounds_);
        restoringPlacement_ = false;
        CapturePlacement();
    } catch (hresult_error const& e) {
        restoringPlacement_ = false; placementProgrammaticBounds_.reset(); ShowError(L"窗口位置未恢复：" + e.message());
    }
}
void MainWindow::ApplyInitialWindowGeometry()
{
    // Called once after user data and saved placement are restored, not on each
    // preference edit. First-video sizing later resolves this same geometry against
    // the actual video aspect. Never override interaction during async load.
    initialVideoGeometry_ = preferences_.String("initialWindowSizePosition");
    if (placementTouched_) initialVideoSizePending_ = false;
    if (closing_ || compactTransition_ || compactLayoutPending_ || compactRestore_ || placementTouched_ || musicMode_.IsMusic()) return;
    auto const& text = initialVideoGeometry_;
    if (text.empty()) return;
    auto geometry = text.size() <= 128 ? iima::WindowGeometry::Parse(text) : std::nullopt;
    if (!geometry) { ShowError(L"已保存的初始窗口几何无效，保留当前窗口位置。"); return; }
    try {
        auto window = AppWindow();
        auto presenter = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        if (!presenter || presenter.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored) return;
        auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
            Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
        if (!display) return;
        HWND hwnd{}; check_hresult(GetNativeWindowHandle(&hwnd));
        const auto dpi = GetDpiForWindow(hwnd) / 96.0;
        if (!std::isfinite(dpi) || dpi < 0.5 || dpi > 8) return;
        const auto work = display.WorkArea(); const auto point = window.Position(); const auto size = window.Size();
        auto rect = geometry->ResolveVisible(
            {double(point.X), double(point.Y), double(size.Width), double(size.Height)},
            {double(work.X), double(work.Y), double(work.Width), double(work.Height)}, {640 * dpi, 360 * dpi});
        if (!rect) { ShowError(L"初始窗口几何无法适用于当前屏幕，保留当前窗口位置。"); return; }
        restoringPlacement_ = true;
        placementProgrammaticBounds_ = Windows::Graphics::RectInt32{static_cast<int>(std::ceil(rect->x)), static_cast<int>(std::ceil(rect->y)),
            static_cast<int>(std::floor(rect->width)), static_cast<int>(std::floor(rect->height))};
        window.MoveAndResize(*placementProgrammaticBounds_);
        restoringPlacement_ = false;
        CapturePlacement();
    } catch (hresult_error const& error) {
        restoringPlacement_ = false; placementProgrammaticBounds_.reset();
        ShowError(L"初始窗口几何未能应用：" + error.message());
    }
}
void MainWindow::SaveWindowPlacement()
{
    if (placementSavePending_ || !windowPlacement_ || !placementWritable_) return;
    placementSavePending_ = true;
    auto captured = *windowPlacement_;
    auto weak = get_weak(); auto dispatcher = DispatcherQueue();
    try {
        std::thread([weak, dispatcher, captured] {
            bool initialized{};
            std::string error;
            try {
                init_apartment(apartment_type::multi_threaded); initialized = true;
                iima::WindowPlacementStore::Save(captured);
            } catch (hresult_error const& e) { error = to_string(e.message()); }
              catch (std::exception const& e) { error = e.what(); }
            if (initialized) uninit_apartment();
            dispatcher.TryEnqueue([weak, error = std::move(error)] {
                if (auto self = weak.get()) {
                    self->placementSavePending_ = false;
                    // Position is nonessential. It must not trap the user in an
                    // otherwise successfully closed player or overwrite corrupt data.
                    if (!error.empty()) OutputDebugStringW(L"IIMA: window position was not saved.\n");
                    self->FinishClose();
                }
            });
        }).detach();
    } catch (...) { placementSavePending_ = false; }
}
}

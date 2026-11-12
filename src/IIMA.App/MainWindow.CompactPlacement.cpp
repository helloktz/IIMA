// IINA MainWindowController enterPIP/prepareForPIPClosure/doneExitingPIP.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Windows uses one AppWindow: retain the normal presenter/placement separately
// from fullscreen's temporary presenter, and restore on the current monitor.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <cmath>
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Windowing;
namespace winrt::IIMA::implementation {
bool MainWindow::EnterCompactOverlay() {
    if(closing_ || !userDataLoaded_ || compactTransition_ || fullscreenTransition_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_) return false;
    CancelABMarker();
    CloseMusicVolume();
    if(!ExitFullscreen()) return false;
    if(compactRestore_) return ExitCompactOverlay();
    auto window=AppWindow();
    if(window.Presenter().Kind()==AppWindowPresenterKind::CompactOverlay) return true;
    auto normal=window.Presenter().try_as<OverlappedPresenter>();
    if(!normal || normal.State()==OverlappedPresenterState::Minimized) return false;
    struct Guard { bool& value; ~Guard(){value=false;} } guard{compactTransition_};
    compactTransition_=true;
    CancelMouseClick(); CancelThumbnailPreview(); CancelControlBarDrag(); CancelScrubbing(); CancelAutomaticVideoSize();
    try {
        HWND hwnd{}; check_hresult(as<::IWindowNative>()->get_WindowHandle(&hwnd));
        MONITORINFO monitor{sizeof(MONITORINFO)};
        check_bool(GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor));
        const auto dpi=GetDpiForWindow(hwnd);
        CompactRestoreState saved;
        saved.presenter=normal;
        saved.maximized=normal.State()==OverlappedPresenterState::Maximized;
        saved.music=musicMode_.IsMusic();
        iima::GeometryRect bounds;
        if(saved.maximized) {
            WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
            check_bool(GetWindowPlacement(hwnd,&placement));
            auto rect=placement.rcNormalPosition;
            // WINDOWPLACEMENT uses workspace coordinates for ordinary windows,
            // but screen coordinates for WS_EX_TOOLWINDOW. Never apply either
            // coordinate system directly to AppWindow.MoveAndResize by mistake.
            if(!(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)) {
                OffsetRect(&rect,monitor.rcWork.left-monitor.rcMonitor.left,
                    monitor.rcWork.top-monitor.rcMonitor.top);
            }
            bounds={double(rect.left),double(rect.top),double(rect.right-rect.left),double(rect.bottom-rect.top)};
        } else {
            const auto p=window.Position(); const auto size=window.Size();
            bounds={double(p.X),double(p.Y),double(size.Width),double(size.Height)};
        }
        saved.geometry=iima::CaptureWindowPlacement(bounds,
            {double(monitor.rcWork.left),double(monitor.rcWork.top),double(monitor.rcWork.right-monitor.rcWork.left),
                double(monitor.rcWork.bottom-monitor.rcWork.top)},dpi,iima::WindowPlacementMode::Normal);
        if(!saved.geometry) {ShowError(L"无法保存小窗之前的窗口位置，未切换呈现模式。");return false;}
        const auto generation=session_->Snapshot().mediaGeneration,intent=session_->Snapshot().mediaOpenIntentRevision,input=inputGeneration_;
        if(!saved.music) windowPlacement_=saved.geometry;
        compactRestore_=std::move(saved);
        windowScaleExpectedPanelWidth_.reset();
        compactLayoutPending_=true;
        window.SetPresenter(AppWindowPresenterKind::CompactOverlay);
        if(window.Presenter().Kind()!=AppWindowPresenterKind::CompactOverlay) {
            UpdatePresentationLayout();
            ShowError(L"小窗切换未完成；可使用返回播放器重试恢复。"); return false;
        }
        if(CompactTransportAvailable() && preferences_.Boolean("pauseWhenPip") &&
            session_->Snapshot().mediaGeneration==generation && session_->Snapshot().mediaOpenIntentRevision==intent && inputGeneration_==input)
            session_->Pause(true);
        UpdatePresentationLayout();
        return true;
    } catch(hresult_error const& e) {
        // A failed entry can already have altered normal bounds. Preserve the
        // saved baseline for the same retry path as an interrupted exit.
        if(compactRestore_) compactLayoutPending_=true;
        try {UpdatePresentationLayout();}catch(...){}
        ShowError(e.message()); return false;
    }
}
bool MainWindow::ExitCompactOverlay(bool restoreGeometry) {
    if(closing_ || compactTransition_ || fullscreenTransition_) return false;
    auto window=AppWindow();
    if(window.Presenter().Kind()!=AppWindowPresenterKind::CompactOverlay && !compactRestore_) return true;
    struct Guard { bool& value; ~Guard(){value=false;} } guard{compactTransition_};
    compactTransition_=true;
    CancelMouseClick(); CancelThumbnailPreview(); CancelControlBarDrag(); CancelScrubbing(); CancelAutomaticVideoSize();
    const bool wasRestoring=restoringPlacement_;
    struct PlacementGuard { bool& value; bool prior; ~PlacementGuard(){value=prior;} } placementGuard{restoringPlacement_,wasRestoring};
    restoringPlacement_=true;
    try {
        // Choose monitor/DPI BEFORE reattaching a presenter which may move the
        // native window back to its old monitor. User moved the PIP: follow it.
        auto display=DisplayArea::GetFromWindowId(window.Id(),DisplayAreaFallback::Nearest);
        HWND hwnd{}; check_hresult(as<::IWindowNative>()->get_WindowHandle(&hwnd));
        const auto dpi=GetDpiForWindow(hwnd);
        std::optional<iima::GeometryRect> bounds;
        if(restoreGeometry && compactRestore_ && compactRestore_->geometry && display) {
            const auto work=display.WorkArea();
            bounds=iima::RestoreWindowPlacement(*compactRestore_->geometry,
                {double(work.X),double(work.Y),double(work.Width),double(work.Height)},dpi,
                {640*dpi/96.0,(compactRestore_->music ? 290.0 : 360.0)*dpi/96.0});
            if(!bounds) {ShowError(L"当前屏幕无法恢复播放器位置，未改变当前呈现模式。");return false;}
        }
        if(restoreGeometry && compactRestore_ && !bounds) {
            ShowError(L"当前屏幕不可用，请稍后重试返回。"); return false;
        }
        compactLayoutPending_=true;
        if(compactRestore_ && compactRestore_->presenter) window.SetPresenter(compactRestore_->presenter);
        else window.SetPresenter(AppWindowPresenterKind::Overlapped);
        auto normal=window.Presenter().try_as<OverlappedPresenter>();
        if(!normal) return false;
        windowScaleExpectedPanelWidth_.reset();
        if(bounds) {
            normal.Restore();
            placementProgrammaticBounds_=Windows::Graphics::RectInt32{int(std::ceil(bounds->x)),int(std::ceil(bounds->y)),
                int(std::floor(bounds->width)),int(std::floor(bounds->height))};
            window.MoveAndResize(*placementProgrammaticBounds_);
            // Store only the successfully restored normal rectangle, never a
            // transient compact-size rectangle or the final maximized bounds.
            if(!compactRestore_->music && display) {
                auto work=display.WorkArea();
                auto captured=iima::CaptureWindowPlacement(*bounds,
                    {double(work.X),double(work.Y),double(work.Width),double(work.Height)},dpi,iima::WindowPlacementMode::Normal);
                if(captured) windowPlacement_=*captured;
            }
            if(compactRestore_->maximized) normal.Maximize();
        } else if(!restoreGeometry) normal.Restore();
        compactRestore_.reset();
        UpdatePresentationLayout();
        QueueWindowScaleFeedback();
        return true;
    } catch(hresult_error const& e) {
        // Keep the unfinished record even when SetPresenter succeeded. The
        // visible retry action owns it until restore or explicit user sizing.
        compactLayoutPending_=true;
        windowScaleExpectedPanelWidth_.reset();
        try {UpdatePresentationLayout();}catch(...){}
        ShowError(L"窗口恢复未完成："+e.message()); return false;
    }
}
}

namespace winrt::IIMA::implementation {
void MainWindow::UpdateCompactLayout() {
    if(!compactLayoutPending_ || compactTransition_ || closing_) return;
    if(compactRestore_ && AppWindow().Presenter().Kind()==AppWindowPresenterKind::Overlapped) return;
    try {
        if(AppWindow().Presenter().Kind()!=AppWindowPresenterKind::Overlapped) {
            compactLayoutPending_=false; return;
        }
        HWND hwnd{}; check_hresult(as<::IWindowNative>()->get_WindowHandle(&hwnd));
        RECT client{}; if(!GetClientRect(hwnd,&client)) return;
        auto content=Content().try_as<Microsoft::UI::Xaml::FrameworkElement>();
        const auto sx=VideoPanel().CompositionScaleX(),sy=VideoPanel().CompositionScaleY();
        if(!content || !content.IsLoaded() || !std::isfinite(sx) || !std::isfinite(sy) || sx<=0 || sy<=0 ||
            std::abs(content.ActualWidth()*sx-(client.right-client.left))>2 ||
            std::abs(content.ActualHeight()*sy-(client.bottom-client.top))>2) return;
        const auto sidebar=Root().ColumnDefinitions().GetAt(1).Width().Value;
        if(std::abs(Root().ActualWidth()-VideoPanel().ActualWidth()-sidebar)>2 ||
            std::abs(Root().ActualHeight()-VideoPanel().ActualHeight())>2) return;
        compactLayoutPending_=false;
        windowScaleExpectedPanelWidth_.reset();
        QueueWindowScaleFeedback();
    } catch(...) {} // Wait for a valid native/XAML layout; never guess chrome.
}
}

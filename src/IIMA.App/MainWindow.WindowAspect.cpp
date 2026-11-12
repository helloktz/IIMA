// IINA MainWindowController.setWindowAspectRatio/currentWindowAspectRatio.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <cmath>

namespace winrt::IIMA::implementation {
void MainWindow::UpdateWindowAspectConstraint(bool layoutChanged) {
    if (!windowLifecycle_) return;
    auto clear = [this] { windowLifecycle_->SetAspectConstraint({}); };
    try {
        auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        if (closing_ || !userDataLoaded_ || preferences_.Boolean("unlockWindowAspectRatio") ||
            fullscreen_ || fullscreenTransition_ || musicMode_.IsMusic() || interactiveCrop_.Active() ||
            cropOriginal_ || !presenter ||
            presenter.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored) {
            clear(); return;
        }
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        const auto dpi = GetDpiForWindow(hwnd);
        const auto outer = AppWindow().Size();
        const bool sizing = windowLifecycle_->Current().sizing;
        // Only a completed video-panel layout can establish fixed non-video
        // chrome. Snapshot updates may arrive before XAML catches up to AppWindow.
        if (layoutChanged) {
            RECT client{};
            const double scale = VideoPanel().CompositionScaleX();
            auto content = Content().try_as<Microsoft::UI::Xaml::FrameworkElement>();
            if (!GetClientRect(hwnd, &client) || !content || !content.IsLoaded() ||
                std::abs(content.ActualWidth() * scale - (client.right - client.left)) > 2 ||
                std::abs(content.ActualHeight() * VideoPanel().CompositionScaleY() - (client.bottom - client.top)) > 2) {
                // A lagging shadow layout must not disarm this drag's already
                // validated native snapshot. Outside drag, wait for fresh layout.
                if (!sizing) clear();
                return;
            }
            const double width = VideoPanel().ActualWidth() * VideoPanel().CompositionScaleX();
            const double height = VideoPanel().ActualHeight() * VideoPanel().CompositionScaleY();
            const double chromeWidth = outer.Width - width, chromeHeight = outer.Height - height;
            if (!dpi || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
                chromeWidth < 0 || chromeHeight < 0) {
                if (!sizing) { windowAspectProjection_.reset(); clear(); }
                return;
            }
            iima::WindowAspectConstraint sampled{};
            if (windowAspectProjection_) sampled.video = windowAspectProjection_->video;
            sampled.chrome = {chromeWidth, chromeHeight};
            sampled.minimum = {640.0 * dpi / 96, 360.0 * dpi / 96};
            sampled.projectedOuter = {double(outer.Width), double(outer.Height)};
            sampled.dpi = dpi;
            windowAspectProjection_ = sampled;
        }
        auto const& state = session_->Snapshot();
        const auto video = VideoDisplaySize(state.videoOutputParameters);
        if (state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded ||
            !state.voConfigured || !state.mediaVideoGeometryReady || state.mediaOriginPending ||
            !video || !windowAspectProjection_ || windowAspectProjection_->dpi != dpi) {
            clear(); return;
        }
        auto& projected = *windowAspectProjection_;
        if (sizing) {
            // LayoutUpdated may retain a new measurement for the next drag,
            // but the native adapter continues using its frozen constraint.
            // A media/filter aspect change cancels that old constraint.
            if (projected.video.width != video->width || projected.video.height != video->height) clear();
            return;
        }
        if (std::abs(projected.projectedOuter.width - outer.Width) > 1 ||
            std::abs(projected.projectedOuter.height - outer.Height) > 1) {
            clear(); return;
        }
        projected.video = *video;
        windowLifecycle_->SetAspectConstraint(projected);
    } catch (...) {
        // Unsupported or unsettled layout means unconstrained resizing, never
        // a guessed ratio or an exception escaping a native sizing callback.
        clear();
    }
}
}

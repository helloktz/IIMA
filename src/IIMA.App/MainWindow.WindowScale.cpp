// IINA PlayerCore.windowScaleChanged and MainWindowController.updateWindowParametersForMPV.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <cmath>

namespace winrt::IIMA::implementation {
void MainWindow::AcceptExternalWindowScale(iima::PlaybackSnapshot const& state) {
    if (state.externalWindowScaleRevision == externalWindowScaleRevision_) return;
    externalWindowScaleRevision_ = state.externalWindowScaleRevision;
    if (!state.mediaGeneration || !state.externalWindowScale) return;
    // This is an explicit external sizing operation, not a new file. Like
    // IINA's setWindowScale, skip fullscreen/PIP rather than queueing a surprise.
    if (compactTransition_ || compactLayoutPending_ || compactRestore_ || restoringPlacement_ || fullscreen_ || musicMode_.IsMusic() || musicTransition_ || pickerOpen_ ||
        interactiveCrop_.Active() || cropOriginal_ ||
        (windowLifecycle_ && windowLifecycle_->Current().sizing)) return;
    auto normal = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
    if (!normal || normal.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored) return;
    initialVideoSizePending_ = false;
    automaticResizeConsumed_ = false;
    automaticResizeStart_ = false;
    automaticResizeExternal_ = true;
    automaticResizeExternalScale_ = *state.externalWindowScale;
    automaticResizeLoaded_ = false;
    automaticResizeAttempts_ = 0;
    automaticResizeInformation_ = 0;
}
void MainWindow::QueueWindowScaleFeedback() {
    if (closing_ || windowScaleFeedbackQueued_) return;
    windowScaleFeedbackQueued_ = true;
    auto weak = get_weak();
    if (!DispatcherQueue().TryEnqueue([weak] {
        if (auto self = weak.get()) {
            self->windowScaleFeedbackQueued_ = false;
            self->PublishWindowScale();
        }
    })) windowScaleFeedbackQueued_ = false;
}
void MainWindow::PublishWindowScale() {
    if (closing_ || compactTransition_ || compactLayoutPending_ || compactRestore_ || restoringPlacement_ || !session_ || !userDataLoaded_ || fullscreen_ || fullscreenTransition_ ||
        musicMode_.IsMusic() || musicTransition_ || automaticResizeAttempting_ || !automaticResizeConsumed_) return;
    auto const& state = session_->Snapshot();
    if (state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded ||
        !state.voConfigured || !state.mediaVideoGeometryReady || state.mediaOriginPending) return;
    auto normal = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
    if (!normal || normal.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored) return;
    auto video = VideoDisplaySize(state.videoOutputParameters);
    if (!video) return;
    HWND hwnd{};
    if (FAILED(GetNativeWindowHandle(&hwnd))) return;
    const double units = preferences_.Boolean("usePhysicalResolution") ? 1.0 : GetDpiForWindow(hwnd) / 96.0;
    const double width = VideoPanel().ActualWidth() * VideoPanel().CompositionScaleX();
    if (windowScaleExpectedPanelWidth_) {
        if (std::abs(width - *windowScaleExpectedPanelWidth_) > 2) return;
        windowScaleExpectedPanelWidth_.reset();
    }
    const double scale = width / (video->width * units);
    if (std::isfinite(scale) && scale > 0 && scale <= 16) session_->SetWindowScaleFromWindow(scale);
}
}

// IINA MainWindowController.handleVideoSizeChange, justStartedFile/justOpenedFile,
// PlayerCore.onVideoReconfig and playbackRestarted.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"

namespace winrt::IIMA::implementation {
void MainWindow::CancelAutomaticVideoSize(bool discardInitial) {
    if (automaticResizeAttempting_) return; // Our own MoveAndResize is not user input.
    automaticResizeConsumed_ = true;
    if (discardInitial) { initialVideoSizePending_ = false; windowScaleExpectedPanelWidth_.reset(); }
    if (session_) {
        auto const& state = session_->Snapshot();
        if (automaticResizeGeneration_ != state.mediaGeneration) automaticReconfigArmed_ = false;
        automaticResizeGeneration_ = state.mediaGeneration;
        automaticResizeIntent_ = state.mediaOpenIntentRevision;
        automaticResizeVideo_ = VideoDisplaySize(state.videoOutputParameters);
    }
}
void MainWindow::UpdateAutomaticVideoSize(bool layoutChanged) {
    if (closing_ || compactTransition_ || compactLayoutPending_ || compactRestore_ || restoringPlacement_ || !session_ || automaticResizeAttempting_ || fullscreenTransition_) return;
    auto const& state = session_->Snapshot();
    if (state.mediaGeneration != automaticResizeGeneration_) {
        automaticResizeGeneration_ = state.mediaGeneration;
        automaticResizeIntent_ = state.mediaOpenIntentRevision;
        automaticResizeConsumed_ = !state.mediaGeneration;
        automaticResizeAwaitingStart_ = false;
        automaticResizeStart_ = true;
        automaticResizeExternal_ = false;
        automaticResizeLoaded_ = false;
        automaticResizeAttempts_ = 0;
        automaticResizeInformation_ = 0;
        automaticReconfigArmed_ = false;
        automaticResizeVideo_.reset();
    }
    if (state.mediaOpenIntentRevision != automaticResizeIntent_) {
        CancelAutomaticVideoSize(false);
        automaticResizeAwaitingStart_ = true;
        return; // A command intent is not a START_FILE generation.
    }
    if (automaticResizeAwaitingStart_) return;
    AcceptExternalWindowScale(state);
    auto video = VideoDisplaySize(state.videoOutputParameters);
    const bool changedSize = video && automaticResizeVideo_ &&
        (video->width != automaticResizeVideo_->width || video->height != automaticResizeVideo_->height);
    // Establish a baseline only after the first Restart's correlated async
    // video-output read. Cached properties at Restart can still describe old video.
    const bool reconfigured = automaticReconfigArmed_ && changedSize;
    if (video) automaticResizeVideo_ = video;
    if (state.mediaVideoGeometryReady && video) automaticReconfigArmed_ = true;
    if (automaticResizeConsumed_ && reconfigured && state.media == iima::MediaState::Loaded &&
        !state.mediaOriginPending) {
        automaticResizeConsumed_ = false;
        automaticResizeStart_ = false;
        automaticResizeExternal_ = false;
        automaticResizeLoaded_ = false;
        automaticResizeAttempts_ = 0;
        automaticResizeInformation_ = 0;
    }
    if (automaticResizeConsumed_) return;
    if (state.session != iima::SessionState::Ready || state.media == iima::MediaState::Empty ||
        state.media == iima::MediaState::Ended || state.media == iima::MediaState::Error) {
        CancelAutomaticVideoSize(false); return;
    }
    // Fullscreen updates only its captured normal restore target. Other modes
    // and user sizing are skipped, never applied late after a mode change.
    auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
    const bool deferredFullscreen = !automaticResizeExternal_ && fullscreen_ && fullscreenRestore_ && fullscreenRestore_->geometry &&
        fullscreenRestore_->videoSizingEligible;
    if ((!deferredFullscreen && (!presenter || presenter.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored)) ||
        musicMode_.IsMusic() || musicTransition_ || interactiveCrop_.Active() || cropOriginal_ || pickerOpen_ ||
        (windowLifecycle_ && windowLifecycle_->Current().sizing)) {
        CancelAutomaticVideoSize(); return;
    }
    if (!userDataLoaded_ || state.media != iima::MediaState::Loaded) return;
    const auto now = std::chrono::steady_clock::now();
    if (!automaticResizeLoaded_) {
        automaticResizeLoaded_ = true;
        automaticResizeTiming_ = static_cast<int>(preferences_.Integer("resizeWindowTiming"));
        automaticResizeOption_ = automaticResizeStart_ ? static_cast<int>(preferences_.Integer("resizeWindowOption")) : 2;
        automaticResizePhysical_ = preferences_.Boolean("usePhysicalResolution");
        automaticResizeInitial_ = automaticResizeStart_ && initialVideoSizePending_;
        automaticResizeKeepArea_ = false;
        automaticResizeDeadline_ = now + std::chrono::seconds(10);
    }
    // Snapshot preferences per request. A setting edit is not a new file open
    // or video reconfiguration. Mid-playback changes use 1x, even with never.
    if (now >= automaticResizeDeadline_) { CancelAutomaticVideoSize(); return; }
    if (state.mediaOriginPending || !state.mediaVideoGeometryReady) return;
    automaticResizeKeepArea_ = automaticResizeStart_ && !automaticResizeInitial_ &&
        (automaticResizeTiming_ == 2 || (automaticResizeTiming_ == 1 && !state.mediaExplicitOpen));
    if (!state.voConfigured) return;
    if (!layoutChanged && automaticResizeAttempts_ && automaticResizeInformation_ == state.videoOutputRevision) return;
    automaticResizeInformation_ = state.videoOutputRevision;
    if (++automaticResizeAttempts_ > 32) { CancelAutomaticVideoSize(); return; }
    static constexpr double scales[]{1.0, 0.5, 1.0, 1.5, 2.0};
    if (automaticResizeOption_ < 0 || automaticResizeOption_ > 4) { CancelAutomaticVideoSize(); return; }
    automaticResizeAttempting_ = true;
    const auto result = TryResizeToVideo(automaticResizeExternal_ ? automaticResizeExternalScale_ : scales[automaticResizeOption_], true);
    automaticResizeAttempting_ = false;
    if (result != VideoResizeResult::Retry) {
        automaticResizeConsumed_ = true;
        if (automaticResizeInitial_) initialVideoSizePending_ = false;
    }
}
}

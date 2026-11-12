// IINA MainWindowController.handleVideoSizeChange, justStartedFile/justOpenedFile.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"

namespace winrt::IIMA::implementation {
void MainWindow::CancelAutomaticVideoSize() {
    if (automaticResizeAttempting_) return; // Our own MoveAndResize is not user input.
    automaticResizeConsumed_ = true;
    if (session_) {
        automaticResizeGeneration_ = session_->Snapshot().mediaGeneration;
        automaticResizeIntent_ = session_->Snapshot().mediaOpenIntentRevision;
    }
}
void MainWindow::UpdateAutomaticVideoSize(bool layoutChanged) {
    if (closing_ || !session_ || automaticResizeAttempting_) return;
    auto const& state = session_->Snapshot();
    if (state.mediaGeneration != automaticResizeGeneration_) {
        const bool first = automaticResizeGeneration_ == 0;
        automaticResizeGeneration_ = state.mediaGeneration;
        automaticResizeIntent_ = state.mediaOpenIntentRevision;
        automaticResizeConsumed_ = !state.mediaGeneration;
        automaticResizeLoaded_ = false;
        automaticResizeAttempts_ = 0;
        automaticResizeInformation_ = 0;
        // Explicit startup geometry has already been applied after restoration.
        // Do not let first-media auto sizing undo it, or a startup user drag.
        if (first && (initialGeometryApplied_ || placementTouched_)) automaticResizeConsumed_ = true;
    }
    if (state.mediaOpenIntentRevision != automaticResizeIntent_) {
        CancelAutomaticVideoSize();
        return; // A command intent is not a START_FILE generation.
    }
    if (automaticResizeConsumed_) return;
    if (state.session != iima::SessionState::Ready || state.media == iima::MediaState::Empty ||
        state.media == iima::MediaState::Ended || state.media == iima::MediaState::Error) {
        CancelAutomaticVideoSize(); return;
    }
    // Non-normal modes are skipped, never deferred until the user returns later.
    auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
    if (!presenter || presenter.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored ||
        fullscreen_ || musicMode_.IsMusic() || musicTransition_ || interactiveCrop_.Active() || cropOriginal_ || pickerOpen_) {
        CancelAutomaticVideoSize(); return;
    }
    if (!userDataLoaded_ || state.media != iima::MediaState::Loaded) return;
    const auto now = std::chrono::steady_clock::now();
    if (!automaticResizeLoaded_) {
        automaticResizeLoaded_ = true;
        automaticResizeTiming_ = static_cast<int>(preferences_.Integer("resizeWindowTiming"));
        automaticResizeOption_ = static_cast<int>(preferences_.Integer("resizeWindowOption"));
        automaticResizePhysical_ = preferences_.Boolean("usePhysicalResolution");
        automaticResizeDeadline_ = now + std::chrono::seconds(10);
    }
    // Snapshot preferences once per media. Editing settings cannot manufacture
    // a new open, and cannot turn an already skipped generation into a resize.
    if (automaticResizeTiming_ == 2 || now >= automaticResizeDeadline_) {
        CancelAutomaticVideoSize(); return;
    }
    if (state.mediaOriginPending) return;
    if (automaticResizeTiming_ == 1 && !state.mediaExplicitOpen) {
        CancelAutomaticVideoSize(); return;
    }
    if (!state.voConfigured) return;
    // Position/time notifications must not repeatedly do native geometry work.
    if (!layoutChanged && automaticResizeAttempts_ && automaticResizeInformation_ == state.videoOutputRevision) return;
    automaticResizeInformation_ = state.videoOutputRevision;
    if (++automaticResizeAttempts_ > 32) { CancelAutomaticVideoSize(); return; }
    static constexpr double scales[]{1.0, 0.5, 1.0, 1.5, 2.0};
    if (automaticResizeOption_ < 0 || automaticResizeOption_ > 4) { CancelAutomaticVideoSize(); return; }
    // fitScreen uses 1x constrained by the work area, not manual "fill work area".
    automaticResizeAttempting_ = true;
    const auto result = TryResizeToVideo(scales[automaticResizeOption_], true);
    automaticResizeAttempting_ = false;
    if (result != VideoResizeResult::Retry) automaticResizeConsumed_ = true;
}
}

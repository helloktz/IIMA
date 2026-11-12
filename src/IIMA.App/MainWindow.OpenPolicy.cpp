// IINA PlayerCore.openURLs / findIdlePlayerCore, Windows window integration.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"

namespace winrt::IIMA::implementation {
bool MainWindow::CanReuseForInput() const
{
    if (closing_ || inputAssigned_ || pickerOpen_ || inputResolving_ || !pendingFiles_.empty() ||
        !activationInputs_.empty() || (pluginRuntime_ || pluginStarting_ || pluginStopPending_ || pluginInspectionBusy_) || onlineSubtitleWorkerActive_) return false;
    auto const& state = session_->Snapshot();
    // Reuse pristine windows only. Keep a reservation after submitting an open,
    // even before START_FILE arrives; an empty snapshot is not an idle proof.
    // Previously used, ended and failed windows are never silently replaced.
    return (state.session == iima::SessionState::Initializing || state.session == iima::SessionState::Ready) &&
        state.media == iima::MediaState::Empty && state.path.empty() && state.playlist.empty();
}
bool MainWindow::HasOpenInput(std::string const& path) const
{
    if (closing_) return false;
    if (!pendingFiles_.empty()) return pendingFiles_.front() == path;
    auto const& state = session_->Snapshot();
    return (state.media == iima::MediaState::Loading || state.media == iima::MediaState::Loaded ||
            state.media == iima::MediaState::Ended) && state.path == path;
}
void MainWindow::ReceiveResolvedInput(std::vector<std::string> paths)
{
    if (closing_ || paths.empty()) return;
    // Input was resolved once on the originating window's worker. Re-enumerating
    // would both change the policy grouping and recursively route it again.
    if (activationTimer_) activationTimer_.Stop();
    activationInputs_.clear(); activationDelayElapsed_ = false;
    inputStop_.request_stop(); ++inputGeneration_; inputResolving_ = false;
    inputAssigned_ = true;
    pendingFiles_ = std::move(paths);
    pendingAppend_ = false;
    BeginNetworkOpen(pendingFiles_);
    ReceiveActivation({});
    OpenPendingFiles(); // Waits for this window's own settings and mpv startup.
}
}

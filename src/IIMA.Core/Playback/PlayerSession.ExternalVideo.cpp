// IINA PlayerCore.loadExternalVideoFile / JavascriptAPICore.TrackAPI.loadTrack.
// Copyright (c) 2016-2026 lhc and IINA contributors. GPL-3.0.
// See references/iina-source.txt and references/iina/LICENSE.
// Windows local-file picker adaptation; no raw command or playlist-load surface.
#include "PlayerSession.h"
#include <algorithm>
#include <string_view>
namespace iima {
namespace {
bool LocalVideoPath(std::string_view path) {
    if (path.size() < 4 || path.size() > 131072 ||
        !((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) ||
        path[1] != ':' || (path[2] != '\\' && path[2] != '/') || path.find(':', 2) != path.npos ||
        std::any_of(path.begin(), path.end(), [](unsigned char ch) { return ch < 32 || ch == 127 || ch == '|' || ch == '?' || ch == '*' || ch == '<' || ch == '>' || ch == '"'; })) return false;
    std::size_t begin = 3;
    while (begin < path.size()) {
        const auto end = path.find_first_of("/\\", begin);
        const auto part = path.substr(begin, end == path.npos ? path.size() - begin : end - begin);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        auto stem = std::string(part.substr(0, part.find('.')));
        while (!stem.empty() && stem.back() == ' ') stem.pop_back();
        for (auto& ch : stem) if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$" ||
            ((stem.starts_with("COM") || stem.starts_with("LPT")) &&
                ((stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9') || stem.substr(3) == "\xc2\xb9" ||
                 stem.substr(3) == "\xc2\xb2" || stem.substr(3) == "\xc2\xb3"))) return false;
        if (end == path.npos) return true;
        begin = end + 1;
    }
    return false;
}
}
void PlayerSession::CancelExternalVideoLoad() {
    if (externalVideoLoadCancelled_) externalVideoLoadCancelled_->store(true);
    if (externalVideoLoadStatus_.phase != ExternalVideoLoadStatus::Phase::Loading) return;
    externalVideoLoadStatus_.phase = ExternalVideoLoadStatus::Phase::Cancelled;
    Notify(); // Retain the request until drain; no promise to recall submitted work.
}
bool PlayerSession::LoadExternalVideo(std::string path) {
    BeginTrackEditIntent();
    CancelScrub();
    CancelSubtitleReload();
    if (ExternalVideoLoadBusy() || ExternalTrackRemovalBusy() || SubtitleReloadBusy() ||
        snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || snapshot_.mediaOriginPending || pauseRequest_ || loadRequest_ ||
        !pendingCommands_.empty() || playlistEdit_ || snapshot_.entryId < 0 ||
        snapshot_.path.empty() || snapshot_.path.size() > 131072 || !LocalVideoPath(path)) return false;
    // No presentationReady_ requirement: adding a video is useful for an
    // audio-only source or one whose output is not currently ready.
    externalVideoLoadGeneration_ = snapshot_.mediaGeneration;
    externalVideoLoadOpenIntent_ = snapshot_.mediaOpenIntentRevision;
    externalVideoLoadEditIntent_ = trackEditIntentRevision_;
    externalVideoLoadEntry_ = snapshot_.entryId;
    externalVideoLoadMediaPath_ = snapshot_.path;
    const auto revision = externalVideoLoadStatus_.revision + 1;
    externalVideoLoadStatus_ = {};
    externalVideoLoadStatus_.revision = revision;
    externalVideoLoadStatus_.phase = ExternalVideoLoadStatus::Phase::Loading;
    try {
        externalVideoLoadCancelled_ = std::make_shared<std::atomic_bool>(false);
        externalVideoLoadRequest_ = client_->LoadExternalVideoAsync(std::move(path), externalVideoLoadEntry_,
            externalVideoLoadMediaPath_, externalVideoLoadCancelled_);
    } catch (...) { externalVideoLoadRequest_ = 0; }
    externalVideoLoadStatus_.inFlight = externalVideoLoadRequest_ != 0;
    if (!externalVideoLoadRequest_) {
        externalVideoLoadStatus_.phase = ExternalVideoLoadStatus::Phase::Failed;
        externalVideoLoadStatus_.error = "Playback worker rejected external video loading";
        externalVideoLoadCancelled_.reset();
    }
    const bool accepted = externalVideoLoadRequest_ != 0;
    Notify(); return accepted;
}
bool PlayerSession::HandleExternalVideoLoadReply(MpvEvent const& event) {
    if (event.kind != MpvEvent::Kind::ExternalVideoLoadReply) return false;
    if (!externalVideoLoadRequest_ || event.requestId != externalVideoLoadRequest_) return true;
    externalVideoLoadRequest_ = 0;
    externalVideoLoadStatus_.inFlight = false;
    externalVideoLoadStatus_.submitted = event.externalVideoLoadSubmitted;
    externalVideoLoadStatus_.selectedConfirmed = event.externalVideoSelectedConfirmed;
    const bool cancelled = externalVideoLoadStatus_.phase == ExternalVideoLoadStatus::Phase::Cancelled ||
        (externalVideoLoadCancelled_ && externalVideoLoadCancelled_->load()) ||
        snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        snapshot_.mediaGeneration != externalVideoLoadGeneration_ || snapshot_.mediaOpenIntentRevision != externalVideoLoadOpenIntent_ ||
        trackEditIntentRevision_ != externalVideoLoadEditIntent_ || snapshot_.entryId != externalVideoLoadEntry_ ||
        snapshot_.path != externalVideoLoadMediaPath_;
    externalVideoLoadCancelled_.reset();
    if (cancelled) externalVideoLoadStatus_.phase = ExternalVideoLoadStatus::Phase::Cancelled;
    else if (event.error >= 0 && event.externalVideoSelectedConfirmed) externalVideoLoadStatus_.phase = ExternalVideoLoadStatus::Phase::Completed;
    else {
        externalVideoLoadStatus_.phase = ExternalVideoLoadStatus::Phase::Failed;
        externalVideoLoadStatus_.error = event.externalVideoSelectedConfirmed ?
            "mpv reported an error, although fresh state confirms the requested external video is selected" :
            event.externalVideoLoadSubmitted ?
            "Loading was submitted, but selection of the requested external video could not be confirmed; tracks may have changed" :
            "Loading was not submitted because the player, media identity, or local path was unavailable";
    }
    // Track-list/vid/video-reconfig and owned swapchain observations drive all
    // playback, music-mode and geometry changes. This reply cannot grant a VO
    // readiness lease or synthesize successful rendering.
    Notify(); return true;
}
}

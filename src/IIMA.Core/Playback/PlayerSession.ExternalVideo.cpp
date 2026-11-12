// IINA PlayerCore.loadExternalAudioFile/loadExternalVideoFile and
// JavascriptAPICore.TrackAPI.loadTrack. Copyright (c) 2016, 2018 lhc. GPL-3.0.
// See references/iina-source.txt and references/iina/LICENSE.
// Windows local-file picker adaptation; no raw command or playlist-load surface.
#include "PlayerSession.h"
#include <algorithm>
#include <string_view>
#include <utility>
namespace iima {
bool PlayerSession::LocalTrackPath(std::string_view path) {
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
void PlayerSession::CancelExternalAVLoad() {
    if (externalAVLoadCancelled_) externalAVLoadCancelled_->store(true);
    if (externalAVLoadStatus_.phase != ExternalAVLoadStatus::Phase::Loading) return;
    externalAVLoadStatus_.phase = ExternalAVLoadStatus::Phase::Cancelled;
    Notify(); // Retain the request until drain; no promise to recall submitted work.
}
bool PlayerSession::LoadExternalAudio(std::string path) {
    return LoadExternalAV("audio", std::move(path));
}
bool PlayerSession::LoadExternalVideo(std::string path) {
    return LoadExternalAV("video", std::move(path));
}
bool PlayerSession::LoadExternalAV(std::string type, std::string path) {
    BeginTrackEditIntent();
    CancelScrub();
    CancelSubtitleLoad(); CancelSubtitleReload();
    if (SubtitleLoadBusy() || (type != "audio" && type != "video") || ExternalTrackReloadBusy() || ExternalAVLoadBusy() || ExternalTrackRemovalBusy() || SubtitleReloadBusy() ||
        snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || snapshot_.mediaOriginPending || pauseRequest_ || loadRequest_ ||
        !pendingCommands_.empty() || playlistEdit_ || snapshot_.entryId < 0 ||
        snapshot_.path.empty() || snapshot_.path.size() > 131072 || !LocalTrackPath(path)) return false;
    // No presentationReady_ requirement: adding an external track can repair
    // a source whose audio/video output is not currently ready.
    externalAVLoadGeneration_ = snapshot_.mediaGeneration;
    externalAVLoadOpenIntent_ = snapshot_.mediaOpenIntentRevision;
    externalAVLoadEditIntent_ = trackEditIntentRevision_;
    externalAVLoadEntry_ = snapshot_.entryId;
    externalAVLoadMediaPath_ = snapshot_.path;
    const auto revision = externalAVLoadStatus_.revision + 1;
    externalAVLoadStatus_ = {};
    externalAVLoadStatus_.revision = revision;
    externalAVLoadStatus_.type = type;
    externalAVLoadStatus_.phase = ExternalAVLoadStatus::Phase::Loading;
    try {
        externalAVLoadCancelled_ = std::make_shared<std::atomic_bool>(false);
        externalAVLoadRequest_ = client_->LoadExternalAVAsync(std::move(type), std::move(path), externalAVLoadEntry_,
            externalAVLoadMediaPath_, externalAVLoadCancelled_);
    } catch (...) { externalAVLoadRequest_ = 0; }
    externalAVLoadStatus_.inFlight = externalAVLoadRequest_ != 0;
    if (!externalAVLoadRequest_) {
        externalAVLoadStatus_.phase = ExternalAVLoadStatus::Phase::Failed;
        externalAVLoadStatus_.error = "Playback worker rejected external audio/video loading";
        externalAVLoadCancelled_.reset();
    }
    const bool accepted = externalAVLoadRequest_ != 0;
    Notify(); return accepted;
}
bool PlayerSession::HandleExternalAVLoadReply(MpvEvent const& event) {
    if (event.kind != MpvEvent::Kind::ExternalAVLoadReply) return false;
    if (!externalAVLoadRequest_ || event.requestId != externalAVLoadRequest_) return true;
    externalAVLoadRequest_ = 0;
    externalAVLoadStatus_.inFlight = false;
    externalAVLoadStatus_.submitted = event.externalAVLoadSubmitted;
    externalAVLoadStatus_.commandAcknowledged = event.externalAVLoadCommandSucceeded;
    externalAVLoadStatus_.selectedConfirmed = event.externalAVSelectedConfirmed;
    const bool cancelled = externalAVLoadStatus_.phase == ExternalAVLoadStatus::Phase::Cancelled ||
        (externalAVLoadCancelled_ && externalAVLoadCancelled_->load()) ||
        snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        snapshot_.mediaGeneration != externalAVLoadGeneration_ || snapshot_.mediaOpenIntentRevision != externalAVLoadOpenIntent_ ||
        trackEditIntentRevision_ != externalAVLoadEditIntent_ || snapshot_.entryId != externalAVLoadEntry_ ||
        snapshot_.path != externalAVLoadMediaPath_;
    externalAVLoadCancelled_.reset();
    if (cancelled) externalAVLoadStatus_.phase = ExternalAVLoadStatus::Phase::Cancelled;
    else if (event.error >= 0 && event.externalAVSelectedConfirmed) externalAVLoadStatus_.phase = ExternalAVLoadStatus::Phase::Completed;
    else {
        externalAVLoadStatus_.phase = ExternalAVLoadStatus::Phase::Failed;
        externalAVLoadStatus_.error = event.externalAVSelectedConfirmed ?
            "mpv reported an error, although fresh state confirms the requested external audio/video track is selected" :
            event.externalAVLoadSubmitted ?
            "Loading was submitted, but selection of the requested external audio/video track could not be confirmed; tracks may have changed" :
            "Loading was not submitted because the player, media identity, or local path was unavailable";
    }
    // Track-list/aid/vid/video-reconfig and owned swapchain observations drive all
    // playback, music-mode and geometry changes. This reply cannot grant a VO
    // readiness lease or synthesize successful rendering.
    Notify(); return true;
}
}

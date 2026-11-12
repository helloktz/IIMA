// Typed Windows adaptation of IINA AppData input-command support for
// sub-remove/audio-remove/video-remove (rather than a new raw command surface).
// Copyright (c) 2016-2026 lhc and IINA contributors. GPL-3.0.
// Sources: references/iina-source.txt, references/iina/LICENSE;
// mpv player/command.c cmd_track_remove and loadfile.c mp_remove_track.
#include "PlayerSession.h"
#include <algorithm>

namespace iima {
void PlayerSession::BeginTrackEditIntent() {
    ++trackEditIntentRevision_;
    CancelSubtitleLoad();
    CancelExternalTrackReload();
    CancelExternalAVLoad();
    CancelExternalTrackRemoval();
}
void PlayerSession::CancelExternalTrackRemoval() {
    if (externalTrackRemovalCancelled_) externalTrackRemovalCancelled_->store(true);
    if (externalTrackRemovalStatus_.phase != ExternalTrackRemovalStatus::Phase::Removing) return;
    externalTrackRemovalStatus_.phase = ExternalTrackRemovalStatus::Phase::Cancelled;
    // The worker tests the token before submission. This is not an atomic
    // cancellation of a command already handed to mpv: retain request/drain.
    Notify();
}
bool PlayerSession::RemoveExternalTrack(std::string type, std::int64_t id, std::string externalPath) {
    BeginTrackEditIntent();
    CancelScrub();
    CancelSubtitleLoad(); CancelSubtitleReload();
    if (type == "sub") ++subtitleEditIntentRevision_;
    if (SubtitleLoadBusy() || ExternalTrackReloadBusy() || ExternalAVLoadBusy() || ExternalTrackRemovalBusy() || SubtitleReloadBusy() || snapshot_.session != SessionState::Ready ||
        snapshot_.media != MediaState::Loaded || waitingForStart_ || snapshot_.mediaOriginPending ||
        pauseRequest_ || loadRequest_ || !pendingCommands_.empty() || playlistEdit_ || snapshot_.entryId < 0 ||
        snapshot_.path.empty() || snapshot_.path.size() > 131072 ||
        (type != "audio" && type != "video" && type != "sub") || id <= 0 ||
        externalPath.empty() || externalPath.size() > 131072 || externalPath.find('\0') != std::string::npos) return false;
    const auto count = std::count_if(snapshot_.tracks.begin(), snapshot_.tracks.end(), [&](auto const& track) {
        return track.type == type && track.id == id;
    });
    const auto found = std::find_if(snapshot_.tracks.begin(), snapshot_.tracks.end(), [&](auto const& track) {
        return track.type == type && track.id == id && track.external && track.externalPath == externalPath;
    });
    if (count != 1 || found == snapshot_.tracks.end()) return false;
    externalTrackRemovalGeneration_ = snapshot_.mediaGeneration;
    externalTrackRemovalOpenIntent_ = snapshot_.mediaOpenIntentRevision;
    externalTrackRemovalEditIntent_ = trackEditIntentRevision_;
    externalTrackRemovalEntry_ = snapshot_.entryId;
    externalTrackRemovalMediaPath_ = snapshot_.path;
    const auto revision = externalTrackRemovalStatus_.revision + 1;
    externalTrackRemovalStatus_ = {};
    externalTrackRemovalStatus_.revision = revision;
    externalTrackRemovalStatus_.phase = ExternalTrackRemovalStatus::Phase::Removing;
    try {
        externalTrackRemovalCancelled_ = std::make_shared<std::atomic_bool>(false);
        externalTrackRemovalRequest_ = client_->RemoveExternalTrackAsync(std::move(type), id, std::move(externalPath),
            externalTrackRemovalEntry_, externalTrackRemovalMediaPath_, externalTrackRemovalCancelled_);
    } catch (...) {
        externalTrackRemovalRequest_ = 0;
    }
    externalTrackRemovalStatus_.inFlight = externalTrackRemovalRequest_ != 0;
    if (!externalTrackRemovalRequest_) {
        externalTrackRemovalStatus_.phase = ExternalTrackRemovalStatus::Phase::Failed;
        externalTrackRemovalStatus_.error = "Playback worker rejected external track removal";
        externalTrackRemovalCancelled_.reset();
    }
    const bool accepted = externalTrackRemovalRequest_ != 0;
    Notify();
    return accepted;
}
bool PlayerSession::HandleExternalTrackRemovalReply(MpvEvent const& event) {
    if (event.kind != MpvEvent::Kind::ExternalTrackRemovalReply) return false;
    if (!externalTrackRemovalRequest_ || event.requestId != externalTrackRemovalRequest_) return true;
    externalTrackRemovalRequest_ = 0;
    externalTrackRemovalStatus_.inFlight = false;
    externalTrackRemovalStatus_.submitted = event.externalTrackRemovalSubmitted;
    externalTrackRemovalStatus_.removedConfirmed = event.externalTrackRemovedConfirmed;
    const bool cancelled = externalTrackRemovalStatus_.phase == ExternalTrackRemovalStatus::Phase::Cancelled ||
        (externalTrackRemovalCancelled_ && externalTrackRemovalCancelled_->load()) ||
        snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        snapshot_.mediaGeneration != externalTrackRemovalGeneration_ ||
        snapshot_.mediaOpenIntentRevision != externalTrackRemovalOpenIntent_ ||
        trackEditIntentRevision_ != externalTrackRemovalEditIntent_ ||
        snapshot_.entryId != externalTrackRemovalEntry_ || snapshot_.path != externalTrackRemovalMediaPath_;
    externalTrackRemovalCancelled_.reset();
    if (cancelled) externalTrackRemovalStatus_.phase = ExternalTrackRemovalStatus::Phase::Cancelled;
    else if (event.error >= 0 && event.externalTrackRemovedConfirmed) {
        externalTrackRemovalStatus_.phase = ExternalTrackRemovalStatus::Phase::Completed;
    } else {
        externalTrackRemovalStatus_.phase = ExternalTrackRemovalStatus::Phase::Failed;
        // Do not expose arbitrary worker/native text or confuse command success
        // with removal: mp_remove_track can refuse while command reply succeeds.
        externalTrackRemovalStatus_.error = event.externalTrackRemovedConfirmed ?
            "mpv reported an error, although a fresh track list confirms the original track is now absent" :
            event.externalTrackRemovalSubmitted ?
            "Removal was submitted, but a fresh complete track list did not confirm its outcome; the track may have changed" :
            "Removal was not submitted: the media or external track identity was unavailable or changed";
    }
    // Ordinary track-list/aid/vid/sid observations remain playback truth. Do not
    // patch them from this result, auto-select another track, or delete a file.
    Notify(); return true;
}
}

// IINA AppData.swift audioReload/videoReload command allowlist.
// Copyright © 2016 lhc. All rights reserved.
// GPL-3.0; references/iina/LICENSE and references/iina-source.txt.
// Selection-preserving Windows transaction; mpv cmd_track_reload removes then
// re-adds a track and selects it, even when the requested track was not selected.
#include "PlayerSession.h"
#include <algorithm>
namespace iima {
void PlayerSession::CancelExternalTrackReload() {
    if (externalTrackReloadCancelled_) externalTrackReloadCancelled_->store(true);
    auto& status = externalTrackReloadStatus_;
    if (status.phase != ExternalTrackReloadStatus::Phase::Reading && status.phase != ExternalTrackReloadStatus::Phase::Reloading &&
        status.phase != ExternalTrackReloadStatus::Phase::Restoring) return;
    status.phase = ExternalTrackReloadStatus::Phase::Cancelled;
    Notify(); // Retain/drain the one submitted request. Never rollback a reload.
}
void PlayerSession::FailExternalTrackReload(std::string error) {
    externalTrackReloadStatus_.phase = ExternalTrackReloadStatus::Phase::Failed;
    externalTrackReloadStatus_.error = std::move(error);
}
bool PlayerSession::ReloadExternalTrack(std::string type, std::int64_t id, std::string externalPath) {
    BeginTrackEditIntent(); CancelScrub(); CancelSubtitleLoad(); CancelSubtitleReload();
    if (SubtitleLoadBusy() || ExternalTrackReloadBusy() || ExternalAVLoadBusy() || ExternalTrackRemovalBusy() || SubtitleReloadBusy() ||
        snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded || waitingForStart_ ||
        snapshot_.mediaOriginPending || pauseRequest_ || loadRequest_ || !pendingCommands_.empty() || playlistEdit_ ||
        snapshot_.entryId < 0 || snapshot_.path.empty() || snapshot_.path.size() > 131072 ||
        (type != "audio" && type != "video") || id <= 0 || externalPath.empty() || externalPath.size() > 131072 ||
        externalPath.find('\0') != std::string::npos) return false;
    if (std::count_if(snapshot_.tracks.begin(), snapshot_.tracks.end(), [&](auto const& track) {
        return track.type == type && track.id == id;
    }) != 1 || std::none_of(snapshot_.tracks.begin(), snapshot_.tracks.end(), [&](auto const& track) {
        return track.type == type && track.id == id && track.external && track.externalPath == externalPath;
    })) return false;
    externalTrackReloadGeneration_ = snapshot_.mediaGeneration;
    externalTrackReloadOpenIntent_ = snapshot_.mediaOpenIntentRevision;
    externalTrackReloadEditIntent_ = trackEditIntentRevision_;
    externalTrackReloadEntry_ = snapshot_.entryId; externalTrackReloadMediaPath_ = snapshot_.path;
    externalTrackReloadType_ = std::move(type); externalTrackReloadTargetId_ = id; externalTrackReloadTargetPath_ = std::move(externalPath);
    externalTrackReloadOriginalId_ = 0; externalTrackReloadOriginalExternal_ = false; externalTrackReloadOriginalPath_.clear();
    externalTrackReloadHadError_ = false; externalTrackReloadRequestWasReload_ = false; externalTrackReloadBaselineCaptured_ = false;
    const auto revision = externalTrackReloadStatus_.revision + 1;
    externalTrackReloadStatus_ = {}; externalTrackReloadStatus_.revision = revision;
    externalTrackReloadStatus_.phase = ExternalTrackReloadStatus::Phase::Reading;
    try {
        externalTrackReloadCancelled_ = std::make_shared<std::atomic_bool>(false);
        externalTrackReloadRequest_ = client_->ReadExternalTrackStateAsync(externalTrackReloadEntry_, externalTrackReloadMediaPath_);
    } catch (...) { externalTrackReloadRequest_ = 0; }
    externalTrackReloadStatus_.inFlight = externalTrackReloadRequest_ != 0;
    if (!externalTrackReloadRequest_) FailExternalTrackReload("Playback worker rejected the fresh track-state read");
    const bool accepted = externalTrackReloadRequest_ != 0;
    Notify(); return accepted;
}
std::optional<std::int64_t> PlayerSession::ResolveExternalReloadSelection(MpvEvent const& fresh) const {
    if (!externalTrackReloadOriginalId_) return 0;
    std::optional<std::int64_t> result;
    for (auto const& track : fresh.tracks) {
        if (track.type != externalTrackReloadType_) continue;
        const bool match = externalTrackReloadOriginalExternal_ ? track.external && track.externalPath == externalTrackReloadOriginalPath_ :
            !track.external && track.id == externalTrackReloadOriginalId_;
        if (!match) continue;
        if (result) return {};
        result = track.id;
    }
    return result;
}
void PlayerSession::SubmitExternalTrackReloadEdit(bool reload, std::int64_t id, std::string path,
    std::optional<std::int64_t> expectedCurrent) {
    externalTrackReloadStatus_.phase = reload ? ExternalTrackReloadStatus::Phase::Reloading : ExternalTrackReloadStatus::Phase::Restoring;
    externalTrackReloadRequestWasReload_ = reload;
    try {
        externalTrackReloadRequest_ = client_->EditExternalTrackAsync(externalTrackReloadType_, id, std::move(path),
            externalTrackReloadEntry_, externalTrackReloadMediaPath_, reload, expectedCurrent, externalTrackReloadCancelled_);
    } catch (...) { externalTrackReloadRequest_ = 0; }
    externalTrackReloadStatus_.inFlight = externalTrackReloadRequest_ != 0;
    if (!externalTrackReloadRequest_) FailExternalTrackReload("Playback worker rejected the track edit; previous changes cannot be rolled back");
}
bool PlayerSession::HandleExternalTrackReloadReply(MpvEvent const& event) {
    if (event.kind != MpvEvent::Kind::ExternalTrackStateReply && event.kind != MpvEvent::Kind::ExternalTrackEditReply) return false;
    if (!externalTrackReloadRequest_ || event.requestId != externalTrackReloadRequest_) return true;
    externalTrackReloadRequest_ = 0; externalTrackReloadStatus_.inFlight = false;
    const auto phase = externalTrackReloadStatus_.phase;
    // Keep operation identity separately: cancellation changes the visible phase.
    const bool editReply = event.kind == MpvEvent::Kind::ExternalTrackEditReply;
    const bool reloadReply = editReply && externalTrackReloadRequestWasReload_;
    if (reloadReply) {
        externalTrackReloadStatus_.reloadSubmitted = event.externalTrackEditSubmitted;
        externalTrackReloadStatus_.reloadAcknowledged = event.externalTrackEditSubmitted && event.externalTrackEditCommandSucceeded;
    }
    // These two facts describe this reply's readback, unlike the retained
    // historical command acknowledgement. A failed latest read is unknown.
    externalTrackReloadStatus_.targetPresentConfirmed = false;
    externalTrackReloadStatus_.selectionRestoredConfirmed = false;
    const auto mediaPath = event.properties.find("path");
    const bool fresh = event.externalTrackStateComplete && event.audioTrack && event.videoTrack &&
        event.entryId == externalTrackReloadEntry_ && mediaPath != event.properties.end() && mediaPath->second == externalTrackReloadMediaPath_;
    const auto selected = fresh ? (externalTrackReloadType_ == "audio" ? *event.audioTrack : *event.videoTrack) : -1;
    const auto pathCount = fresh ? std::count_if(event.tracks.begin(), event.tracks.end(), [&](auto const& track) {
        return track.type == externalTrackReloadType_ && track.external && track.externalPath == externalTrackReloadTargetPath_;
    }) : 0;
    if (fresh) {
        externalTrackReloadStatus_.targetPresentConfirmed = pathCount == 1;
        // Cancellation suppresses further mutation, not truthful evidence from
        // the already submitted operation. No default off-selection is inferred
        // before a baseline has actually been captured.
        if (externalTrackReloadBaselineCaptured_) {
            auto original = ResolveExternalReloadSelection(event);
            externalTrackReloadStatus_.selectionRestoredConfirmed = original && selected == *original;
        }
    }
    const bool cancelled = phase == ExternalTrackReloadStatus::Phase::Cancelled ||
        (externalTrackReloadCancelled_ && externalTrackReloadCancelled_->load()) ||
        snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        snapshot_.mediaGeneration != externalTrackReloadGeneration_ || snapshot_.mediaOpenIntentRevision != externalTrackReloadOpenIntent_ ||
        trackEditIntentRevision_ != externalTrackReloadEditIntent_ || snapshot_.entryId != externalTrackReloadEntry_ ||
        snapshot_.path != externalTrackReloadMediaPath_;
    if (cancelled) { externalTrackReloadStatus_.phase = ExternalTrackReloadStatus::Phase::Cancelled; Notify(); return true; }
    if (!fresh) {
        FailExternalTrackReload("Fresh complete media/track state is unavailable; submitted changes may already have taken effect");
        Notify(); return true;
    }
    if (reloadReply && !event.externalTrackEditSubmitted) {
        FailExternalTrackReload("Reload was not submitted; no selection-restoration command was issued");
        Notify(); return true;
    }
    if (phase == ExternalTrackReloadStatus::Phase::Reading) {
        const bool target = pathCount == 1 && std::any_of(event.tracks.begin(), event.tracks.end(), [&](auto const& track) {
            return track.type == externalTrackReloadType_ && track.external && track.id == externalTrackReloadTargetId_ &&
                track.externalPath == externalTrackReloadTargetPath_;
        });
        if (event.error < 0 || !target) {
            FailExternalTrackReload("The external track identity is missing or ambiguous; reload was not submitted"); Notify(); return true;
        }
        externalTrackReloadOriginalId_ = selected;
        if (selected) {
            auto original = std::find_if(event.tracks.begin(), event.tracks.end(), [&](auto const& track) {
                return track.type == externalTrackReloadType_ && track.id == selected;
            });
            if (original == event.tracks.end()) {
                FailExternalTrackReload("Original track selection is unavailable"); Notify(); return true;
            }
            externalTrackReloadOriginalExternal_ = original->external;
            externalTrackReloadOriginalPath_ = original->externalPath;
            if (!ResolveExternalReloadSelection(event)) {
                FailExternalTrackReload("Original selection has an ambiguous external path; reload was not submitted"); Notify(); return true;
            }
        }
        externalTrackReloadBaselineCaptured_ = true;
        SubmitExternalTrackReloadEdit(true, externalTrackReloadTargetId_, externalTrackReloadTargetPath_);
    } else {
        externalTrackReloadStatus_.targetPresentConfirmed = pathCount == 1;
        externalTrackReloadHadError_ = externalTrackReloadHadError_ || event.error < 0;
        auto target = ResolveExternalReloadSelection(event);
        if (!target) {
            FailExternalTrackReload("The original selected track is missing or ambiguous after reload; no replacement was guessed");
        } else if (selected == *target) {
            externalTrackReloadStatus_.selectionRestoredConfirmed = true;
            if (!externalTrackReloadStatus_.targetPresentConfirmed || externalTrackReloadHadError_ || !externalTrackReloadStatus_.reloadAcknowledged)
                FailExternalTrackReload("Original selection is confirmed, but reload reported an error or the target path was not uniquely confirmed");
            else externalTrackReloadStatus_.phase = ExternalTrackReloadStatus::Phase::Completed;
        } else if (phase == ExternalTrackReloadStatus::Phase::Reloading) {
            SubmitExternalTrackReloadEdit(false, *target, externalTrackReloadOriginalExternal_ ? externalTrackReloadOriginalPath_ : "", selected);
        } else {
            FailExternalTrackReload("Original track selection was not restored; no further command was submitted");
        }
    }
    Notify(); return true;
}
}

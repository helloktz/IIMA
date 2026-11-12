// IINA PlayerCore.swift subtitle operations; Copyright © 2016 lhc.
// GPL v3, references/iina/LICENSE. mpv DOCS/man/input.rst sub-reload/sub-seek.
#include "PlayerSession.h"
#include <algorithm>
#include "../Subtitles/CharacterEncoding.h"
#include <cmath>
namespace iima {
void PlayerSession::ApplySubtitleStyle(SubtitleStyle const& style, std::vector<std::string> fields) {
    if (snapshot_.session != SessionState::Ready) return;
    auto properties = style.Properties();
    if (!properties) {
        snapshot_.error = "Invalid subtitle style";
        Notify();
        return;
    }
    // IINA's individual setters leave unrelated style fields untouched. Validate
    // the complete requested subset before issuing any asynchronous commands.
    for (auto const& field : fields) {
        if (std::none_of(properties->begin(), properties->end(), [&](auto const& pair) { return pair.first == field; })) {
            snapshot_.error = "Unknown subtitle style field"; Notify(); return;
        }
    }
    for (auto const& [name, value] : *properties)
        if (fields.empty() || std::find(fields.begin(), fields.end(), name) != fields.end())
            client_->Command({"set", name, value});
}
void PlayerSession::SetSubtitleScale(double scale, bool primary) {
    if (snapshot_.session != SessionState::Ready || !std::isfinite(scale)) return;
    scale = std::abs(scale); // IINA accepts signed slider values as magnitudes.
    // mpv mp_subtitle_shared_sub_opts permits zero (invisible text), unlike
    // IINA's narrower slider. Primary UI may still constrain its own editor.
    if (scale > 100) return;
    client_->Command({"set", primary ? "sub-scale" : "secondary-sub-scale", std::to_string(scale)});
}
void PlayerSession::AdjustSubtitleScale(int direction) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || playlistEdit_) return;
    if (direction && !snapshot_.subtitleScale) {
        snapshot_.error = "Subtitle scale is unavailable"; Notify(); return;
    }
    auto scale = StepSubtitleScale(snapshot_.subtitleScale.value_or(1), direction);
    if (!scale) {
        snapshot_.error = "Subtitle scale step is outside the supported range; reset or enter a value";
        Notify(); return;
    }
    SetSubtitleScale(*scale);
}
void PlayerSession::SetSubtitlePosition(int percent, bool primary) {
    // Current mpv supports 0..150 for both slots. Values above 100 can put
    // text outside the visible image; the UI must explain that explicitly.
    if (snapshot_.session != SessionState::Ready || percent < 0 || percent > 150) return;
    client_->Command({"set", primary ? "sub-pos" : "secondary-sub-pos", std::to_string(percent)});
}
void PlayerSession::SetSecondarySubtitleDelay(double seconds) {
    if (snapshot_.session == SessionState::Ready && std::isfinite(seconds))
        client_->Command({"set", "secondary-sub-delay", std::to_string(seconds)});
}
void PlayerSession::SelectSecondarySubtitle(std::int64_t id) {
    BeginTrackEditIntent();
    CancelSubtitleReload();
    ++subtitleEditIntentRevision_;
    if (snapshot_.session != SessionState::Ready || id < 0) return;
    if (id && std::none_of(snapshot_.tracks.begin(), snapshot_.tracks.end(), [id](auto const& track) {
        return track.type == "sub" && track.id == id;
    })) return;
    client_->Command({"set", "secondary-sid", id ? std::to_string(id) : "no"});
}
void PlayerSession::SetSecondarySubtitlesVisible(bool visible) {
    if (snapshot_.session == SessionState::Ready)
        client_->Command({"set", "secondary-sub-visibility", visible ? "yes" : "no"});
}
void PlayerSession::ReloadSubtitles() { BeginSubtitleReload(std::nullopt); }
void PlayerSession::SeekSubtitle(int direction, bool primary) {
    CancelScrub();
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        !snapshot_.seekable || waitingForStart_ || playlistEdit_ || direction < -1 || direction > 1) return;
    auto id = primary ? snapshot_.subtitleId : snapshot_.secondarySubtitleId;
    if (!id || *id <= 0) return;
    client_->Command({"sub-seek", std::to_string(direction), primary ? "primary" : "secondary"});
}
// IINA's shipped input.conf Ctrl+Shift+LEFT/RIGHT uses sub-step, which
// changes subtitle delay rather than moving the audio/video playback position.
void PlayerSession::StepSubtitleTiming(int direction, bool primary) {
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || playlistEdit_ || direction < -1 || direction > 1) return;
    auto id = primary ? snapshot_.subtitleId : snapshot_.secondarySubtitleId;
    if (!id || *id <= 0) return;
    client_->Command({"sub-step", std::to_string(direction), primary ? "primary" : "secondary"});
    // Neither compute a synthetic delay nor seek/resume: observed delay is
    // authoritative and available events depend on mpv's subtitle prefetch.
}
// IINA PlayerCore.reloadAllSubs preserves the original subtitle by filename.
// Pinned mpv cmd_track_reload removes/re-adds tracks and switches the primary
// slot. Use fresh serialized state reads; never infer IDs from old observations.
bool PlayerSession::SetSubtitleEncoding(std::string encoding) {
    if (!IsSubtitleEncoding(encoding)) { BeginTrackEditIntent(); CancelSubtitleReload(); ++subtitleEditIntentRevision_; return false; }
    return BeginSubtitleReload(std::move(encoding));
}
bool PlayerSession::SubtitleReloadContextValid() const {
    return snapshot_.session == SessionState::Ready && snapshot_.media == MediaState::Loaded &&
        !waitingForStart_ && !playlistEdit_ && !snapshot_.mediaOriginPending &&
        snapshot_.mediaGeneration == subtitleReloadGeneration_ &&
        snapshot_.mediaOpenIntentRevision == subtitleReloadOpenIntent_ &&
        subtitleEditIntentRevision_ == subtitleReloadEditIntent_ &&
        snapshot_.entryId == subtitleReloadEntry_ && snapshot_.path == subtitleReloadMediaPath_;
}
void PlayerSession::CancelSubtitleReload() {
    auto& state = subtitleReloadStatus_;
    if (state.phase != SubtitleReloadStatus::Phase::Reading && state.phase != SubtitleReloadStatus::Phase::Encoding &&
        state.phase != SubtitleReloadStatus::Phase::Reloading && state.phase != SubtitleReloadStatus::Phase::Restoring) return;
    state.phase = SubtitleReloadStatus::Phase::Cancelled;
    // A submitted command may still complete. Keep its identity until drained,
    // and do not restore either slot after cancellation or a newer user intent.
    Notify();
}
void PlayerSession::FailSubtitleReload(std::string error) {
    subtitleReloadStatus_.phase = SubtitleReloadStatus::Phase::Failed;
    subtitleReloadStatus_.error = std::move(error);
}
void PlayerSession::ReadSubtitleReloadState(SubtitleReloadStep step) {
    subtitleReloadStep_ = step;
    subtitleReloadStatus_.phase = step == SubtitleReloadStep::SecondaryRead || step == SubtitleReloadStep::Verify ?
        SubtitleReloadStatus::Phase::Restoring : SubtitleReloadStatus::Phase::Reading;
    subtitleReloadRequest_ = client_->ReadSubtitleStateAsync();
    subtitleReloadStatus_.inFlight = subtitleReloadRequest_ != 0;
    if (!subtitleReloadRequest_) FailSubtitleReload("Playback worker rejected subtitle state read; remaining work was not submitted");
}
void PlayerSession::SubmitSubtitleReloadCommand(std::vector<std::string> command, SubtitleReloadStep step) {
    subtitleReloadStep_ = step;
    subtitleReloadStatus_.phase = step == SubtitleReloadStep::Encoding ? SubtitleReloadStatus::Phase::Encoding :
        step == SubtitleReloadStep::Reload ? SubtitleReloadStatus::Phase::Reloading : SubtitleReloadStatus::Phase::Restoring;
    subtitleReloadRequest_ = client_->Command(std::move(command));
    subtitleReloadStatus_.inFlight = subtitleReloadRequest_ != 0;
    if (!subtitleReloadRequest_) {
        FailSubtitleReload("Playback worker rejected subtitle command; remaining work was not submitted");
    }
}
bool PlayerSession::BeginSubtitleReload(std::optional<std::string> encoding) {
    BeginTrackEditIntent();
    if (ExternalTrackReloadBusy() || ExternalAVLoadBusy() || ExternalTrackRemovalBusy()) return false;
    if (SubtitleReloadBusy()) { CancelSubtitleReload(); ++subtitleEditIntentRevision_; return false; }
    if (snapshot_.session != SessionState::Ready || snapshot_.media != MediaState::Loaded ||
        waitingForStart_ || playlistEdit_ || snapshot_.mediaOriginPending || pauseRequest_ || loadRequest_ ||
        !pendingCommands_.empty() || snapshot_.entryId < 0 || snapshot_.path.empty()) return false;
    ++subtitleEditIntentRevision_;
    subtitleReloadEditIntent_ = subtitleEditIntentRevision_;
    subtitleReloadGeneration_ = snapshot_.mediaGeneration;
    subtitleReloadOpenIntent_ = snapshot_.mediaOpenIntentRevision;
    subtitleReloadEntry_ = snapshot_.entryId;
    subtitleReloadMediaPath_ = snapshot_.path;
    subtitleReloadEncoding_ = std::move(encoding);
    subtitleReloadTracks_.clear(); subtitleReloadIndex_ = 0;
    subtitleReloadPrimary_ = {}; subtitleReloadSecondary_ = {};
    subtitleReloadRestoreFailed_ = false;
    const auto revision = subtitleReloadStatus_.revision + 1;
    subtitleReloadStatus_ = {};
    subtitleReloadStatus_.revision = revision;
    ReadSubtitleReloadState(SubtitleReloadStep::Baseline);
    const bool accepted = subtitleReloadRequest_ != 0;
    Notify(); return accepted;
}
std::optional<std::int64_t> PlayerSession::ResolveReloadSelection(MpvEvent const& fresh, bool primary) const {
    auto const& selected = primary ? subtitleReloadPrimary_ : subtitleReloadSecondary_;
    if (!selected.id) return 0;
    std::optional<std::int64_t> result;
    for (auto const& track : fresh.tracks) {
        const bool matches = selected.external ? track.external && track.externalPath == selected.path :
            !track.external && track.id == selected.id;
        if (!matches) continue;
        if (result) return {}; // never choose between ambiguous filenames
        result = track.id;
    }
    return result;
}
void PlayerSession::RestoreSubtitleReloadSlot(MpvEvent const& fresh, bool primary) {
    const auto target = ResolveReloadSelection(fresh, primary);
    if (!target) {
        subtitleReloadRestoreFailed_ = true;
        subtitleReloadStatus_.error = "An original subtitle selection is missing or ambiguous; it was not restored";
        if (primary) RestoreSubtitleReloadSlot(fresh, false);
        else ReadSubtitleReloadState(SubtitleReloadStep::Verify);
        return;
    }
    const auto other = primary ? fresh.secondarySubtitle : fresh.primarySubtitle;
    if (*target && other == target) {
        // mp_switch_track_n rejects cross-slot duplicates. Normal reloads
        // vacate secondary before selecting primary. An unexpected collision
        // may be an external edit: do not forcibly clear that newer selection.
        FailSubtitleReload("Original subtitle target is selected in the other slot; restoration stopped");
        return;
    }
    const auto observed = primary ? fresh.primarySubtitle : fresh.secondarySubtitle;
    if (observed == target) {
        if (primary) RestoreSubtitleReloadSlot(fresh, false);
        else ReadSubtitleReloadState(SubtitleReloadStep::Verify);
        return;
    }
    SubmitSubtitleReloadCommand({"set", primary ? "sid" : "secondary-sid", *target ? std::to_string(*target) : "no"},
        primary ? SubtitleReloadStep::PrimarySet : SubtitleReloadStep::SecondarySet);
}
void PlayerSession::AdvanceSubtitleReload(MpvEvent const& fresh) {
    while (subtitleReloadIndex_ < subtitleReloadTracks_.size()) {
        auto const& [id, path] = subtitleReloadTracks_[subtitleReloadIndex_];
        const auto found = std::find_if(fresh.tracks.begin(), fresh.tracks.end(), [&](auto const& track) {
            return track.external && track.id == id && track.externalPath == path;
        });
        if (found == fresh.tracks.end()) {
            ++subtitleReloadIndex_; ++subtitleReloadStatus_.skipped; --subtitleReloadStatus_.remaining; continue;
        }
        SubmitSubtitleReloadCommand({"sub-reload", std::to_string(id)}, SubtitleReloadStep::Reload);
        return;
    }
    if (subtitleReloadStatus_.total) RestoreSubtitleReloadSlot(fresh, true);
    else subtitleReloadStatus_.phase = SubtitleReloadStatus::Phase::Completed;
}
bool PlayerSession::HandleSubtitleReloadReply(MpvEvent const& event) {
    if ((event.kind != MpvEvent::Kind::CommandReply && event.kind != MpvEvent::Kind::SubtitleStateReply) ||
        !subtitleReloadRequest_ || event.requestId != subtitleReloadRequest_) return false;
    subtitleReloadRequest_ = 0;
    subtitleReloadStatus_.inFlight = false;
    const bool isReload = event.kind == MpvEvent::Kind::CommandReply && subtitleReloadStep_ == SubtitleReloadStep::Reload;
    // Count only acknowledgements for submitted reloads, including a command
    // which was already executing when cancelled. No implication of rollback.
    if (isReload) {
        ++subtitleReloadIndex_; --subtitleReloadStatus_.remaining;
        if (event.error < 0) {
            ++subtitleReloadStatus_.failed;
            subtitleReloadStatus_.error = "mpv rejected a reload; that subtitle track may already have been removed";
        } else ++subtitleReloadStatus_.completed;
    }
    if (subtitleReloadStatus_.phase == SubtitleReloadStatus::Phase::Cancelled) { Notify(); return true; }
    if (!SubtitleReloadContextValid()) { CancelSubtitleReload(); return true; }
    if (event.kind == MpvEvent::Kind::SubtitleStateReply) {
        const auto path = event.properties.find("path");
        if (event.error < 0 || !event.subtitleStateComplete || !event.primarySubtitle || !event.secondarySubtitle ||
            event.entryId != subtitleReloadEntry_ || path == event.properties.end() || path->second != subtitleReloadMediaPath_) {
            FailSubtitleReload("Fresh subtitle state is unavailable, incomplete, or belongs to different media");
            Notify(); return true;
        }
        if (subtitleReloadStep_ == SubtitleReloadStep::Baseline) {
            std::size_t bytes{};
            for (auto const& track : event.tracks) {
                if (!track.external) continue;
                if (track.id <= 0 || track.externalPath.empty() || track.externalPath.size() > 131072 ||
                    track.externalPath.find('\0') != std::string::npos || subtitleReloadTracks_.size() >= 64 ||
                    bytes + track.externalPath.size() > 1024 * 1024) {
                    FailSubtitleReload("External subtitle identities exceed the reload budget"); Notify(); return true;
                }
                bytes += track.externalPath.size();
                subtitleReloadTracks_.emplace_back(track.id, track.externalPath);
            }
            auto capture = [&](std::int64_t id, ReloadSelection& target) {
                target.id = id;
                if (!id) return;
                auto found = std::find_if(event.tracks.begin(), event.tracks.end(), [&](auto const& track) { return track.id == id; });
                if (found != event.tracks.end()) { target.external = found->external; target.path = found->externalPath; }
            };
            capture(*event.primarySubtitle, subtitleReloadPrimary_);
            capture(*event.secondarySubtitle, subtitleReloadSecondary_);
            subtitleReloadStatus_.total = subtitleReloadStatus_.remaining = subtitleReloadTracks_.size();
            if (subtitleReloadEncoding_) {
                auto encoding = std::move(*subtitleReloadEncoding_); subtitleReloadEncoding_.reset();
                SubmitSubtitleReloadCommand({"set", "sub-codepage", std::move(encoding)}, SubtitleReloadStep::Encoding);
            } else AdvanceSubtitleReload(event);
        } else if (subtitleReloadStep_ == SubtitleReloadStep::SecondaryRead) {
            RestoreSubtitleReloadSlot(event, false);
        } else if (subtitleReloadStep_ == SubtitleReloadStep::Verify) {
            const auto primary = ResolveReloadSelection(event, true), secondary = ResolveReloadSelection(event, false);
            if (subtitleReloadRestoreFailed_ || !primary || !secondary || event.primarySubtitle != primary || event.secondarySubtitle != secondary)
                FailSubtitleReload("Reload commands finished, but original subtitle selections could not both be confirmed");
            else subtitleReloadStatus_.phase = SubtitleReloadStatus::Phase::Completed;
        } else AdvanceSubtitleReload(event);
    } else if (subtitleReloadStep_ == SubtitleReloadStep::Encoding) {
        if (event.error < 0) FailSubtitleReload("mpv rejected subtitle encoding change; no reload was submitted");
        else ReadSubtitleReloadState(SubtitleReloadStep::Refresh);
    } else if (subtitleReloadStep_ == SubtitleReloadStep::Reload) {
        ReadSubtitleReloadState(SubtitleReloadStep::Refresh);
    } else {
        if (event.error < 0) {
            subtitleReloadRestoreFailed_ = true;
            subtitleReloadStatus_.error = "mpv rejected restoration of an original subtitle selection";
        }
        ReadSubtitleReloadState(subtitleReloadStep_ == SubtitleReloadStep::PrimarySet ?
            SubtitleReloadStep::SecondaryRead : SubtitleReloadStep::Verify);
    }
    Notify(); return true;
}

}

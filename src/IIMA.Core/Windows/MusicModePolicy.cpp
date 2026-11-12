// Source: references/iina-source.txt; see header for original symbols/license.
#include "MusicModePolicy.h"

namespace iima {
MusicTrackSummary ClassifyMusicTracks(std::span<MediaTrack const> tracks,
                                     bool tracksKnown, bool network) noexcept
{
    MusicTrackSummary result;
    for (auto const& track : tracks) {
        if (track.type == "audio") result.hasAudio = true;
        if (track.type == "video") {
            if (track.albumArt) result.hasAlbumArt = true;
            else result.hasRealVideo = true;
        }
    }
    // Preserve PlaybackInfo.isAudio's network-first branch. In particular, an
    // audio-only stream is not automatically switched into music mode upstream.
    if (network) result.kind = MusicMediaKind::Video;
    else if (!tracksKnown || (!result.hasAudio && !result.hasRealVideo && !result.hasAlbumArt))
        result.kind = MusicMediaKind::Unknown;
    else result.kind = result.hasRealVideo ? MusicMediaKind::Video : MusicMediaKind::Audio;
    return result;
}
bool AudioOnlyPlaybackEligible(std::span<MediaTrack const> tracks, bool tracksKnown) noexcept
{
    if (!tracksKnown) return false;
    const auto summary = ClassifyMusicTracks(tracks, true, false);
    return summary.hasAudio && !summary.hasRealVideo;
}
void MusicModePolicy::CancelPending() noexcept
{
    pending_.reset();
    // Equality is always checked together with the currently held pending ticket;
    // wrapping this unsigned revision does not make an absent ticket acceptable.
    ++revision_;
}
std::optional<MusicModePolicy::Transition> MusicModePolicy::Propose(bool music, Cause cause)
{
    if (!generation_ || music == music_) return std::nullopt;
    pending_ = Transition{music, cause, *generation_, revision_};
    return pending_;
}
std::optional<MusicModePolicy::Transition> MusicModePolicy::Update(MediaContext const& context)
{
    CancelPending();
    if (!generation_ || *generation_ != context.generation) {
        generation_ = context.generation;
        // Generation changes invalidate tickets, not the player's manual choice.
        // IINA keeps overrideAutoSwitchToMusicMode until a second manual change
        // or MiniPlayerWindowController.windowWillClose resets it.
    }
    summary_ = ClassifyMusicTracks(context.tracks, context.tracksKnown, context.network);
    if (!context.active || !context.automatic || override_) return std::nullopt;
    if (summary_.kind == MusicMediaKind::Audio && !music_ && !context.fullscreen)
        return Propose(true, Cause::Automatic);
    if (summary_.kind == MusicMediaKind::Video && music_)
        return Propose(false, Cause::Automatic);
    return std::nullopt;
}
std::optional<MusicModePolicy::Transition> MusicModePolicy::Manual(bool music)
{
    CancelPending();
    return Propose(music, Cause::Manual);
}
bool MusicModePolicy::Commit(Transition const& transition) noexcept
{
    if (!pending_ || !generation_ || transition.generation != *generation_ ||
        transition.revision != revision_ || transition.revision != pending_->revision ||
        transition.music != pending_->music || transition.cause != pending_->cause ||
        transition.generation != pending_->generation) return false;
    music_ = transition.music;
    if (transition.cause == Cause::Manual) override_ = !override_;
    pending_.reset();
    return true;
}
void MusicModePolicy::Close() noexcept
{
    CancelPending();
    generation_.reset();
    summary_ = {};
    music_ = false;
    override_ = false;
}
}

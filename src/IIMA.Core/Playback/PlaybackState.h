#pragma once
#include "../Mpv/MpvClient.h"
#include "PlaybackSettings.h"
#include <map>

namespace iima {
enum class SessionState { Initializing, Ready, Closing, Closed, Failed };
enum class MediaState { Empty, Loading, Loaded, Ended, Error };
struct PlaybackSnapshot {
    SessionState session{SessionState::Initializing};
    MediaState media{MediaState::Empty};
    bool paused{true}, seeking{}, buffering{};
    std::optional<double> position, duration, abLoopA, abLoopB;
    std::string abLoopCount{"inf"};
    double volume{100}, speed{1}, maxVolume{100}, audioDelay{}, subtitleDelay{};
    bool muted{}, subtitlesVisible{true};
    // IINA MPVController/PlaybackInfo track the two subtitle slots separately.
    // track-list.selected is not a primary-slot identifier. Preserve mpv's
    // -2=no and -1=auto values; null means no valid observation, never "no".
    std::optional<std::int64_t> subtitleId, secondarySubtitleId;
    std::optional<std::int64_t> videoId, audioId; // Same no/auto/unknown rules.
    std::optional<bool> subtitleVisibility, secondarySubtitlesVisible;
    std::optional<double> observedSubtitleDelay, secondarySubtitleDelay;
    std::optional<double> subtitlePosition, secondarySubtitlePosition;
    std::optional<double> subtitleScale, secondarySubtitleScale;
    std::map<std::string, std::string> subtitleStyleValues; // Fixed observed property names; missing means unknown.
    // Observed mpv option values, not optimistic command echoes. Empty means
    // unavailable/invalid, not neutral zero. Options can persist across files.
    std::optional<int> videoBrightness, videoContrast, videoSaturation, videoGamma, videoHue;
    std::optional<bool> deinterlace;
    // Current mpv also supports auto, which cannot be faithfully represented
    // by IINA's older Boolean switch. Keep it visible rather than reporting off.
    std::optional<std::string> deinterlaceMode;
    std::optional<std::string> hardwareDecodeMode; // Requested hwdec option, not actual decoder.
    RepeatMode repeatMode{RepeatMode::Off};
    std::string loopFile{"no"}, loopPlaylist{"no"}, keepOpen{"yes"};
    std::int64_t entryId{-1};
    std::string path, error;
    bool seekable{};
    std::string mediaTitle, metadataTitle, metadataAlbum, metadataArtist;
    MediaProperties metadata, videoParameters, audioParameters;
    std::optional<double> videoBitrate, audioBitrate, fileSize;
    std::vector<AudioDevice> audioDevices;
    std::string audioDevice{"auto"};
    std::uint64_t informationRevision{}, audioDevicesRevision{};
    bool compositionAvailable{}, voConfigured{};
    std::uint64_t clientApiVersion{};
    std::string mpvVersion, videoCodec, hwdecCurrent, currentVo;
    std::shared_ptr<void> swapChain;
    // Revisions let high-frequency position updates skip O(n) UI comparisons.
    std::uint64_t playlistRevision{}, tracksRevision{}, chaptersRevision{};
    FilterChainSnapshot videoFilters, audioFilters;
    std::vector<PlaylistItem> playlist;
    std::vector<MediaTrack> tracks;
    std::vector<Chapter> chapters;
    std::optional<std::int64_t> chapterIndex;
};
// Ordered event reduction. Command acceptance never asserts successful playback.
void ApplyPlaybackEvent(PlaybackSnapshot& state, MpvEvent const& event);
}

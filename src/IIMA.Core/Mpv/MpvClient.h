#pragma once
#include <cstdint>
#include <atomic>
#include "../Diagnostics/MpvLogBuffer.h"
#include <cstddef>
#include "../Media/MediaInformation.h"
#include "../Filters/FilterChain.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iima {
// InspectorWindowController watch table, adapted from IINA (GPL-3.0).
// Results are owned strings; no borrowed mpv payload crosses the worker boundary.
inline constexpr std::size_t MaxWatchedProperties = 32;
inline constexpr std::size_t MaxWatchedPropertyNameBytes = 128;
inline constexpr std::size_t MaxWatchedPropertyValueBytes = 4096;
inline bool ValidateWatchedPropertyName(std::string const& name) noexcept {
    if (name.empty() || name.size() > MaxWatchedPropertyNameBytes) return false;
    for (unsigned char c : name) if (c < 33 || c > 126) return false;
    return true;
}
struct WatchedPropertyValue {
    std::string name, value, error;
    bool truncated{};
};
struct PlaylistItem {
    std::int64_t id{};
    std::string path, title;
    bool current{};
    bool operator==(PlaylistItem const&) const = default;
};
struct MediaTrack {
    std::int64_t id{};
    std::string type, title, lang, externalPath;
    bool selected{}, external{};
    std::optional<std::int64_t> sourceId, width, height, channelCount, sampleRate;
    std::optional<std::string> codec, decoderDescription, channelLayout;
    std::optional<double> framesPerSecond;
    bool isDefault{}, forced{}, image{}, albumArt{};
    bool operator==(MediaTrack const&) const = default;
};
struct Chapter {
    int index{};
    double time{};
    std::string title;
    bool operator==(Chapter const&) const = default;
};
// Owns all payloads; never exports mpv's borrowed event memory.
struct MpvEvent {
    enum class Kind { Ready, Failure, StartFile, Loaded, EndFile, Seek, Restart, SeekMeasurement,
                      Property, CommandReply, PlaylistReply, ExternalTrackStateReply, ExternalTrackEditReply, ExternalAVLoadReply, ExternalTrackRemovalReply, SubtitleStateReply, PauseReply, VideoGeometryReply, WindowScaleReply, WatchedPropertiesReply, VideoReconfig, Shutdown } kind;
    std::string name, text;
    std::optional<double> number;
    std::optional<bool> flag;
    std::int64_t entryId{-1};
    std::int64_t insertedEntryId{-1}; // First playlist entry created by END_FILE redirect.
    std::uint64_t requestId{};
    int error{}, reason{};
    std::optional<std::int64_t> integer;
    std::vector<PlaylistItem> playlist;
    std::vector<MediaTrack> tracks;
    // Fresh bounded subtitle transaction read, not ordinary observations.
    bool subtitleStateComplete{};
    bool externalTrackStateComplete{}, externalTrackEditSubmitted{}, externalTrackEditCommandSucceeded{};
    std::optional<std::int64_t> audioTrack, videoTrack; // Fresh AV slots; 0 means off.
    bool externalTrackRemovalSubmitted{}, externalTrackRemovedConfirmed{};
    bool externalAVLoadSubmitted{}, externalAVSelectedConfirmed{}, externalAVLoadCommandSucceeded{};
    std::optional<std::int64_t> primarySubtitle, secondarySubtitle; // 0 means no selection.
    std::vector<Chapter> chapters;
    FilterChainSnapshot filters;
    MediaProperties properties;
    std::vector<WatchedPropertyValue> watchedProperties;
    std::vector<AudioDevice> audioDevices;
    bool audioDevicesComplete{}; // Full valid array, never a truncated/partial parse.
    // Exact IDXGISwapChain interface, owning one reference; never a borrowed address.
    std::shared_ptr<void> swapChain;
};
class MpvClient final {
public:
    using Sink = std::function<void(MpvEvent)>;
    explicit MpvClient(Sink sink);
    ~MpvClient();
    MpvClient(MpvClient const&) = delete;
    MpvClient& operator=(MpvClient const&) = delete;
    // DLL must be absolute and supplied from the application deployment directory.
    void Start(std::wstring dllPath);
    void SetPauseWhenOpen(bool value);
    bool StartMpvLogCapture(std::string level);
    void StopMpvLogCapture();
    void ClearMpvLogCapture();
    MpvLogSnapshot SnapshotMpvLogs() const;
    std::uint64_t Command(std::vector<std::string> arguments, bool measureSeek = false);
    // Queued in the SAME serialized worker lane as commands. PlaylistReply
    // owns a fresh node copy; ordinary observed properties are not substitutes.
    std::uint64_t GetPlaylistAsync();
    // Serialized fresh reads with media identity before/after. Not an atomic
    // snapshot or compare-and-set against other mpv clients/natural transitions.
    std::uint64_t ReadSubtitleStateAsync();
    // Complete bounded AV identities/slots. Multiple GETs are not an atomic CAS.
    std::uint64_t ReadExternalTrackStateAsync(std::int64_t expectedEntry, std::string mediaPath);
    // reload: exact external target. Restore: empty externalPath means embedded,
    // id 0 means off. Restore requires the fresh current slot to match its guard.
    std::uint64_t EditExternalTrackAsync(std::string type, std::int64_t id, std::string externalPath,
        std::int64_t expectedEntry, std::string mediaPath, bool reload,
        std::optional<std::int64_t> expectedCurrentId, std::shared_ptr<std::atomic_bool> cancelled = {});
    // Fresh identity check, one typed remove, then complete fresh confirmation.
    // Cancellation prevents queued submission, not an already executing command.
    // Local checks are not atomic with external mpv edits or natural transitions.
    std::uint64_t RemoveExternalTrackAsync(std::string type, std::int64_t id,
        std::string externalPath, std::int64_t expectedEntry, std::string mediaPath,
        std::shared_ptr<std::atomic_bool> cancelled = {});
    // audio/video-add select with fresh media checks and actual aid/vid readback.
    // Confirmation is track selection, not output binding or frame presentation.
    std::uint64_t LoadExternalAVAsync(std::string type, std::string path, std::int64_t expectedEntry,
        std::string mediaPath, std::shared_ptr<std::atomic_bool> cancelled = {});
    // Genuine async GET reply through mpv's event queue, not cached observation.
    std::uint64_t GetVideoGeometryAsync();
    std::uint64_t GetWindowScaleAsync();
    std::uint64_t GetPauseAsync(); // Genuine event-queue FLAG reply for scrub ownership.
    // Serialized compare-before-set: recover only this still-missing selection.
    std::uint64_t RecoverAudioDeviceAsync(std::string expectedDevice);
    // Read-only, serialized with commands. Does not install persistent observers.
    // Each property may fail independently. 0 means rejected before enqueue.
    std::uint64_t ReadWatchedPropertiesAsync(std::vector<std::string> names);
    // Fresh read/compare and typed node write in the serialized worker.
    std::uint64_t EditFilterChainAsync(bool audio, FilterEditPlan plan,
        std::optional<std::int64_t> expectedEntry = {}, std::string expectedPath = {});
    // Nonblocking; completion runs on worker AFTER terminate_destroy and DLL release.
    void CloseAsync(std::function<void()> completion = {});
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

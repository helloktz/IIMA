#pragma once
#include <cstdint>
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
                      Property, CommandReply, PlaylistReply, WatchedPropertiesReply, VideoReconfig, Shutdown } kind;
    std::string name, text;
    std::optional<double> number;
    std::optional<bool> flag;
    std::int64_t entryId{-1};
    std::uint64_t requestId{};
    int error{}, reason{};
    std::optional<std::int64_t> integer;
    std::vector<PlaylistItem> playlist;
    std::vector<MediaTrack> tracks;
    std::vector<Chapter> chapters;
    FilterChainSnapshot filters;
    MediaProperties properties;
    std::vector<WatchedPropertyValue> watchedProperties;
    std::vector<AudioDevice> audioDevices;
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
    std::uint64_t Command(std::vector<std::string> arguments, bool measureSeek = false);
    // Queued in the SAME serialized worker lane as commands. PlaylistReply
    // owns a fresh node copy; ordinary observed properties are not substitutes.
    std::uint64_t GetPlaylistAsync();
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

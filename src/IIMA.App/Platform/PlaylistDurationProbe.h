// Windows adaptation of IINA PlayerCore.refreshCachedVideoInfo / FFmpegController.
// IINA business rules copyright (c) 2017 lhc and contributors, GPL-3.0.
// See references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>

namespace iima {
struct PlaylistDurationFingerprint {
    std::uint32_t volumeSerial{};
    std::uint64_t fileIndex{}, size{}, lastWriteTime{};
    bool operator==(PlaylistDurationFingerprint const&) const = default;
};
enum class PlaylistDurationProbeStatus { Ready, Cancelled, Unsupported, Changed, Failed };
struct PlaylistDurationProbeResult {
    PlaylistDurationProbeStatus status{PlaylistDurationProbeStatus::Failed};
    std::optional<double> seconds;
    // Display-only UTF-8, <=1024 bytes each, sanitized to a single line.
    // Missing service/property/type is unknown, never a filename fallback.
    std::optional<std::string> title, artist;
    std::optional<PlaylistDurationFingerprint> fingerprint;
};
// Background MTA worker only. No player state, UI, sample decoding, or URL
// resolver. Absolute fixed/removable files only; excludes UNC, ADS, reparse and
// offline paths. At most 32767 UTF-16 units and 1 TiB, common single-file media
// extensions only; shared stream clones have a cumulative 64 MiB read budget.
// Ready means at least one of duration/title/artist is available and the file
// fingerprint is stable; metadata-only success has no seconds. Metadata is
// queried only from the existing source MF_PROPERTY_HANDLER_SERVICE, never
// from a Shell path handler or URL. Only title/artist, no artwork enumeration.
// Unsupported MF containers/codecs or unknown duration remain
// unknown, never zero. Fingerprint is a point-in-time same-HANDLE check, NOT a
// persistent cache validity guarantee or file watcher. Callers invalidate their
// bounded cache on queue/input changes and offer explicit refresh.
// Cooperative cancellation between native calls cannot interrupt a blocking
// installed handler or guarantee that handler performs no internal I/O. Never
// join from UI/close; discard late results using the caller's lifetime/epoch.
PlaylistDurationProbeResult ProbePlaylistDuration(std::filesystem::path const& path,
    std::stop_token stop = {});
}

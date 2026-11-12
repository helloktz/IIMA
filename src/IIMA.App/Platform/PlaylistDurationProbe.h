// Windows adaptation of IINA PlayerCore.refreshCachedVideoInfo / FFmpegController.
// IINA business rules copyright (c) 2017 lhc and contributors, GPL-3.0.
// See references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>

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
    std::optional<PlaylistDurationFingerprint> fingerprint;
};
// Background MTA worker only. No player state, UI, sample decoding, or URL
// resolver. Absolute fixed/removable files only; excludes UNC, ADS, reparse and
// offline paths. At most 32767 UTF-16 units and 1 TiB, common single-file media
// extensions only; shared stream clones have a cumulative 64 MiB read budget.
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

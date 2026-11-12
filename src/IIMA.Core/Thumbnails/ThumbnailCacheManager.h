// Adapted from IINA CacheManager.swift, copyright (c) 2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace iima {
struct ThumbnailCacheFile {
    // A single directory entry, not an absolute or relative multi-part path.
    std::string name;
    std::uint64_t size{};
    std::optional<std::int64_t> accessedUnixSeconds;
    bool regularFile{true}, hidden{}, symbolicLink{};
};
struct ThumbnailEvictionResult {
    bool alreadyRunning{};
    std::uint64_t reclaimedBytes{};
    std::vector<std::string> deleted;
    std::vector<std::string> failed;
};
class ThumbnailCacheManager final {
public:
    // Storage layer enumerates only its dedicated thumbnail directory and
    // supplies access times if available. Windows access times may be disabled;
    // RecordRead provides application-driven LRU ordering in that case.
    void Refresh(std::vector<ThumbnailCacheFile> files);
    void Invalidate();
    bool NeedsRefresh() const;
    std::uint64_t Size() const;
    bool ShouldWrite(std::uint64_t maximumBytes) const noexcept;
    bool NeedsEviction(std::uint64_t maximumBytes) const;
    void RecordRead(std::string const& name, std::int64_t accessedUnixSeconds);
    void RecordWrite(ThumbnailCacheFile file);
    void RecordDelete(std::string const& name);
    std::vector<ThumbnailCacheFile> OldestFirst() const;
    // Matches IINA's target: reclaim half the configured maximum, not half
    // current usage. Run before writing when current size > maximum. Actual
    // deletion is delegated to the directory-owning storage layer. Failures
    // do not count as reclaimed bytes, and the job guard is always released.
    ThumbnailEvictionResult ClearOldCache(std::uint64_t maximumBytes,
        std::function<bool(ThumbnailCacheFile const&)> const& removeFile);
    static std::optional<std::uint64_t> BytesFromMebibytes(std::int64_t mebibytes) noexcept;
    static bool IsSafeCacheName(std::string const& name) noexcept;
private:
    static bool Eligible(ThumbnailCacheFile const& file) noexcept;
    std::uint64_t SizeLocked() const noexcept;
    std::vector<ThumbnailCacheFile> OldestFirstLocked() const;
    mutable std::mutex mutex_;
    std::map<std::string, ThumbnailCacheFile, std::less<>> files_;
    bool needsRefresh_{true};
    bool jobRunning_{};
};
}

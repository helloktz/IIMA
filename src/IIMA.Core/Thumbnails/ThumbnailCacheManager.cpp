// Adapted from IINA CacheManager.swift, copyright (c) 2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#include "ThumbnailCacheManager.h"
#include <algorithm>
#include <limits>
#include <utility>
#include <string_view>

namespace iima {
namespace {
std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) noexcept {
    return right > std::numeric_limits<std::uint64_t>::max() - left ?
        std::numeric_limits<std::uint64_t>::max() : left + right;
}
}
bool ThumbnailCacheManager::IsSafeCacheName(std::string const& name) noexcept {
    if (name.empty() || name == "." || name == ".." || name.front() == '.' ||
        name.back() == '.' || name.back() == ' ') return false;
    // Reject path separators, alternate data streams and Win32 wildcard/device
    // spelling. Cache writers should use digest filenames, not media titles.
    for (auto c : name) {
        if (static_cast<unsigned char>(c) < 32 || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
    }
    auto end = name.find('.');
    auto base = std::string_view(name).substr(0, end);
    auto equalAscii = [](std::string_view left, std::string_view right) {
        if (left.size() != right.size()) return false;
        for (std::size_t i = 0; i < left.size(); ++i) {
            auto c = left[i];
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (c != right[i]) return false;
        }
        return true;
    };
    if (equalAscii(base, "CON") || equalAscii(base, "PRN") || equalAscii(base, "AUX") || equalAscii(base, "NUL")) return false;
    if (base.size() == 4 && (equalAscii(base.substr(0, 3), "COM") || equalAscii(base.substr(0, 3), "LPT")) &&
        base.back() >= '1' && base.back() <= '9') return false;
    return true;
}
bool ThumbnailCacheManager::Eligible(ThumbnailCacheFile const& file) noexcept {
    return file.regularFile && !file.hidden && !file.symbolicLink && IsSafeCacheName(file.name);
}
void ThumbnailCacheManager::Refresh(std::vector<ThumbnailCacheFile> files) {
    std::map<std::string, ThumbnailCacheFile, std::less<>> refreshed;
    for (auto& file : files) if (Eligible(file)) {
        auto name = file.name;
        refreshed.insert_or_assign(std::move(name), std::move(file));
    }
    std::lock_guard lock(mutex_);
    files_ = std::move(refreshed);
    needsRefresh_ = false;
}
void ThumbnailCacheManager::Invalidate() {
    std::lock_guard lock(mutex_);
    needsRefresh_ = true;
}
bool ThumbnailCacheManager::NeedsRefresh() const {
    std::lock_guard lock(mutex_);
    return needsRefresh_;
}
std::uint64_t ThumbnailCacheManager::SizeLocked() const noexcept {
    std::uint64_t total = 0;
    for (auto const& [name, file] : files_) total = SaturatingAdd(total, file.size);
    return total;
}
std::uint64_t ThumbnailCacheManager::Size() const {
    std::lock_guard lock(mutex_);
    return SizeLocked();
}
bool ThumbnailCacheManager::ShouldWrite(std::uint64_t maximumBytes) const noexcept { return maximumBytes != 0; }
bool ThumbnailCacheManager::NeedsEviction(std::uint64_t maximumBytes) const {
    std::lock_guard lock(mutex_);
    return maximumBytes != 0 && SizeLocked() > maximumBytes;
}
void ThumbnailCacheManager::RecordRead(std::string const& name, std::int64_t accessedUnixSeconds) {
    std::lock_guard lock(mutex_);
    if (auto entry = files_.find(name); entry != files_.end()) entry->second.accessedUnixSeconds = accessedUnixSeconds;
}
void ThumbnailCacheManager::RecordWrite(ThumbnailCacheFile file) {
    if (!Eligible(file)) return;
    std::lock_guard lock(mutex_);
    auto name = file.name;
    files_.insert_or_assign(std::move(name), std::move(file));
}
void ThumbnailCacheManager::RecordDelete(std::string const& name) {
    std::lock_guard lock(mutex_);
    files_.erase(name);
}
std::vector<ThumbnailCacheFile> ThumbnailCacheManager::OldestFirstLocked() const {
    std::vector<ThumbnailCacheFile> files;
    files.reserve(files_.size());
    for (auto const& [name, file] : files_) files.push_back(file);
    std::stable_sort(files.begin(), files.end(), [](auto const& left, auto const& right) {
        auto a = left.accessedUnixSeconds.value_or(std::numeric_limits<std::int64_t>::min());
        auto b = right.accessedUnixSeconds.value_or(std::numeric_limits<std::int64_t>::min());
        return a != b ? a < b : left.name < right.name;
    });
    return files;
}
std::vector<ThumbnailCacheFile> ThumbnailCacheManager::OldestFirst() const {
    std::lock_guard lock(mutex_);
    return OldestFirstLocked();
}
ThumbnailEvictionResult ThumbnailCacheManager::ClearOldCache(std::uint64_t maximumBytes,
    std::function<bool(ThumbnailCacheFile const&)> const& removeFile) {
    ThumbnailEvictionResult result;
    if (!removeFile || maximumBytes == 0) return result;
    std::vector<ThumbnailCacheFile> snapshot;
    {
        std::lock_guard lock(mutex_);
        if (jobRunning_) { result.alreadyRunning = true; return result; }
        // Allocate before setting the flag so allocation failure cannot wedge it.
        snapshot = OldestFirstLocked();
        jobRunning_ = true;
    }
    struct JobGuard {
        ThumbnailCacheManager& owner;
        ~JobGuard() {
            std::lock_guard lock(owner.mutex_);
            owner.jobRunning_ = false;
        }
    } guard{*this};
    auto target = maximumBytes / 2;
    for (auto const& file : snapshot) {
        if (result.reclaimedBytes >= target) break;
        bool removed = false;
        try { removed = removeFile(file); } catch (...) { removed = false; }
        if (removed) {
            result.deleted.push_back(file.name);
            result.reclaimedBytes = SaturatingAdd(result.reclaimedBytes, file.size);
            std::lock_guard lock(mutex_);
            // Preserve a newer record if a writer replaced this name during
            // eviction. The storage callback must itself guard replacement races.
            auto current = files_.find(file.name);
            if (current != files_.end() && current->second.size == file.size &&
                current->second.accessedUnixSeconds == file.accessedUnixSeconds) files_.erase(current);
            else needsRefresh_ = true;
        } else {
            result.failed.push_back(file.name);
            Invalidate();
        }
    }
    return result;
}
std::optional<std::uint64_t> ThumbnailCacheManager::BytesFromMebibytes(std::int64_t mebibytes) noexcept {
    constexpr std::uint64_t factor = 1024 * 1024;
    if (mebibytes < 0 || static_cast<std::uint64_t>(mebibytes) > std::numeric_limits<std::uint64_t>::max() / factor)
        return std::nullopt;
    return static_cast<std::uint64_t>(mebibytes) * factor;
}
}

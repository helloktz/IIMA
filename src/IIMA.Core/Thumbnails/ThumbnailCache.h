// Adapted from IINA ThumbnailCache.swift, copyright (c) 2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace iima {
struct ThumbnailSourceIdentity {
    std::uint64_t size{};
    std::int64_t modifiedUnixSeconds{};
    bool operator==(ThumbnailSourceIdentity const&) const = default;
};
struct EncodedThumbnail {
    double seconds{};
    std::vector<std::uint8_t> jpeg;
};
struct ThumbnailCacheLimits {
    std::size_t maximumFileBytes{512 * 1024 * 1024};
    std::size_t maximumImageBytes{16 * 1024 * 1024};
    std::size_t maximumThumbnails{10000};
};
struct ThumbnailCacheEntry {
    double seconds{};
    std::size_t jpegOffset{}, jpegLength{};
};
struct ThumbnailCacheIndex {
    ThumbnailSourceIdentity source;
    std::vector<ThumbnailCacheEntry> entries;
    // Last thumbnail at or before the requested time. Returns no selection for
    // empty indexes/invalid times; clamps a time before the first to index 0.
    std::optional<std::size_t> AtOrBefore(double seconds) const noexcept;
};
struct ThumbnailCacheIndexResult {
    std::optional<ThumbnailCacheIndex> index;
    std::string error;
};
struct ThumbnailCacheEncodeResult {
    std::vector<std::uint8_t> bytes;
    std::string error;
    explicit operator bool() const noexcept { return error.empty(); }
};

// Pinned IINA format version 2, exactly 17 metadata bytes. On its supported
// Intel/Apple-Silicon hosts native numbers are little endian; encode that
// explicitly for Windows x64. Records: signed int64 block length, binary64
// timestamp, JPEG bytes. Length includes timestamp, not its own eight bytes.
inline constexpr std::uint8_t ThumbnailCacheVersion = 2;
inline constexpr std::size_t ThumbnailCacheMetadataBytes = 17;
ThumbnailCacheIndexResult IndexThumbnailCache(std::span<std::uint8_t const> bytes,
    std::optional<ThumbnailSourceIdentity> expectedSource = std::nullopt,
    ThumbnailCacheLimits limits = {});
ThumbnailCacheEncodeResult EncodeThumbnailCache(ThumbnailSourceIdentity source,
    std::span<EncodedThumbnail const> thumbnails, ThumbnailCacheLimits limits = {});
// Returned spans borrow the supplied file bytes; caller owns their lifetime.
std::optional<std::span<std::uint8_t const>> ThumbnailImageBytes(
    std::span<std::uint8_t const> bytes, ThumbnailCacheEntry const& entry) noexcept;
std::optional<ThumbnailSourceIdentity> ReadThumbnailSourceIdentity(
    std::filesystem::path const& mediaPath, std::string& error);
ThumbnailCacheIndexResult ReadThumbnailCacheFile(std::filesystem::path const& path,
    std::vector<std::uint8_t>& bytes, std::optional<ThumbnailSourceIdentity> expectedSource = std::nullopt,
    ThumbnailCacheLimits limits = {});
// Image decompression/validation and JPEG quality 0.75 encoding belong to the
// Windows image provider. Marker checking here is not full JPEG validation.
}

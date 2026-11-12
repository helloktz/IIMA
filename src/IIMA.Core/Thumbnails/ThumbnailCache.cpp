// Adapted from IINA ThumbnailCache.swift, copyright (c) 2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#include "ThumbnailCache.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>

namespace iima {
namespace {
std::uint64_t Read64(std::span<std::uint8_t const> bytes, std::size_t position) {
    std::uint64_t result{};
    for (unsigned i = 0; i < 8; ++i) result |= static_cast<std::uint64_t>(bytes[position + i]) << (8 * i);
    return result;
}
void Write64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
bool JpegMarkers(std::span<std::uint8_t const> bytes) {
    return bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8 &&
        bytes[bytes.size() - 2] == 0xff && bytes.back() == 0xd9;
}
ThumbnailCacheIndexResult Failure(std::string error) { return {std::nullopt, std::move(error)}; }
}
std::optional<std::size_t> ThumbnailCacheIndex::AtOrBefore(double seconds) const noexcept {
    if (entries.empty() || !std::isfinite(seconds)) return std::nullopt;
    auto after = std::upper_bound(entries.begin(), entries.end(), seconds,
        [](double time, ThumbnailCacheEntry const& entry) { return time < entry.seconds; });
    return after == entries.begin() ? 0 : static_cast<std::size_t>(after - entries.begin() - 1);
}
ThumbnailCacheIndexResult IndexThumbnailCache(std::span<std::uint8_t const> bytes,
    std::optional<ThumbnailSourceIdentity> expectedSource, ThumbnailCacheLimits limits) {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
    if (bytes.size() > limits.maximumFileBytes) return Failure("Thumbnail cache exceeds the file size limit");
    if (bytes.size() < ThumbnailCacheMetadataBytes) return Failure("Truncated thumbnail metadata");
    if (bytes.front() != ThumbnailCacheVersion) return Failure("Unsupported thumbnail cache version");
    ThumbnailCacheIndex index;
    index.source = {Read64(bytes, 1), std::bit_cast<std::int64_t>(Read64(bytes, 9))};
    if (expectedSource && index.source != *expectedSource) return Failure("Thumbnail cache belongs to a different file revision");
    std::size_t position = ThumbnailCacheMetadataBytes;
    while (position < bytes.size()) {
        if (index.entries.size() >= limits.maximumThumbnails) return Failure("Too many thumbnail records");
        if (bytes.size() - position < 16) return Failure("Truncated thumbnail record header");
        auto length = std::bit_cast<std::int64_t>(Read64(bytes, position));
        position += 8;
        // Check the signed size before conversion/subtraction. The Swift reader
        // subtracts without rejecting negative/small/oversized block lengths.
        if (length < 12 || static_cast<std::uint64_t>(length) > bytes.size() - position)
            return Failure("Invalid thumbnail record length");
        auto seconds = std::bit_cast<double>(Read64(bytes, position));
        if (!std::isfinite(seconds) || seconds < 0) return Failure("Invalid thumbnail timestamp");
        if (!index.entries.empty() && seconds < index.entries.back().seconds)
            return Failure("Thumbnail timestamps are not ordered");
        position += 8;
        auto imageSize = static_cast<std::size_t>(length - 8);
        if (imageSize > limits.maximumImageBytes) return Failure("Thumbnail image exceeds the size limit");
        auto image = bytes.subspan(position, imageSize);
        if (!JpegMarkers(image)) return Failure("Thumbnail record is not JPEG framed");
        index.entries.push_back({seconds, position, imageSize});
        position += imageSize;
    }
    return {std::move(index), {}};
}
ThumbnailCacheEncodeResult EncodeThumbnailCache(ThumbnailSourceIdentity source,
    std::span<EncodedThumbnail const> thumbnails, ThumbnailCacheLimits limits) {
    if (thumbnails.size() > limits.maximumThumbnails) return {{}, "Too many thumbnail records"};
    std::size_t size = ThumbnailCacheMetadataBytes;
    double previous = -1;
    if (size > limits.maximumFileBytes) return {{}, "Thumbnail metadata exceeds the file size limit"};
    for (auto const& item : thumbnails) {
        if (!std::isfinite(item.seconds) || item.seconds < 0 || item.seconds < previous)
            return {{}, "Invalid or unordered thumbnail timestamp"};
        previous = item.seconds;
        if (item.jpeg.size() > limits.maximumImageBytes || !JpegMarkers(item.jpeg))
            return {{}, "Invalid or oversized JPEG thumbnail"};
        if (item.jpeg.size() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - 8)
            return {{}, "Thumbnail record length is not representable"};
        if (limits.maximumFileBytes - size < 16 || item.jpeg.size() > limits.maximumFileBytes - size - 16)
            return {{}, "Thumbnail cache exceeds the file size limit"};
        size += 16 + item.jpeg.size();
    }
    ThumbnailCacheEncodeResult result;
    result.bytes.reserve(size);
    result.bytes.push_back(ThumbnailCacheVersion);
    Write64(result.bytes, source.size);
    Write64(result.bytes, std::bit_cast<std::uint64_t>(source.modifiedUnixSeconds));
    for (auto const& item : thumbnails) {
        Write64(result.bytes, item.jpeg.size() + 8);
        Write64(result.bytes, std::bit_cast<std::uint64_t>(item.seconds));
        result.bytes.insert(result.bytes.end(), item.jpeg.begin(), item.jpeg.end());
    }
    return result;
}
std::optional<std::span<std::uint8_t const>> ThumbnailImageBytes(
    std::span<std::uint8_t const> bytes, ThumbnailCacheEntry const& entry) noexcept {
    if (entry.jpegOffset > bytes.size() || entry.jpegLength > bytes.size() - entry.jpegOffset)
        return std::nullopt;
    return bytes.subspan(entry.jpegOffset, entry.jpegLength);
}
std::optional<ThumbnailSourceIdentity> ReadThumbnailSourceIdentity(
    std::filesystem::path const& mediaPath, std::string& error) {
    error.clear();
    std::error_code code;
    auto size = std::filesystem::file_size(mediaPath, code);
    if (code) { error = code.message(); return std::nullopt; }
    auto modified = std::filesystem::last_write_time(mediaPath, code);
    if (code) { error = code.message(); return std::nullopt; }
    // duration_cast truncates toward zero, matching Swift Int64(Date seconds).
    auto systemTime = std::chrono::file_clock::to_sys(modified);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
    return ThumbnailSourceIdentity{size, seconds};
}
ThumbnailCacheIndexResult ReadThumbnailCacheFile(std::filesystem::path const& path,
    std::vector<std::uint8_t>& bytes, std::optional<ThumbnailSourceIdentity> expectedSource,
    ThumbnailCacheLimits limits) {
    bytes.clear();
    std::error_code code;
    auto size = std::filesystem::file_size(path, code);
    if (code) return Failure(code.message());
    if (size > limits.maximumFileBytes || size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return Failure("Thumbnail cache exceeds the file size limit");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return Failure("Unable to open thumbnail cache");
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.peek() != std::char_traits<char>::eof()) {
        bytes.clear();
        return Failure("Thumbnail cache changed or could not be read completely");
    }
    auto result = IndexThumbnailCache(bytes, expectedSource, limits);
    if (!result.index) bytes.clear();
    // A corrupt file is not deleted as a side effect of inspecting untrusted
    // bytes. The storage owner can quarantine/remove it under its cache policy.
    return result;
}
}

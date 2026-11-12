// Subtitle matching hashes adapted from IINA OpenSubSubtitle/ShooterSubtitle.
// Copyright (c) lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
// Reference snapshot: references/iina-source.txt.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace iima {
inline constexpr std::size_t OpenSubHashChunkBytes = 65536;
inline constexpr std::uint64_t OpenSubHashMinimumFileBytes = 131072;
inline constexpr std::size_t ShooterHashChunkBytes = 4096;
inline constexpr std::uint64_t ShooterHashMinimumFileBytes = 12288;

// No file I/O, native-endian assumptions or unaligned UInt64 loads. Caller must
// read exactly 64 KiB at offsets 0 and fileSize-65536 from the SAME stable file.
// The fixed IINA snapshot rejects size <= 131072, including exactly 128 KiB.
// Raises invalid_argument on a too-small file or either truncated/wrong span.
// Unsigned modulo-2^64 sum is the OpenSubtitles protocol, not a security digest.
std::string OpenSubMovieHash(std::uint64_t fileSize,
    std::span<const std::uint8_t> first64KiB,
    std::span<const std::uint8_t> last64KiB);

// IINA order is 4096, (size/3)*2, size/3, size-8192. Division BEFORE
// multiplication is significant for sizes not divisible by three. Read exactly
// 4096 bytes at each offset, calculate MD5 independently, join in this order.
std::array<std::uint64_t, 4> ShooterChunkOffsets(std::uint64_t fileSize);
std::string JoinShooterDigests(const std::array<std::string, 4>& md5HexDigests);
}

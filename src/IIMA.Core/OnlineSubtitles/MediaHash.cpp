// Subtitle matching hashes adapted from IINA OpenSubSubtitle/ShooterSubtitle.
// Copyright (c) lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "MediaHash.h"
#include <stdexcept>

namespace {
std::uint64_t SumLittleEndianWords(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t sum{};
    for (std::size_t offset = 0; offset < bytes.size(); offset += 8) {
        std::uint64_t word{};
        for (unsigned octet = 0; octet != 8; ++octet)
            word |= static_cast<std::uint64_t>(bytes[offset + octet]) << (octet * 8);
        sum += word;
    }
    return sum;
}
std::string Hex64(std::uint64_t value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result(16, '0');
    for (std::size_t i = 0; i != result.size(); ++i) {
        result[result.size() - 1 - i] = hex[value & 0xf];
        value >>= 4;
    }
    return result;
}
}
namespace iima {
std::string OpenSubMovieHash(std::uint64_t fileSize,
    std::span<const std::uint8_t> first, std::span<const std::uint8_t> last) {
    if (fileSize <= OpenSubHashMinimumFileBytes)
        throw std::invalid_argument("OpenSubtitles hashing requires a file larger than 131072 bytes");
    if (first.size() != OpenSubHashChunkBytes || last.size() != OpenSubHashChunkBytes)
        throw std::invalid_argument("OpenSubtitles hashing requires two complete 65536-byte chunks");
    return Hex64(fileSize + SumLittleEndianWords(first) + SumLittleEndianWords(last));
}
std::array<std::uint64_t, 4> ShooterChunkOffsets(std::uint64_t fileSize) {
    if (fileSize < ShooterHashMinimumFileBytes)
        throw std::invalid_argument("Shooter hashing requires a file of at least 12288 bytes");
    return {4096, (fileSize / 3) * 2, fileSize / 3, fileSize - 8192};
}
std::string JoinShooterDigests(const std::array<std::string, 4>& digests) {
    std::string result;
    result.reserve(4 * 32 + 3);
    for (const auto& digest : digests) {
        if (digest.size() != 32) throw std::invalid_argument("Shooter requires four complete MD5 digests");
        if (!result.empty()) result += ';';
        for (auto ch : digest) {
            if (ch >= 'A' && ch <= 'F') ch += 'a' - 'A';
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
                throw std::invalid_argument("Shooter MD5 digest is not hexadecimal");
            result += ch;
        }
    }
    return result;
}
}

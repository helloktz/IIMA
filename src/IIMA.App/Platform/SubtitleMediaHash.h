// Windows file/hash adaptation of IINA OpenSubSubtitle and ShooterSubtitle.
// Copyright (c) lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>

namespace iima {
struct SubtitleMediaHash {
    std::uint64_t fileSize{};
    std::string hash;
};
// Synchronous background-worker API. No network request or account access.
// Accepts only ordinary absolute drive-letter paths on local fixed/removable
// drives; refuses URL, UNC, device namespace, ADS, offline and reparse paths.
// Holds read-only handles excluding writers/deletion for the duration. Reads
// only protocol chunks, checks metadata before and after, never returns partial
// results. Win32 failures raise hresult_error; cancellation raises ERROR_CANCELLED.
// Cancellation is cooperative between bounded reads, not a hard disk-I/O timeout.
SubtitleMediaHash ReadOpenSubMovieHash(const std::filesystem::path& path,
    std::stop_token stop = {});
SubtitleMediaHash ReadShooterMovieHash(const std::filesystem::path& path,
    std::stop_token stop = {});
}

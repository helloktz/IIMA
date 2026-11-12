// Downloaded subtitle ownership adapted from IINA OnlineSubtitle/AssrtSubtitle.
// Copyright (c) 2017-2018 lhc and IINA contributors. GPL-3.0.
// Source snapshot and license: references/iina-source.txt, references/iina/LICENSE.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>
#include <winrt/base.h>

namespace iima {
// Worker-thread-only owner. Keep alive through completed PlayerSession shutdown:
// mpv may reopen an external subtitle for reload or when switching tracks. A
// successful sub-add reply is NOT a sufficient lifetime barrier for deleting it.
// Cleanup is exact-path, nonrecursive and best-effort; a failure leaves private
// temporary data rather than deleting unrelated files. Crash recovery is not
// provided by this class. Callers may share ownership but must serialize Write.
class SubtitleStaging final {
public:
    static constexpr std::size_t MaximumFileBytes = 8 * 1024 * 1024;
    static constexpr std::size_t MaximumTotalBytes = 32 * 1024 * 1024;
    static constexpr std::size_t MaximumFiles = 32;

    static SubtitleStaging Create();
    SubtitleStaging(SubtitleStaging&& other) noexcept;
    SubtitleStaging& operator=(SubtitleStaging&& other) noexcept;
    ~SubtitleStaging();
    SubtitleStaging(const SubtitleStaging&) = delete;
    SubtitleStaging& operator=(const SubtitleStaging&) = delete;

    // Provider names are untrusted UTF-8, never interpreted as destination paths.
    // The supported text-subtitle extension is retained, not inferred from MIME.
    // Empty, oversized, executable/archive or obvious binary content is rejected.
    // Returns only after all bytes are written, flushed and the file is closed.
    std::filesystem::path Write(std::string_view providerFilenameUtf8,
        const std::vector<std::uint8_t>& bytes);
    const std::filesystem::path& Directory() const noexcept;
    // Copy only a file owned by this object to an explicitly chosen destination.
    // Blocking: call off UI, and retain shared ownership through completion.
    void Publish(std::filesystem::path const& source,
        std::filesystem::path const& destination) const;
private:
    SubtitleStaging(std::filesystem::path directory, winrt::handle directoryLock);
    void Cleanup() noexcept;
    std::filesystem::path directory_;
    winrt::handle directoryLock_;
    std::vector<std::filesystem::path> files_;
    std::size_t totalBytes_{};
    std::uint64_t nextFile_{};
};
}

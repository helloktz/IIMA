// IINA JavascriptAPIFile / JavascriptAPIHttp.download / parsePath adaptation.
// Copyright (c) 2020 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <string>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <vector>

namespace iima {
// Synchronous worker-only private storage. Construction performs no filesystem
// I/O; an instance owns a random @tmp namespace and an identifier-scoped @data
// namespace. This is a restricted API, not an operating-system sandbox.
class PluginFileStore final {
public:
    explicit PluginFileStore(std::string identifier);
    // JSON object {path,content?,options?:{encoding?,includeSubDir?}}; JSON result. Throws
    // for unsupported paths/options, corrupt UTF-8 and I/O failures.
    // Move accepts {path,destination}, never overwrites, and supports ordinary
    // binary/text files between these two namespaces with the same quotas. Moving
    // into @tmp opts into its shutdown cleanup; it does not retain the source.
    // Only direct filenames below @data/ or @tmp/ are supported. list returns filename/path/
    // isDir fields. No implicit subdirectory creation, recursion or external I/O.
    std::string Invoke(std::string const& operation, std::string const& argumentsJson);
    // Pure syntax validation, no file I/O or namespace creation; call before
    // transmitting a network request. WriteDownload repeats all validation.
    static void ValidateDownloadPath(std::string const& path);
    // Native download worker only. The bytes may be binary; the same 1 MiB
    // per-file, 128-file and 16 MiB namespace quotas apply. Only direct filenames
    // below @data/ or @tmp/ are accepted. Replaces atomically after cancellation
    // checks; cancellation racing after the commit does not roll back the file.
    // Does not execute downloaded content or grant access outside private storage.
    void WriteDownload(std::string const& path, std::vector<std::uint8_t> const& bytes,
        std::stop_token stop);
    // Owner calls after all file/network producers have stopped, never UI.
    // Marks this instance closed so even a late operation cannot recreate @tmp.
    // Best-effort bounded removal
    // of ordinary session files; no recursion and no follow of unsafe entries.
    void CleanupTemporary() noexcept;
private:
    // Serializes JS file calls, network download commits and shutdown cleanup.
    // Disk I/O is synchronous; cancellation cannot interrupt a native OS call.
    std::mutex mutex_;
    bool closed_{};
    bool temporaryCreated_{};
    std::wstring identity_;
    std::wstring temporary_;
    // Destruction does no disk I/O (may occur on UI during failed startup).
    // CleanupTemporary is explicit worker-side; crash/failure leftovers remain.
};
}

// IINA JavascriptAPIFile / JavascriptAPI.parsePath native adaptation.
// Copyright (c) 2020 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <string>

namespace iima {
// Synchronous worker-only private storage. Construction performs no filesystem
// I/O; an instance owns a random @tmp namespace and an identifier-scoped @data
// namespace. This is a restricted API, not an operating-system sandbox.
class PluginFileStore final {
public:
    explicit PluginFileStore(std::string identifier);
    // JSON object {path,content?,options?:{encoding?,includeSubDir?}}; JSON result. Throws
    // for unsupported paths/options, corrupt UTF-8 and I/O failures. Only direct
    // filenames below @data/ or @tmp/ are supported. list returns filename/path/
    // isDir fields. No implicit subdirectory creation, recursion or external I/O.
    std::string Invoke(std::string const& operation, std::string const& argumentsJson);
    // Owner calls on worker termination, never UI. Best-effort bounded removal
    // of ordinary session files; no recursion and no follow of unsafe entries.
    void CleanupTemporary() noexcept;
private:
    bool temporaryCreated_{};
    std::wstring identity_;
    std::wstring temporary_;
    // Destruction does no disk I/O (may occur on UI during failed startup).
    // CleanupTemporary is explicit worker-side; crash/failure leftovers remain.
};
}

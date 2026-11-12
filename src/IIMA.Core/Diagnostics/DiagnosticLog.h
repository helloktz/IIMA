// Record snapshots adapt IINA Logger.Log / LogWindowController.flushBuffer.
// Copyright lhc and contributors. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iima {
struct DiagnosticLogRecord {
    std::uint64_t sequence{};
    std::int64_t timestampMs{}; // UTC Unix milliseconds, captured at Write.
    std::string category;
    std::string message;
};
struct DiagnosticLogSnapshot {
    std::vector<DiagnosticLogRecord> records; // oldest retained record first
    std::uint64_t revision{};
    std::uint64_t evicted{}; // memory records removed by the 1024-record bound
    std::uint64_t diskQueueDropped{}; // events not queued for disk, not evictions
    bool closing{};
    bool closed{};
};
// Best-effort bounded local diagnostics, not telemetry. Never pass paths, URLs or
// user content. Calls do no filesystem IO; close completion runs on the worker.
class DiagnosticLog final {
public:
    explicit DiagnosticLog(std::filesystem::path directory,
                           std::function<void(std::string)> failure = {});
    ~DiagnosticLog();
    DiagnosticLog(DiagnosticLog const&) = delete;
    DiagnosticLog& operator=(DiagnosticLog const&) = delete;
    void Write(std::string category, std::string value);
    // Owning, non-destructive snapshot. No filesystem I/O; copies at most 1024
    // records under a mutex. It is not a complete disk history nor a guarantee
    // that any record has been flushed to disk. No log levels are fabricated.
    DiagnosticLogSnapshot Snapshot() const;
    void CloseAsync(std::function<void()> complete = {});
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

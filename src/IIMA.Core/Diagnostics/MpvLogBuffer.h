// IINA MPVController.handleEvent / LogWindowController bounded memory projection.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
namespace iima {
struct MpvLogRecord {
    std::uint64_t sequence{};
    std::int64_t timestampMs{};
    int level{};
    std::string prefix,text;
    bool truncated{};
};
struct MpvLogSnapshot {
    std::vector<MpvLogRecord> records;
    std::uint64_t revision{},dropped{},truncated{};
    bool capturing{},pending{},closed{};
    std::string level{"no"},error;
};
// Independent memory only. Raw mpv text may contain paths, signed URLs and
// credentials: never pass these records to the ordinary persistent logger.
class MpvLogBuffer final {
public:
    static bool ValidLevel(std::string const&) noexcept;
    bool Start(std::string level);
    void Stop(); // Immediately gate new records, retain the existing display.
    void Clear(); // Clear memory and flush native logs via a fresh worker epoch.
    void Close(); // Immediately erase all raw content and reject future starts.
    MpvLogSnapshot Snapshot() const;
    std::pair<std::uint64_t,std::string> Desired() const;
    void Complete(std::uint64_t epoch, int error);
    void Append(std::uint64_t epoch,int level,char const* prefix,char const* text);
    void Overflow(std::uint64_t epoch);
private:
    mutable std::mutex mutex_;
    std::deque<MpvLogRecord> records_;
    std::uint64_t epoch_{},sequence_{},revision_{},dropped_{},truncated_{};
    std::size_t bytes_{};
    bool capturing_{},pending_{},closed_{};
    std::string level_{"no"},error_;
};
}

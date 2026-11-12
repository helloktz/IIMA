// IINA MPVController.handleEvent seek/restart timing, conservative correlation.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
namespace iima {
struct SeekLatencyResult {
    std::uint64_t request{};
    std::optional<double> seconds; // absent means ambiguous, failed or cancelled
};
// Worker-local, steady clock only. mpv SEEK/RESTART have no request IDs, so
// this is an observational window, not proof of causality with external clients.
class SeekLatency final {
public:
    using Clock = std::chrono::steady_clock;
    void Begin(std::uint64_t request) {
        request_ = request; acknowledged_ = false; start_.reset(); elapsed_.reset();
    }
    std::optional<SeekLatencyResult> Cancel() {
        if (!request_) return {};
        auto request = request_; Begin(0); return SeekLatencyResult{request,{}};
    }
    std::optional<SeekLatencyResult> Seek(Clock::time_point now) {
        if (!request_) return {};
        if (start_ || elapsed_) return Cancel(); // overlapping/restarted seek
        start_ = now; return {};
    }
    std::optional<SeekLatencyResult> Restart(Clock::time_point now) {
        if (!request_) return {};
        if (!start_ || elapsed_) return Cancel();
        const double elapsed = std::chrono::duration<double>(now-*start_).count();
        if (elapsed < 0 || elapsed > 10) return Cancel();
        elapsed_ = elapsed;
        return Finish();
    }
    std::optional<SeekLatencyResult> Reply(std::uint64_t request, bool success) {
        if (!request_ || request != request_) return {};
        if (!success) return Cancel();
        acknowledged_ = true; return Finish();
    }
private:
    std::optional<SeekLatencyResult> Finish() {
        if (!acknowledged_ || !elapsed_) return {};
        SeekLatencyResult result{request_,elapsed_}; Begin(0); return result;
    }
    std::uint64_t request_{};
    bool acknowledged_{};
    std::optional<Clock::time_point> start_;
    std::optional<double> elapsed_;
};
}

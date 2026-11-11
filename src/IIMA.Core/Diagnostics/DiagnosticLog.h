#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace iima {
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
    void CloseAsync(std::function<void()> complete = {});
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iima {
// Owns all payloads; never exports mpv's borrowed event memory.
struct MpvEvent {
    enum class Kind { Ready, Failure, StartFile, Loaded, EndFile, Seek, Restart,
                      Property, CommandReply, VideoReconfig, Shutdown } kind;
    std::string name, text;
    std::optional<double> number;
    std::optional<bool> flag;
    std::int64_t entryId{-1};
    std::uint64_t requestId{};
    int error{}, reason{};
};
class MpvClient final {
public:
    using Sink = std::function<void(MpvEvent)>;
    explicit MpvClient(Sink sink);
    ~MpvClient();
    MpvClient(MpvClient const&) = delete;
    MpvClient& operator=(MpvClient const&) = delete;
    // DLL must be absolute and supplied from the application deployment directory.
    void Start(std::wstring dllPath);
    std::uint64_t Command(std::vector<std::string> arguments);
    // Nonblocking; completion runs on worker AFTER terminate_destroy and DLL release.
    void CloseAsync(std::function<void()> completion = {});
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

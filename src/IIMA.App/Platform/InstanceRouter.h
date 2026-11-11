#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iima {
// Single instance per Windows logon session. Only paths cross the pipe, never
// shell commands. Filesystem resolution remains the receiver's responsibility.
class InstanceRouter final {
public:
    using Receiver = std::function<bool(std::vector<std::wstring>)>;
    enum class ForwardResult { Accepted, Rejected, Unavailable, Uncertain };
    InstanceRouter();
    ~InstanceRouter();
    InstanceRouter(InstanceRouter const&) = delete;
    InstanceRouter& operator=(InstanceRouter const&) = delete;
    bool IsPrimary() const noexcept;
    // Caller supplies normalized absolute paths. Empty vector just activates UI.
    // Uncertain means a request may already have been queued: do not auto-retry.
    ForwardResult Forward(std::vector<std::wstring> const& absoluteInputs);
    // Invoked on pipe worker. Return true only if UI dispatch accepted the work.
    // Install once. Pending startup request waits boundedly for installation.
    void SetReceiver(Receiver receiver);
    // Nonblocking: immediately reject new requests and cancel pending pipe IO.
    void StopAccepting() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}

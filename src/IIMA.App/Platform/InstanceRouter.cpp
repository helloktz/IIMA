#include "pch.h"
#include "InstanceRouter.h"
#include "InstanceProtocol.h"
#include <sddl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#pragma comment(lib, "advapi32.lib")

namespace iima {
namespace {
constexpr auto MaxPayload = activation::MaxPayload;
constexpr ULONGLONG Timeout = 10000;
struct Handle {
    HANDLE value{nullptr};
    explicit Handle(HANDLE handle = nullptr) noexcept : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(Handle const&) = delete;
    Handle& operator=(Handle const&) = delete;
};
struct LocalMemory {
    void* value{};
    ~LocalMemory() { if (value) LocalFree(value); }
};
void Check(bool ok) { if (!ok) winrt::throw_last_error(); }
DWORD Remaining(ULONGLONG deadline) noexcept {
    auto now = GetTickCount64();
    return now >= deadline ? 0 : static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, MAXDWORD - 1));
}
std::wstring LogonSid() {
    HANDLE raw{};
    Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw) != FALSE);
    Handle token(raw);
    DWORD bytes{};
    GetTokenInformation(token.value, TokenGroups, nullptr, 0, &bytes);
    Check(bytes != 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    std::vector<std::byte> buffer(bytes);
    Check(GetTokenInformation(token.value, TokenGroups, buffer.data(), bytes, &bytes) != FALSE);
    auto groups = reinterpret_cast<TOKEN_GROUPS const*>(buffer.data());
    for (DWORD i = 0; i < groups->GroupCount; ++i) {
        if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID) {
            LPWSTR text{};
            Check(ConvertSidToStringSidW(groups->Groups[i].Sid, &text) != FALSE);
            LocalMemory owner{ text };
            return text;
        }
    }
    throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_NO_SUCH_LOGON_SESSION), L"Cannot isolate the IIMA activation endpoint to a logon session.");
}
// Every issued OVERLAPPED remains alive until completion, including cancellation.
bool Await(HANDLE io, OVERLAPPED& operation, HANDLE stop, DWORD timeout, DWORD& transferred) noexcept {
    HANDLE events[]{stop, operation.hEvent};
    auto result = WaitForMultipleObjects(2, events, FALSE, timeout);
    if (result == WAIT_OBJECT_0 + 1) return GetOverlappedResult(io, &operation, &transferred, FALSE) != FALSE;
    CancelIoEx(io, &operation);
    // Local named-pipe IO is cancellable. Completion is awaited before releasing
    // the event/buffer; the worker never calls into or waits for the UI thread.
    GetOverlappedResult(io, &operation, &transferred, TRUE);
    return false;
}
bool Transfer(HANDLE pipe, HANDLE stop, void* bytes, DWORD count, bool writing, ULONGLONG deadline,
              bool* touched = nullptr) {
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event.value) return false;
    auto cursor = static_cast<std::byte*>(bytes);
    while (count) {
        if (!Remaining(deadline) || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return false;
        OVERLAPPED operation{}; operation.hEvent = event.value;
        ResetEvent(event.value);
        DWORD done{};
        BOOL ok = writing ? WriteFile(pipe, cursor, count, &done, &operation)
                          : ReadFile(pipe, cursor, count, &done, &operation);
        // A submitted write can reach the server even if its completion is lost.
        if (writing && touched) *touched = true;
        if (!ok && (GetLastError() != ERROR_IO_PENDING || !Await(pipe, operation, stop, Remaining(deadline), done))) return false;
        if (ok && !GetOverlappedResult(pipe, &operation, &done, FALSE)) return false;
        if (!done || done > count) return false;
        cursor += done; count -= done;
    }
    return true;
}
std::vector<std::byte> Encode(std::vector<std::wstring> const& inputs) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    if (inputs.size() > activation::MaxPaths) throw winrt::hresult_invalid_argument(L"Too many activation paths.");
    std::vector<std::u16string> unicode;
    for (auto const& input : inputs) {
        if (input.size() > 32767) throw winrt::hresult_invalid_argument(L"Activation path is too long.");
        unicode.emplace_back(input.begin(), input.end());
    }
    auto bytes = activation::Encode(unicode);
    if (!bytes) throw winrt::hresult_invalid_argument(L"Activation contains invalid paths or exceeds its 256-file / 1 MiB limit.");
    return std::move(*bytes);
}
bool Decode(std::vector<std::byte> const& bytes, std::vector<std::wstring>& result) {
    auto paths = activation::Decode(bytes);
    if (!paths) return false;
    for (auto const& path : *paths) result.emplace_back(path.begin(), path.end());
    return true;
}
}
struct InstanceRouter::State {
    Handle instance, pipe, stop, receiverReady;
    std::wstring name;
    bool primary{};
    std::atomic_bool accepting{true};
    std::mutex receiverMutex;
    Receiver receiver;
    std::thread worker;
    State() {
        stop.value = CreateEventW(nullptr, TRUE, FALSE, nullptr); Check(stop.value != nullptr);
        receiverReady.value = CreateEventW(nullptr, TRUE, FALSE, nullptr); Check(receiverReady.value != nullptr);
        auto sid = LogonSid();
        auto descriptor = L"D:P(A;;GA;;;" + sid + L")";
        PSECURITY_DESCRIPTOR security{};
        Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor.c_str(), SDDL_REVISION_1, &security, nullptr) != FALSE);
        LocalMemory owner{security};
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security, FALSE};
        auto mutexName = L"Local\\IIMA.Instance." + sid;
        SetLastError(ERROR_SUCCESS);
        instance.value = CreateMutexW(&attributes, FALSE, mutexName.c_str());
        auto error = GetLastError();
        Check(instance.value != nullptr);
        primary = error != ERROR_ALREADY_EXISTS;
        name = L"\\\\.\\pipe\\IIMA.Activation." + sid;
        if (!primary) return;
        pipe.value = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 4096, 4096, static_cast<DWORD>(Timeout), &attributes);
        Check(pipe.value != INVALID_HANDLE_VALUE);
        worker = std::thread([this] { Serve(); });
    }
    ~State() {
        accepting = false; SetEvent(stop.value);
        if (pipe.value && pipe.value != INVALID_HANDLE_VALUE) CancelIoEx(pipe.value, nullptr);
        if (worker.joinable()) worker.join();
    }
    void Serve() noexcept {
        try {
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event.value) return;
            while (accepting && WaitForSingleObject(stop.value, 0) != WAIT_OBJECT_0) {
                OVERLAPPED connection{}; connection.hEvent = event.value;
                ResetEvent(event.value);
                bool connected = ConnectNamedPipe(pipe.value, &connection) != FALSE;
                if (!connected) {
                    auto error = GetLastError(); DWORD unused{};
                    if (error == ERROR_PIPE_CONNECTED) connected = true;
                    else if (error == ERROR_NO_DATA) { DisconnectNamedPipe(pipe.value); continue; }
                    else if (error == ERROR_IO_PENDING) connected = Await(pipe.value, connection, stop.value, INFINITE, unused);
                }
                if (!connected) break;
                ServeRequest();
                DisconnectNamedPipe(pipe.value);
            }
        } catch (...) { accepting = false; }
    }
    void ServeRequest() {
        auto deadline = GetTickCount64() + Timeout;
        std::uint32_t count{};
        if (!Transfer(pipe.value, stop.value, &count, sizeof(count), false, deadline) || count < 12 || count > MaxPayload) return;
        std::vector<std::byte> bytes(count);
        if (!Transfer(pipe.value, stop.value, bytes.data(), count, false, deadline)) return;
        std::vector<std::wstring> paths;
        std::uint32_t status = 1;
        if (Decode(bytes, paths) && accepting) {
            HANDLE events[]{stop.value, receiverReady.value};
            if (WaitForMultipleObjects(2, events, FALSE, Remaining(deadline)) == WAIT_OBJECT_0 + 1 && accepting) {
                Receiver callback;
                { std::scoped_lock lock(receiverMutex); callback = receiver; }
                // Receiver only enqueues UI work; no synchronous UI callback/join.
                try { if (callback && callback(std::move(paths))) status = 0; }
                catch (...) { status = 1; }
            }
        }
        if (Transfer(pipe.value, stop.value, &status, sizeof(status), true, deadline)) {
            // Let client acknowledge reading status before DisconnectNamedPipe.
            // Unlike FlushFileBuffers this read remains cancellable and bounded.
            std::uint8_t acknowledgement{};
            Transfer(pipe.value, stop.value, &acknowledgement, sizeof(acknowledgement), false, deadline);
        }
    }
};
InstanceRouter::InstanceRouter() : state_(std::make_unique<State>()) {}
InstanceRouter::~InstanceRouter() = default;
bool InstanceRouter::IsPrimary() const noexcept { return state_->primary; }
void InstanceRouter::StopAccepting() noexcept {
    state_->accepting = false; SetEvent(state_->stop.value);
    if (state_->pipe.value && state_->pipe.value != INVALID_HANDLE_VALUE) CancelIoEx(state_->pipe.value, nullptr);
}
void InstanceRouter::SetReceiver(Receiver receiver) {
    if (!state_->primary || !receiver) throw winrt::hresult_invalid_argument(L"Only the primary instance can receive activations.");
    std::scoped_lock lock(state_->receiverMutex);
    if (state_->receiver) throw winrt::hresult_illegal_method_call(L"The activation receiver is already installed.");
    state_->receiver = std::move(receiver); SetEvent(state_->receiverReady.value);
}
InstanceRouter::ForwardResult InstanceRouter::Forward(std::vector<std::wstring> const& inputs) {
    if (state_->primary) throw winrt::hresult_illegal_method_call(L"The primary instance does not forward to itself.");
    auto bytes = Encode(inputs);
    auto deadline = GetTickCount64() + Timeout;
    Handle client;
    while (Remaining(deadline)) {
        client.value = CreateFileW(state_->name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (client.value != INVALID_HANDLE_VALUE) break;
        auto error = GetLastError();
        if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) return ForwardResult::Unavailable;
        // ERROR_FILE_NOT_FOUND can occur between mutex creation and pipe setup.
        if (error == ERROR_FILE_NOT_FOUND) Sleep(std::min<DWORD>(25, Remaining(deadline)));
        else {
            auto wait = Remaining(deadline);
            if (!wait) break; // Zero means NMPWAIT_USE_DEFAULT_WAIT, not a zero timeout.
            WaitNamedPipeW(state_->name.c_str(), std::min<DWORD>(200, wait));
        }
    }
    if (client.value == INVALID_HANDLE_VALUE || !client.value) return ForwardResult::Unavailable;
    ULONG serverProcess{};
    if (GetNamedPipeServerProcessId(client.value, &serverProcess))
        AllowSetForegroundWindow(serverProcess); // Best effort; Windows retains final foreground policy.
    bool submitted{};
    auto count = static_cast<std::uint32_t>(bytes.size());
    if (!Transfer(client.value, state_->stop.value, &count, sizeof(count), true, deadline, &submitted) ||
        !Transfer(client.value, state_->stop.value, bytes.data(), count, true, deadline, &submitted))
        return submitted ? ForwardResult::Uncertain : ForwardResult::Unavailable;
    std::uint32_t status{};
    if (!Transfer(client.value, state_->stop.value, &status, sizeof(status), false, deadline)) return ForwardResult::Uncertain;
    std::uint8_t acknowledgement = 1;
    Transfer(client.value, state_->stop.value, &acknowledgement, sizeof(acknowledgement), true, deadline);
    return status == 0 ? ForwardResult::Accepted : ForwardResult::Rejected;
}
}

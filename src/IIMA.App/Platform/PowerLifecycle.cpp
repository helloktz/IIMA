// IINA PlayerWindowController willSleepNotification pauses an active player and
// intentionally has no corresponding automatic resume. Windows adaptation.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PowerLifecycle.h"
#include <commctrl.h>
#include <exception>
#include <utility>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")

namespace iima {
struct PowerLifecycle::State : std::enable_shared_from_this<State> {
    HWND window{};
    DWORD thread{};
    HPOWERNOTIFY notification{};
    DWORD registrationError{};
    std::function<void()> onSuspend;
    bool closed{}, destroyed{}, suspended{}, notifying{};

    void RequireOwner() const noexcept {
        // Removing a subclass from another thread is unsupported. Continuing
        // after such misuse would leave its raw reference pointing to freed
        // state. Fail closed instead of hiding a use-after-free behind noexcept.
        if (GetCurrentThreadId() != thread) std::terminate();
    }
    void Detach() noexcept {
        RequireOwner();
        if (closed) return;
        closed = true;
        onSuspend = {};
        if (notification) {
            UnregisterSuspendResumeNotification(notification);
            notification = nullptr;
        }
        if (window) {
            auto hwnd = std::exchange(window, nullptr);
            RemoveWindowSubclass(hwnd, Procedure, reinterpret_cast<UINT_PTR>(this));
        }
    }
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM parameter,
        LPARAM data, UINT_PTR, DWORD_PTR reference) noexcept {
        // The owner can Close or destroy this adapter inside its callback or
        // inside a nested window destruction. Pin state before touching it.
        auto self = reinterpret_cast<State*>(reference)->weak_from_this().lock();
        if (!self) return DefSubclassProc(hwnd, message, parameter, data);
        bool handled{};
        try {
            if (message == WM_NCDESTROY) {
                self->destroyed = true;
                self->Detach();
            }
            else if (!self->closed && message == WM_POWERBROADCAST) {
                if (parameter == PBT_APMSUSPEND) {
                    handled = true;
                    if (!self->suspended && !self->notifying) {
                        self->suspended = true;
                        self->notifying = true;
                        struct NotificationEnd {
                            State& state;
                            ~NotificationEnd() { state.notifying = false; }
                        } end{*self};
                        // Copy before invocation: a reentrant Close may clear
                        // the stored function while this call is still active.
                        auto callback = self->onSuspend;
                        if (callback) callback();
                    }
                } else if (parameter == PBT_APMRESUMEAUTOMATIC ||
                           parameter == PBT_APMRESUMESUSPEND ||
                           parameter == PBT_APMRESUMECRITICAL) {
                    self->suspended = false;
                    // Resume re-arms the next suspend only. Never resume mpv:
                    // doing so would overwrite a user pause or stopped state.
                }
            }
        } catch (...) {
            // Do not unwind into user32 or delay power transition with a dialog.
            // This is best-effort delivery, not a synchronous pause guarantee.
            OutputDebugStringW(L"IIMA: suspend notification callback failed.\n");
        }
        // Always preserve the rest of the HWND subclass chain, even when a
        // callback detached this adapter. State stays pinned through the call.
        // A callback may synchronously DestroyWindow. Its nested NCDESTROY has
        // already traversed the chain; do not call user32 on that dead HWND
        // from this outer suspend frame. Ordinary Close still forwards below.
        if (self->destroyed && message != WM_NCDESTROY) return handled ? TRUE : 0;
        auto result = DefSubclassProc(hwnd, message, parameter, data);
        return handled ? TRUE : result;
    }
};

PowerLifecycle::PowerLifecycle(HWND window, std::function<void()> onSuspend) {
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() || !onSuspend)
        throw winrt::hresult_invalid_argument(L"Power lifecycle requires an owned UI window and callback");
    auto state = std::make_shared<State>();
    state->window = window;
    state->thread = GetCurrentThreadId();
    state->onSuspend = std::move(onSuspend);
    if (!SetWindowSubclass(window, State::Procedure, reinterpret_cast<UINT_PTR>(state.get()),
        reinterpret_cast<DWORD_PTR>(state.get())))
        throw winrt::hresult_error(E_FAIL, L"Cannot observe window power notifications");
    // Windows 8+ supports this notification registration, including connected
    // standby. Failure is observable, not reclassified as guaranteed coverage.
    state->notification = RegisterSuspendResumeNotification(window, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!state->notification) {
        state->registrationError = GetLastError();
        if (!state->registrationError) state->registrationError = ERROR_GEN_FAILURE;
    }
    state_ = std::move(state);
}
PowerLifecycle::~PowerLifecycle() { Close(); }
void PowerLifecycle::Close() noexcept {
    if (!state_) return;
    state_->Detach();
    state_.reset();
}
bool PowerLifecycle::ModernStandbyNotificationsRegistered() const noexcept {
    return state_ && !state_->closed && state_->notification != nullptr;
}
DWORD PowerLifecycle::RegistrationError() const noexcept {
    return state_ ? state_->registrationError : ERROR_SUCCESS;
}
}

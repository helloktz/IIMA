#pragma once
// Native adaptation of IINA PlayerWindowController willSleepNotification.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include <windows.h>
#include <functional>
#include <memory>

namespace iima {
// Observes this window's suspend notification; it does not prevent suspend,
// acquire power requests, resume playback or recreate graphics resources.
class PowerLifecycle final {
public:
    // All operations, including destruction, belong to the HWND's UI thread.
    // onSuspend runs inline during WM_POWERBROADCAST, not via DispatcherQueue:
    // the message loop may stop immediately afterwards. It should check the
    // current preference/active-session state and enqueue a pause to mpv, without
    // awaiting an acknowledgement, doing disk I/O, or running a nested message
    // loop. Enqueue is not a promise mpv processes the pause before suspension.
    PowerLifecycle(HWND window, std::function<void()> onSuspend);
    ~PowerLifecycle();
    PowerLifecycle(PowerLifecycle const&) = delete;
    PowerLifecycle& operator=(PowerLifecycle const&) = delete;
    void Close() noexcept;
    // Registration failure leaves the ordinary WM_POWERBROADCAST observation
    // installed, but modern-standby delivery must not then be claimed.
    bool ModernStandbyNotificationsRegistered() const noexcept;
    DWORD RegistrationError() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

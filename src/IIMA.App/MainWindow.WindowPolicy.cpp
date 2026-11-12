// Windows lifecycle projection for IINA pauseWhenInactive/pauseWhenMinimized.
// Copyright © IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>

namespace winrt::IIMA::implementation {
void MainWindow::WireWindowPlaybackPolicy() {
    HWND window{};
    check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&window));
    auto weak=get_weak();
    windowLifecycle_=std::make_unique<iima::WindowLifecycle>(window,DispatcherQueue(),
        [weak](iima::WindowLifecycleState state) {
            if(auto self=weak.get(); self && !self->closing_)
                self->session_->WindowLifecycleChanged(state.inactive,state.minimized);
        },[weak] {
            if (auto self=weak.get(); self && !self->closing_) self->RefreshAccessibility();
        });
    powerLifecycle_=std::make_unique<iima::PowerLifecycle>(window, [weak] {
        if(auto self=weak.get(); self && !self->closing_ &&
            self->preferences_.Boolean("pauseWhenGoesToSleep"))
            self->session_->SystemWillSuspend();
    });
    if(!powerLifecycle_->ModernStandbyNotificationsRegistered())
        ShowError(L"无法注册现代待机通知，仅保留传统睡眠广播；睡眠暂停可能不可用。Windows错误：" +
            winrt::to_hstring(powerLifecycle_->RegistrationError()));
    RefreshAccessibility();
    const auto state=windowLifecycle_->Current();
    session_->WindowLifecycleChanged(state.inactive,state.minimized);
}
}

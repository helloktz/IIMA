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
        });
    const auto state=windowLifecycle_->Current();
    session_->WindowLifecycleChanged(state.inactive,state.minimized);
}
}

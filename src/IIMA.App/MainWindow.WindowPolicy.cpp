// Windows lifecycle projection for IINA pauseWhenInactive/pauseWhenMinimized.
// Copyright © IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>

namespace winrt::IIMA::implementation {
void MainWindow::WireWindowPlaybackPolicy() {
    HWND window{};
    check_hresult(GetNativeWindowHandle(&window));
    auto weak=get_weak();
    windowLifecycle_=std::make_unique<iima::WindowLifecycle>(window,DispatcherQueue(),
        [weak](iima::WindowLifecycleState state) {
            if(auto self=weak.get(); self && !self->closing_)
                self->session_->WindowLifecycleChanged(state.inactive,state.minimized);
        },[weak] {
            if (auto self=weak.get(); self && !self->closing_) {
                self->RefreshAccessibility();
                self->UpdateWindowAspectConstraint();
            }
        },[weak] {
            if (auto self=weak.get(); self && !self->closing_ && !self->restoringPlacement_ && !self->compactTransition_ && !self->fullscreenTransition_ &&
                self->AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped) {
                // An explicit normal-window sizing gesture supersedes an
                // unfinished compact restore; never replay it over user input.
                if(self->compactRestore_) {
                    self->compactRestore_.reset();
                    self->compactLayoutPending_=true;
                    self->windowScaleExpectedPanelWidth_.reset();
                    self->ExitCompactOverlayButton().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
                }
                self->placementProgrammaticBounds_.reset();
                self->placementTouched_ = true;
                self->CancelAutomaticVideoSize();
            }
        });
    powerLifecycle_=std::make_unique<iima::PowerLifecycle>(window, [weak] {
        if(auto self=weak.get(); self && !self->closing_ &&
            self->preferences_.Boolean("pauseWhenGoesToSleep"))
            self->session_->SystemWillSuspend();
    });
    if(!powerLifecycle_->ModernStandbyNotificationsRegistered())
        ShowError(L"无法注册现代待机通知，仅保留传统睡眠广播；睡眠暂停可能不可用。Windows错误：" +
            winrt::to_hstring(static_cast<std::uint32_t>(powerLifecycle_->RegistrationError())));
    RefreshAccessibility();
    const auto state=windowLifecycle_->Current();
    session_->WindowLifecycleChanged(state.inactive,state.minimized);
}
}

// IINA PlayerWindowController.mouseUp/rightMouseUp/otherMouseUp and
// MainWindowController.performMouseAction. Copyright lhc and contributors.
// GPL-3.0; references/iina/LICENSE and references/iina-source.txt.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <chrono>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
namespace {
bool ModifiedMouseClick() {
    return (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000) ||
        (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_LWIN) & 0x8000) ||
        (GetKeyState(VK_RWIN) & 0x8000);
}
}
bool MainWindow::MouseClickAllowed() {
    auto const& state = session_->Snapshot();
    return !closing_ && !pickerOpen_ && !controlBarInactive_ && !ModifiedMouseClick() &&
        KeyboardPlaybackAllowed() && state.session == iima::SessionState::Ready &&
        state.media == iima::MediaState::Loaded;
}
void MainWindow::CancelMouseClick() {
    if (singleClickTimer_) singleClickTimer_.Stop();
    pendingClickAction_.reset();
}
void MainWindow::PerformMouseClick(std::int64_t action) {
    if (!MouseClickAllowed()) return;
    try {
        switch (action) {
        case 1: ToggleFullscreen(); break;
        case 2: session_->TogglePause(); break;
        case 3:
            // Same auto-hide preference and accessibility protections as timer.
            if (CanHideControlBar()) {
                controlBarTimer_.Stop();
                BottomControlsBorder().Visibility(Visibility::Collapsed);
                CancelThumbnailPreview();
            }
            break;
        case 4: ExecuteApplicationCommand(iima::ApplicationCommand::TogglePictureInPicture); break;
        case 5: session_->CycleABLoop(); break;
        case 6: session_->SetSpeed(1); break;
        default: break;
        }
    } catch (...) { if (!closing_) ShowError(L"无法执行鼠标播放操作。"); }
}
void MainWindow::WireMouseClicks() {
    auto weak = get_weak();
    singleClickTimer_ = DispatcherQueue().CreateTimer();
    singleClickTimer_.IsRepeating(false);
    singleClickTimer_.Tick([weak](auto const&, auto const&) {
        if (auto self = weak.get()) {
            auto action = self->pendingClickAction_;
            self->pendingClickAction_.reset();
            if (action && self->pendingClickEpoch_ == self->navigationEpoch_)
                self->PerformMouseClick(*action);
        }
    });
    VideoPanel().Tapped([weak](auto const&, Input::TappedRoutedEventArgs const& args) {
        auto self = weak.get();
        if (!self || args.Handled() || !self->MouseClickAllowed() ||
            args.PointerDeviceType() != Windows::Devices::Input::PointerDeviceType::Mouse) return;
        auto single = self->preferences_.Integer("singleClickAction");
        auto twice = self->preferences_.Integer("doubleClickAction");
        if (!single) return;
        self->CancelMouseClick();
        if (!twice) self->PerformMouseClick(single);
        else {
            self->pendingClickAction_ = single;
            self->pendingClickEpoch_ = self->navigationEpoch_;
            self->singleClickTimer_.Interval(std::chrono::milliseconds(GetDoubleClickTime()));
            self->singleClickTimer_.Start();
        }
        args.Handled(true);
    });
    VideoPanel().DoubleTapped([weak](auto const&, Input::DoubleTappedRoutedEventArgs const& args) {
        auto self = weak.get();
        if (!self || args.PointerDeviceType() != Windows::Devices::Input::PointerDeviceType::Mouse) return;
        self->CancelMouseClick();
        if (args.Handled() || !self->MouseClickAllowed()) return;
        auto action = self->preferences_.Integer("doubleClickAction");
        if (action) { self->PerformMouseClick(action); args.Handled(true); }
        else {
            // WinUI consumes the second tap into DoubleTapped even when the
            // product has no double action. Preserve the second single action.
            self->PerformMouseClick(self->preferences_.Integer("singleClickAction"));
        }
    });
    VideoPanel().RightTapped([weak](auto const&, Input::RightTappedRoutedEventArgs const& args) {
        auto self = weak.get();
        if (!self || args.Handled() || !self->MouseClickAllowed() ||
            args.PointerDeviceType() != Windows::Devices::Input::PointerDeviceType::Mouse) return;
        self->CancelMouseClick();
        auto action = self->preferences_.Integer("rightClickAction");
        if (action) { self->PerformMouseClick(action); args.Handled(true); }
    });
    auto cancel = [weak](auto const&, Input::PointerRoutedEventArgs const&) {
        if (auto self = weak.get()) self->CancelMouseClick();
    };
    VideoPanel().PointerCanceled(cancel);
    VideoPanel().PointerCaptureLost(cancel);
    VideoPanel().PointerExited(cancel);
    Activated([weak](auto const&, WindowActivatedEventArgs const& args) {
        if (auto self = weak.get(); self && args.WindowActivationState() == WindowActivationState::Deactivated)
            self->CancelMouseClick();
    });
}
}

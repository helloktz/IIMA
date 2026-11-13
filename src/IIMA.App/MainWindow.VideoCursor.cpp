// IINA MainWindowController.hideUIAndCursor / mouseMoved adaptation.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.Devices.Input.h>
#include <initializer_list>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireVideoCursor() {
    auto weak = get_weak();
    VideoPanel().PointerEntered([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        if (args.Pointer().PointerDeviceType() != Microsoft::UI::Input::PointerDeviceType::Mouse) return;
        if (auto self = weak.get(); self && !self->closing_) {
            self->videoMouseInside_ = true;
            // Entering a hidden-controls surface must first reveal it, rather
            // than immediately inheriting the previous idle cursor decision.
            self->ControlBarActivity();
        }
    });
    VideoPanel().PointerExited([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        if (args.Pointer().PointerDeviceType() != Microsoft::UI::Input::PointerDeviceType::Mouse) return;
        if (auto self = weak.get()) {
            self->videoMouseInside_ = false;
            self->VideoPanel().SetCursorHidden(false);
        }
    });
    VideoPanel().PointerCaptureLost([weak](auto const&, Input::PointerRoutedEventArgs const&) {
        if (auto self = weak.get(); self && !self->closing_) self->ControlBarActivity();
    });
    videoCursorVisibilityToken_ = BottomControlsBorder().RegisterPropertyChangedCallback(
        UIElement::VisibilityProperty(), [weak](auto const&, auto const&) {
            if (auto self = weak.get()) self->UpdateVideoCursor();
        });
}
void MainWindow::UpdateVideoCursor() {
    // A hidden bottom bar alone is insufficient: compact mode and crop also
    // hide it, but both need a visible pointer. Audio-only/music is excluded.
    bool hide = !closing_ && videoMouseInside_ && !pickerOpen_ && !controlBarInactive_ &&
        !interactiveCrop_.Active() && !cropOriginal_ && !musicMode_.IsMusic() &&
        BottomControlsBorder().Visibility() == Visibility::Collapsed;
    if (hide) {
        auto const& state = session_->Snapshot();
        hide = state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded &&
            state.voConfigured && CanHideControlBar();
    }
    // Keep the pointer while a button is held, even if a timer or an external
    // command hid the controls. Movement/press/key input will re-arm idleness.
    if (hide) for (int key : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2})
        if (GetKeyState(key) & 0x8000) { hide = false; break; }
    VideoPanel().SetCursorHidden(hide);
}
void MainWindow::CloseVideoCursor() {
    videoMouseInside_ = false;
    VideoPanel().SetCursorHidden(false);
    if (videoCursorVisibilityToken_) {
        BottomControlsBorder().UnregisterPropertyChangedCallback(
            UIElement::VisibilityProperty(), *videoCursorVisibilityToken_);
        videoCursorVisibilityToken_.reset();
    }
}
}

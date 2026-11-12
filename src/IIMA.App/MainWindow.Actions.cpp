// PlayerCore frame/seek/A-B actions projected to WinUI controls and keys.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.System.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
bool MainWindow::KeyboardPlaybackAllowed() const {
    auto focused = Input::FocusManager::GetFocusedElement(Root().XamlRoot());
    return !focused.try_as<Controls::TextBox>() && !focused.try_as<Controls::PasswordBox>() &&
        !focused.try_as<Controls::NumberBox>();
}
void MainWindow::WireAdvancedPlayback() {
    auto weak = get_weak();
    StepBackButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get(); self && !self->closing_) self->session_->StepFrame(true); });
    StepForwardButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get(); self && !self->closing_) self->session_->StepFrame(false); });
    ABLoopButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get(); self && !self->closing_) self->session_->CycleABLoop(); });
    ClearABLoopButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get(); self && !self->closing_) self->session_->ClearABLoop(); });
    auto add = [this, weak](Windows::System::VirtualKey key, double offset) {
        Input::KeyboardAccelerator accelerator;
        accelerator.Key(key);
        accelerator.Invoked([weak, offset](auto const&, auto const& args) {
            if (auto self = weak.get(); self && !self->closing_ && self->KeyboardPlaybackAllowed()) {
                self->session_->SeekRelative(offset); args.Handled(true);
            }
        });
        Root().KeyboardAccelerators().Append(accelerator);
    };
    add(Windows::System::VirtualKey::Left, -5);
    add(Windows::System::VirtualKey::Right, 5);
}
}

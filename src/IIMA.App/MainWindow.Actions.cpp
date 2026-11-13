// PlayerCore frame/seek/A-B actions projected to WinUI controls and keys.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.System.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
bool MainWindow::KeyboardPlaybackAllowed() {
    if (interactiveCrop_.Active() || cropOriginal_) return false;
    auto focused = Input::FocusManager::GetFocusedElement(Root().XamlRoot()).try_as<DependencyObject>();
    // NumberBox/AutoSuggestBox and editable ComboBox focus their internal text
    // controls; inspect ancestors as well, not just the immediate focused node.
    for (auto element = focused; element; element = Media::VisualTreeHelper::GetParent(element)) {
        if (element.try_as<Controls::TextBox>() || element.try_as<Controls::PasswordBox>() ||
            element.try_as<Controls::RichEditBox>() || element.try_as<Controls::NumberBox>() ||
            element.try_as<Controls::AutoSuggestBox>() || element.try_as<Controls::ContentDialog>()) return false;
    }
    return true;
}
void MainWindow::WireAdvancedPlayback() {
    auto weak = get_weak();
    auto relativeSeek = [weak](double seconds) {
        if (auto self = weak.get(); self && !self->closing_ && !self->pickerOpen_ && self->KeyboardPlaybackAllowed()) {
            auto choice = self->preferences_.Integer("useExactSeek");
            auto mode = choice == 2 ? iima::RelativeSeekMode::Automatic :
                choice == 1 ? iima::RelativeSeekMode::Exact : iima::RelativeSeekMode::Relative;
            self->session_->SeekRelativeWithMode(seconds,mode);
        }
    };
    RelativeSeekBackButton().Click([relativeSeek](auto const&, auto const&) { relativeSeek(-5); });
    RelativeSeekForwardButton().Click([relativeSeek](auto const&, auto const&) { relativeSeek(5); });
    StepBackButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get(); self && !self->closing_) self->session_->StepFrame(true); });
    StepForwardButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get(); self && !self->closing_) self->session_->StepFrame(false); });
    ABLoopButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get(); self && !self->closing_) self->session_->CycleABLoop(); });
    ClearABLoopButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get(); self && !self->closing_) self->session_->ClearABLoop(); });

}
}

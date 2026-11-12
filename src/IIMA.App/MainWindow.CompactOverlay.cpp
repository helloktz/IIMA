// IINA MainWindowController pipActionPlay/Pause/Stop, skipInterval and
// syncPIPPlaybackState. Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Windows CompactOverlay has no native transport. This explicit, always-visible
// strip uses fixed ten-second skips; returning never implicitly resumes.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/VideoTime.h"
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
bool MainWindow::CompactTransportAvailable() {
    auto const& state=session_->Snapshot();
    return !closing_ && userDataLoaded_ && !pickerOpen_ && !inputResolving_ &&
        pendingFiles_.empty() && activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ &&
        !session_->PlaylistEditBusy() && !state.mediaOriginPending &&
        AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay &&
        state.session==iima::SessionState::Ready && state.media==iima::MediaState::Loaded;
}
void MainWindow::WireCompactOverlayControls() {
    auto weak=get_weak();
    CompactPlayPauseButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && self->CompactTransportAvailable()) {
            if(self->session_->Snapshot().paused && !self->session_->PresentationReady()) return;
            self->CancelMouseClick();
            // Use the observed state, never the button's translated label.
            self->session_->Pause(!self->session_->Snapshot().paused);
        }
    });
    auto skip=[weak](double seconds) {
        if(auto self=weak.get();self && self->CompactTransportAvailable() && self->session_->Snapshot().seekable) {
            self->CancelMouseClick();
            self->session_->SeekRelativeWithMode(seconds,iima::RelativeSeekMode::Relative);
        }
    };
    CompactBackButton().Click([skip](auto const&,auto const&) {skip(-10);});
    CompactForwardButton().Click([skip](auto const&,auto const&) {skip(10);});
    CompactPauseReturnButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && self->CompactTransportAvailable()) {
            self->CancelMouseClick();
            // IINA's PIP stop is pause, NOT unload/stop. A rejected return does
            // not undo this explicit pause, and no optimistic state is shown.
            self->session_->Pause(true);
            if(!self->closing_ && self->AppWindow().Presenter().Kind()==
                Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay)
                self->ExecuteApplicationCommand(iima::ApplicationCommand::TogglePictureInPicture);
        }
    });
    CompactOverlayControls().AddHandler(UIElement::PointerPressedEvent(),Input::PointerEventHandler(
        [weak](auto const&,auto const&) {if(auto self=weak.get()) self->CancelMouseClick();}),true);
    CompactOverlayControls().Tapped([](auto const&,Input::TappedRoutedEventArgs const& e){e.Handled(true);});
    CompactOverlayControls().DoubleTapped([](auto const&,Input::DoubleTappedRoutedEventArgs const& e){e.Handled(true);});
    CompactOverlayControls().RightTapped([](auto const&,Input::RightTappedRoutedEventArgs const& e){e.Handled(true);});
}
void MainWindow::UpdateCompactOverlayControls() {
    const bool compact=!closing_ && AppWindow().Presenter().Kind()==
        Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay;
    CompactOverlayControls().Visibility(compact ? Visibility::Visible : Visibility::Collapsed);
    auto const& state=session_->Snapshot();
    const bool ready=CompactTransportAvailable();
    CompactPlayPauseButton().IsEnabled(ready && (state.paused ? session_->PresentationReady() : true));
    CompactPlayPauseButton().Content(box_value(state.paused ? L"播放" : L"暂停"));
    CompactBackButton().IsEnabled(ready && state.seekable);
    CompactForwardButton().IsEnabled(ready && state.seekable);
    CompactPauseReturnButton().IsEnabled(ready);
    auto clock=[](std::optional<double> value)->hstring {
        return value && std::isfinite(*value) && *value>=0 ? to_hstring(iima::VideoTime(*value).StringRepresentation()) : hstring(L"--:--");
    };
    CompactTimeText().Text(compact && state.media==iima::MediaState::Loaded ?
        clock(state.position)+L" / "+clock(state.duration) : hstring(L"未加载媒体"));
}
}

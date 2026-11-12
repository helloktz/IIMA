// IINA PlaySlider.scrollWheel / VolumeSlider.scrollWheel (guard then native).
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
// Windows adaptation: typed relative commands replace Slider's optimistic
// wheel ValueChanged. One notch uses SmallChange, not the video-surface policy.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.System.h>
#include <algorithm>
#include <cmath>
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireSliderWheels() {
    auto weak=get_weak();
    auto wire=[weak](IIMA::WheelSlider const& slider,bool volume) {
        slider.WheelInput([weak,volume](auto const& sender,Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            // WheelSlider has already swallowed the native wheel operation,
            // including disabled/modified input. Never mutate Slider.Value here.
            auto self=weak.get();if(!self) return;
            if(self->closing_ || !self->userDataLoaded_ || self->pickerOpen_ || (!volume && (self->scrubStarting_ || self->PositionSlider().Tracking())) || self->oscDragging_ ||
                self->interactiveCrop_.Active() || self->cropOriginal_ || self->inputResolving_ ||
                !self->pendingFiles_.empty() || !self->activationInputs_.empty() || self->session_->PlaylistEditBusy()) return;
            if(self->preferences_.Boolean(volume ? "disableVolumeSliderScrolling" : "disablePlaySliderScrolling") ||
                args.KeyModifiers()!=Windows::System::VirtualKeyModifiers::None) return;
            auto const& state=self->session_->Snapshot();
            if(state.session!=iima::SessionState::Ready || state.media!=iima::MediaState::Loaded || state.mediaOriginPending) return;
            auto target=sender.template as<IIMA::WheelSlider>();
            if(!target.IsEnabled()) return;
            auto properties=args.GetCurrentPoint(target).Properties();
            if(properties.IsLeftButtonPressed() || properties.IsRightButtonPressed() || properties.IsMiddleButtonPressed() ||
                properties.IsXButton1Pressed() || properties.IsXButton2Pressed()) return;
            const double change=target.SmallChange();
            const double steps=std::clamp(static_cast<double>(properties.MouseWheelDelta())/120.0,-32.0,32.0);
            if(!std::isfinite(change) || change<=0 || steps==0 || !std::isfinite(steps*change)) return;
            // High-resolution fractions are submitted directly, so separate
            // axes, media changes and modifiers cannot inherit a remainder.
            const double amount=steps*change;
            try {
                if(volume) self->session_->AdjustVolumeByWheel(amount);
                else {
                    if(self->scrubToken_) {
                        const auto generation=state.mediaGeneration,intent=state.mediaOpenIntentRevision;
                        self->CancelScrubbing();
                        if(self->closing_ || state.mediaGeneration!=generation || state.mediaOpenIntentRevision!=intent) return;
                    }
                    if(!state.seekable || !state.duration || !std::isfinite(*state.duration) || *state.duration<=0) return;
                    const auto choice=self->preferences_.Integer("useExactSeek");
                    const auto mode=self->preferences_.Boolean("wheelExactSeek") || choice==1 ? iima::RelativeSeekMode::Exact :
                        choice==2 ? iima::RelativeSeekMode::Automatic : iima::RelativeSeekMode::Relative;
                    self->session_->SeekRelativeWithMode(amount,mode);
                }
                self->ControlBarActivity();
            } catch(...) {if(!self->closing_) self->ShowError(L"无法提交滑块滚轮操作。");}
        });
    };
    wire(PositionSlider(),false);wire(VolumeSlider(),true);
}
}

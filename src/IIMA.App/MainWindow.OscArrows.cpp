// IINA MainWindowController leftButtonAction/rightButtonAction/arrowButtonAction.
// Copyright (c) 2016 lhc and contributors. GPL-3.0; references/iina/LICENSE.
// Windows has ordinary presses, not Force Touch. Paused playback stays paused.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Input.h>
#include <cmath>
#include <format>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
bool MainWindow::OscArrowAvailable() {
    auto const& s=session_->Snapshot();
    return !closing_ && userDataLoaded_ && !pickerOpen_ && !controlBarInactive_ && !inputResolving_ &&
        pendingFiles_.empty() && activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ &&
        !oscDragging_ && !abMarkerDragging_ && !scrubToken_ && !scrubStarting_ && !session_->PlaylistEditBusy() &&
        !fullscreenTransition_ && !musicTransition_ && !compactTransition_ && !compactLayoutPending_ &&
        BottomControlsBorder().Visibility()==Visibility::Visible &&
        AppWindow().Presenter().Kind()!=Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay &&
        s.session==iima::SessionState::Ready && s.media==iima::MediaState::Loaded && !s.mediaOriginPending;
}
void MainWindow::CancelOscArrow() {
    auto token=oscArrowToken_;oscArrowToken_=0;oscArrowPointer_=false;oscArrowStarting_=false;
    // Suppress the rest of this native dispatch, without disabling future UI Automation.
    auto weak=get_weak();DispatcherQueue().TryEnqueue([weak] {
        if(auto self=weak.get();self && !self->oscArrowPointer_)self->oscArrowSuppressClick_=false;
    });
    if(token && session_->ActiveSpeedHoldToken()==token) session_->CancelSpeedHold();
    OscLeftButton().ReleasePointerCaptures();OscRightButton().ReleasePointerCaptures();
}
void MainWindow::InvokeOscArrow(int direction) {
    if(!OscArrowAvailable() || oscArrowPointer_ || oscArrowStarting_) return;
    const auto mode=preferences_.Integer("arrowBtnAction");
    if(mode==0) {
        if(session_->SpeedHoldBusy()) return;
        auto token=session_->BeginSpeedHold(direction);
        if(token) session_->EndSpeedHold(token); // explicit keyboard/automation short tap
    } else if(mode==1) {
        if(direction<0) session_->PlaylistPrevious();else session_->PlaylistNext();
    } else if(mode==2 && session_->Snapshot().seekable)
        session_->SeekRelativeWithMode(direction*10.0,iima::RelativeSeekMode::Relative);
}
void MainWindow::UpdateOscArrows() {
    if(oscArrowUpdating_ || oscArrowStarting_) return;
    oscArrowUpdating_=true;
    struct Reset {bool& v;~Reset(){v=false;}} reset{oscArrowUpdating_};
    try {
        auto const& s=session_->Snapshot();const auto mode=preferences_.Integer("arrowBtnAction");
        if(oscArrowPointer_ || oscArrowToken_) {
            auto button=oscArrowDirection_<0?OscLeftButton():OscRightButton();
            auto root=Root().XamlRoot();auto origin=button.TransformToVisual(Root()).TransformPoint({0,0});
            if(!OscArrowAvailable() || mode!=oscArrowMode_ || inputGeneration_!=oscArrowInput_ ||
                s.mediaGeneration!=oscArrowGeneration_ || s.mediaOpenIntentRevision!=oscArrowIntent_ ||
                (mode==0 && session_->ActiveSpeedHoldToken()!=oscArrowToken_) ||
                musicMode_.IsMusic()!=oscArrowMusic_ || fullscreen_!=oscArrowFullscreen_ ||
                static_cast<std::int64_t>(AppWindow().Presenter().Kind())!=oscArrowPresenter_ ||
                preferences_.Integer("oscPosition")!=oscArrowPosition_ || !root || root.RasterizationScale()!=oscArrowScale_ ||
                origin.X!=oscArrowOrigin_.X || origin.Y!=oscArrowOrigin_.Y ||
                button.Template()!=oscArrowTemplate_ || button.ActualWidth()!=oscArrowWidth_ || button.ActualHeight()!=oscArrowHeight_) CancelOscArrow();
        }
        const bool available=OscArrowAvailable();
        bool enabled=available && (mode!=0 || (s.speedObserved && (!session_->SpeedHoldBusy() || oscArrowPointer_ || oscArrowToken_))) &&
            (mode!=2 || s.seekable);
        for(auto direction:{-1,1}) {
            auto b=direction<0?OscLeftButton():OscRightButton();
            hstring label=mode==1?(direction<0?L"上一项":L"下一项"):
                mode==2?(direction<0?L"后退10秒":L"前进10秒"):(direction<0?L"减速":L"加速");
            if(unbox_value_or<hstring>(b.Content(),L"")!=label) b.Content(box_value(label));
            if(Automation::AutomationProperties::GetName(b)!=label) Automation::AutomationProperties::SetName(b,label);
            hstring tip=mode==0?label+L"；短按保留档位，首档长按释放回到1倍；取消不回滚":label;
            if(unbox_value_or<hstring>(Controls::ToolTipService::GetToolTip(b),L"")!=tip)
                Controls::ToolTipService::SetToolTip(b,box_value(tip));
            if(b.IsEnabled()!=enabled) b.IsEnabled(enabled);
        }
        hstring speed=mode==0 && s.speedObserved && std::isfinite(s.speed)?hstring{std::format(L"{:.2f}×",s.speed)}:L"";
        if(OscArrowSpeed().Text()!=speed) OscArrowSpeed().Text(speed);
    } catch(...) {CancelOscArrow();}
}
void MainWindow::WireOscArrows() {
    auto weak=get_weak();
    for(auto direction:{-1,1}) {
        auto button=direction<0?OscLeftButton():OscRightButton();
        button.AddHandler(UIElement::PointerPressedEvent(),box_value(Input::PointerEventHandler([weak,direction](auto const& sender,Input::PointerRoutedEventArgs const& args) {
            auto self=weak.get();if(!self)return;
            auto b=sender.template as<Controls::Button>();
            auto point=args.GetCurrentPoint(b);
            if(!point.Properties().IsLeftButtonPressed() || args.KeyModifiers()!=Windows::System::VirtualKeyModifiers::None) return;
            args.Handled(true);
            if(self->oscArrowToken_ && !self->oscArrowPointer_) return; // Pending release owns the slot.
            self->oscArrowSuppressClick_=true;
            if(self->oscArrowPointer_) return;
            if(!self->OscArrowAvailable() || !b.IsEnabled()) {self->CancelOscArrow();return;}
            try {
                auto root=self->Root().XamlRoot();if(!root){self->CancelOscArrow();return;}
                ++self->oscArrowEpoch_;
                self->oscArrowDirection_=direction;self->oscArrowPointerId_=args.Pointer().PointerId();
                auto const& s=self->session_->Snapshot();
                self->oscArrowGeneration_=s.mediaGeneration;self->oscArrowIntent_=s.mediaOpenIntentRevision;
                self->oscArrowInput_=self->inputGeneration_;self->oscArrowMode_=self->preferences_.Integer("arrowBtnAction");
                self->oscArrowMusic_=self->musicMode_.IsMusic();self->oscArrowFullscreen_=self->fullscreen_;
                self->oscArrowPresenter_=static_cast<std::int64_t>(self->AppWindow().Presenter().Kind());
                self->oscArrowPosition_=self->preferences_.Integer("oscPosition");
                self->oscArrowTemplate_=b.Template();
                self->oscArrowOrigin_=b.TransformToVisual(self->Root()).TransformPoint({0,0});
                self->oscArrowWidth_=b.ActualWidth();self->oscArrowHeight_=b.ActualHeight();self->oscArrowScale_=root.RasterizationScale();
                self->oscArrowPointer_=true;
                if(!b.CapturePointer(args.Pointer())) {self->CancelOscArrow();return;}
                if(self->oscArrowMode_==0) {
                    self->oscArrowStarting_=true;
                    self->oscArrowToken_=self->session_->BeginSpeedHold(direction);
                    self->oscArrowStarting_=false;
                    if(!self->oscArrowToken_ || !self->oscArrowPointer_) {self->CancelOscArrow();return;}
                }
                self->UpdateOscArrows();self->ControlBarActivity();
            } catch(...) {self->CancelOscArrow();}
        })),true);
        button.AddHandler(UIElement::PointerReleasedEvent(),box_value(Input::PointerEventHandler([weak](auto const& sender,Input::PointerRoutedEventArgs const& args) {
            auto self=weak.get();if(!self || !self->oscArrowPointer_ || args.Pointer().PointerId()!=self->oscArrowPointerId_)return;
            args.Handled(true);self->UpdateOscArrows();if(!self->oscArrowPointer_)return;
            auto b=sender.template as<Controls::Button>();auto p=args.GetCurrentPoint(b).Position();
            if(p.X<0 || p.Y<0 || p.X>b.ActualWidth() || p.Y>b.ActualHeight()) {self->CancelOscArrow();return;}
            const auto token=self->oscArrowToken_;const auto direction=self->oscArrowDirection_;
            self->oscArrowPointer_=false; // Retain ownership while Core drains/rechecks the release.
            b.ReleasePointerCaptures();
            if(token)self->session_->EndSpeedHold(token);else self->InvokeOscArrow(direction);
            self->UpdateOscArrows();
            self->DispatcherQueue().TryEnqueue([weak] {if(auto current=weak.get();current && !current->oscArrowPointer_)current->oscArrowSuppressClick_=false;});
        })),true);
        auto cancel=[weak](auto const&,Input::PointerRoutedEventArgs const& args) {
            if(auto self=weak.get();self && self->oscArrowPointer_ && args.Pointer().PointerId()==self->oscArrowPointerId_) self->CancelOscArrow();
        };
        button.PointerCanceled(cancel);
        button.PointerCaptureLost([weak](auto const&,Input::PointerRoutedEventArgs const& args) {
            // Button may release native capture before routed PointerReleased.
            // Only capture loss without the matching release cancels the lease.
            if(auto self=weak.get();self && self->oscArrowPointer_ && args.Pointer().PointerId()==self->oscArrowPointerId_) {
                const auto id=self->oscArrowPointerId_;const auto epoch=self->oscArrowEpoch_;
                self->DispatcherQueue().TryEnqueue([weak,id,epoch] {if(auto current=weak.get();current && current->oscArrowEpoch_==epoch && current->oscArrowPointer_ && current->oscArrowPointerId_==id)current->CancelOscArrow();});
            }
        });
        button.LostFocus([weak](auto const&,auto const&) {if(auto self=weak.get())self->CancelOscArrow();});
        button.Unloaded([weak](auto const&,auto const&) {if(auto self=weak.get())self->CancelOscArrow();});
        button.PreviewKeyDown([weak,direction](auto const&,Input::KeyRoutedEventArgs const& args) {
            if(args.Key()!=Windows::System::VirtualKey::Space && args.Key()!=Windows::System::VirtualKey::Enter)return;
            args.Handled(true);if(auto self=weak.get();self && !args.KeyStatus().WasKeyDown) {
                self->oscArrowSuppressClick_=true;self->InvokeOscArrow(direction);
                self->DispatcherQueue().TryEnqueue([weak] {if(auto current=weak.get();current && !current->oscArrowPointer_)current->oscArrowSuppressClick_=false;});
            }
        });
        button.PreviewKeyUp([](auto const&,Input::KeyRoutedEventArgs const& args) {
            if(args.Key()==Windows::System::VirtualKey::Space || args.Key()==Windows::System::VirtualKey::Enter)args.Handled(true);
        });
        button.Click([weak,direction](auto const&,auto const&) {
            if(auto self=weak.get();self && !self->oscArrowSuppressClick_)self->InvokeOscArrow(direction);
        });
    }
    Root().LayoutUpdated([weak](auto const&,auto const&) {if(auto self=weak.get())self->UpdateOscArrows();});
}
}

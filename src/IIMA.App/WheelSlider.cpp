#include "pch.h"
#include "WheelSlider.h"
#include <cmath>
#include "WheelSlider.g.cpp"
namespace winrt::IIMA::implementation {
void WheelSlider::OnPointerWheelChanged(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
    const bool handled=args.Handled();
    args.Handled(true);
    // Never call base: that would mutate Value optimistically and invoke the
    // existing absolute-seek/volume ValueChanged path with an obsolete value.
    // Native pointer/keyboard behavior is preserved, with tracking hooks below.
    if(!handled) wheelInput_(*this,args);
}
winrt::event_token WheelSlider::WheelInput(Windows::Foundation::EventHandler<Microsoft::UI::Xaml::Input::PointerRoutedEventArgs> const& handler) {
    return wheelInput_.add(handler);
}
void WheelSlider::WheelInput(winrt::event_token const& token) noexcept {wheelInput_.remove(token);}
}

// IINA PlaySliderCell tracking adapted to WinUI's native track and template Thumb.
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.Devices.Input.h>
namespace winrt::IIMA::implementation {
WheelSlider::WheelSlider() {
    auto weak=get_weak();
    using namespace Microsoft::UI::Xaml;
    AddHandler(UIElement::PointerPressedEvent(),Input::PointerEventHandler([weak](auto const&,auto const& args) {
        if(auto self=weak.get();self && self->tracking_ && !self->trackingPointerId_) self->trackingPointerId_=args.Pointer().PointerId();
    }),true);
    AddHandler(UIElement::PointerReleasedEvent(),Input::PointerEventHandler([weak](auto const&,auto const& args) {
        if(auto self=weak.get();self && self->trackingPointerId_==args.Pointer().PointerId()) self->QueueTrackingEnd(false);
    }),true);
    AddHandler(UIElement::PointerCanceledEvent(),Input::PointerEventHandler([weak](auto const&,auto const& args) {
        if(auto self=weak.get();self && (!self->trackingPointerId_ || self->trackingPointerId_==args.Pointer().PointerId())) self->CancelTracking();
    }),true);
    AddHandler(UIElement::PointerCaptureLostEvent(),Input::PointerEventHandler([weak](auto const&,auto const& args) {
        if(auto self=weak.get();self && self->trackingPointerId_==args.Pointer().PointerId()) self->QueueTrackingEnd(true);
    }),true);
    Unloaded([weak](auto const&,auto const&) {if(auto self=weak.get()) self->CancelTracking();});
}
void WheelSlider::OnApplyTemplate() {
    ++templateRevision_;
    CancelTracking();
    for(auto const& hooks:thumbs_) {hooks.thumb.DragStarted(hooks.started);hooks.thumb.DragCompleted(hooks.completed);}
    thumbs_.clear();
    WheelSliderT<WheelSlider>::OnApplyTemplate();
    horizontalTrack_=GetTemplateChild(L"HorizontalTemplate").try_as<Microsoft::UI::Xaml::FrameworkElement>();
    verticalTrack_=GetTemplateChild(L"VerticalTemplate").try_as<Microsoft::UI::Xaml::FrameworkElement>();
    auto weak=get_weak();
    for(auto name:{L"HorizontalThumb",L"VerticalThumb"}) {
        auto thumb=GetTemplateChild(name).try_as<Microsoft::UI::Xaml::Controls::Primitives::Thumb>();
        if(!thumb) continue;
        ThumbHooks hooks;hooks.thumb=thumb;
        hooks.started=thumb.DragStarted([weak](auto const& sender,auto const&) {
            if(auto self=weak.get();self && !self->tracking_) {
                auto captured=sender.template as<Microsoft::UI::Xaml::Controls::Primitives::Thumb>().PointerCaptures();
                if(captured && captured.Size()==1) self->trackingPointerId_=captured.GetAt(0).PointerId();
                self->BeginTracking();
            }
        });
        hooks.completed=thumb.DragCompleted([weak](auto const&,auto const& args) {
            if(auto self=weak.get()) {if(args.Canceled()) self->CancelTracking();else self->QueueTrackingEnd(false);}
        });
        thumbs_.push_back(std::move(hooks));
    }
}
void WheelSlider::OnPointerPressed(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
    if(tracking_ && trackingPointerId_ && *trackingPointerId_!=args.Pointer().PointerId()) {args.Handled(true);return;}
    if(trackingEnabled_ && IsEnabled() && !args.Handled()) {
        auto point=args.GetCurrentPoint(*this);
        const bool primary=point.PointerDevice().PointerDeviceType()!=Windows::Devices::Input::PointerDeviceType::Mouse || point.Properties().IsLeftButtonPressed();
        auto track=Orientation()==Microsoft::UI::Xaml::Controls::Orientation::Horizontal ? horizontalTrack_ : verticalTrack_;
        if(primary && point.IsInContact() && track) {
            auto local=args.GetCurrentPoint(track).Position();
            if(local.X>=0 && local.Y>=0 && local.X<=track.ActualWidth() && local.Y<=track.ActualHeight()) {
                if(!tracking_) trackingPointerId_=args.Pointer().PointerId();
                BeginTracking();
                if(!tracking_) {args.Handled(true);return;} // Owner rejected the gesture before native ValueChanged.
            }
        }
    }
    WheelSliderT<WheelSlider>::OnPointerPressed(args);
}
void WheelSlider::OnPointerReleased(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
    if(trackingPointerId_==args.Pointer().PointerId()) QueueTrackingEnd(false); // Before base releases capture.
    WheelSliderT<WheelSlider>::OnPointerReleased(args);
}
void WheelSlider::SetTrackingEnabled(bool enabled) {if(!enabled) CancelTracking();trackingEnabled_=enabled;}
void WheelSlider::BeginTracking() {
    if(!trackingEnabled_ || tracking_ || !IsEnabled()) return;
    tracking_=true;endQueued_=false;++trackingEpoch_;
    trackingStarted_(*this,true);
}
void WheelSlider::QueueTrackingEnd(bool cancelled) {
    if(!tracking_) return;
    if(endQueued_) {endCancelled_=endCancelled_ && cancelled;return;}
    endQueued_=true;endCancelled_=cancelled;
    auto weak=get_weak();const auto epoch=trackingEpoch_;
    // Capture loss can occur inside native release. Drain this input dispatch
    // before committing, so final ValueChanged runs before the one end event.
    if(!DispatcherQueue().TryEnqueue([weak,epoch] {
        if(auto self=weak.get();self && self->tracking_ && self->trackingEpoch_==epoch) {
            // A Slider can transfer capture between its rail and template Thumb.
            // Provisional loss is not cancellation if that same pointer is still
            // captured by another native part after dispatch completes.
            if(self->endCancelled_ && self->trackingPointerId_) {
                auto captured=[&](auto const& element) {
                    auto pointers=element.PointerCaptures();
                    if(pointers) for(auto const& pointer:pointers)
                        if(pointer.PointerId()==*self->trackingPointerId_) return true;
                    return false;
                };
                bool owned=captured(*self);
                for(auto const& hooks:self->thumbs_) owned=owned || captured(hooks.thumb);
                if(owned) {self->endQueued_=false;return;}
            }
            const bool cancel=self->endCancelled_;self->tracking_=false;self->endQueued_=false;self->trackingPointerId_.reset();
            self->trackingEnded_(*self,cancel);
        }
    })) CancelTracking();
}
void WheelSlider::CancelTracking() {
    if(!tracking_) return;
    tracking_=false;endQueued_=false;trackingPointerId_.reset();++trackingEpoch_;
    for(auto const& hooks:thumbs_) hooks.thumb.CancelDrag();
    ReleasePointerCaptures();
    trackingEnded_(*this,true);
}
winrt::event_token WheelSlider::TrackingStarted(Windows::Foundation::EventHandler<bool> const& handler) {return trackingStarted_.add(handler);}
void WheelSlider::TrackingStarted(winrt::event_token const& token) noexcept {trackingStarted_.remove(token);}
winrt::event_token WheelSlider::TrackingEnded(Windows::Foundation::EventHandler<bool> const& handler) {return trackingEnded_.add(handler);}
void WheelSlider::TrackingEnded(winrt::event_token const& token) noexcept {trackingEnded_.remove(token);}
}

namespace winrt::IIMA::implementation {
std::optional<std::pair<double,double>> WheelSlider::HorizontalTravel() {
    try {
    if(!horizontalTrack_ || Orientation()!=Microsoft::UI::Xaml::Controls::Orientation::Horizontal) return {};
    for(auto const& hooks:thumbs_) if(hooks.thumb.Name()==L"HorizontalThumb") {
        const auto width=horizontalTrack_.ActualWidth(), thumb=hooks.thumb.ActualWidth();
        if(!std::isfinite(width) || !std::isfinite(thumb) || thumb<=0 || width<=thumb) return {};
        const auto origin=horizontalTrack_.TransformToVisual(*this).TransformPoint({0,0});
        if(!std::isfinite(origin.X)) return {};
        double start=origin.X+thumb/2, travel=width-thumb;
        if(IsDirectionReversed() != (FlowDirection()==Microsoft::UI::Xaml::FlowDirection::RightToLeft)) {start+=travel;travel=-travel;}
        return std::pair{start,travel};
    }
    return {};
    } catch(...) { return {}; }
}
}

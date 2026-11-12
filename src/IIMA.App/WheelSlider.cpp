#include "pch.h"
#include "WheelSlider.h"
#include "WheelSlider.g.cpp"
namespace winrt::IIMA::implementation {
void WheelSlider::OnPointerWheelChanged(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
    const bool handled=args.Handled();
    args.Handled(true);
    // Never call base: that would mutate Value optimistically and invoke the
    // existing absolute-seek/volume ValueChanged path with an obsolete value.
    // Other pointer, touch and keyboard overrides remain entirely native.
    if(!handled) wheelInput_(*this,args);
}
winrt::event_token WheelSlider::WheelInput(Windows::Foundation::EventHandler<Microsoft::UI::Xaml::Input::PointerRoutedEventArgs> const& handler) {
    return wheelInput_.add(handler);
}
void WheelSlider::WheelInput(winrt::event_token const& token) noexcept {wheelInput_.remove(token);}
}

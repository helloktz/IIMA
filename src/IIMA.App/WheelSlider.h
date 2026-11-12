#pragma once
// IINA PlaySlider/VolumeSlider scrollWheel guards, Windows typed-wheel adapter.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "WheelSlider.g.h"
#include <winrt/Microsoft.UI.Xaml.Input.h>
namespace winrt::IIMA::implementation {
struct WheelSlider : WheelSliderT<WheelSlider> {
    WheelSlider() = default;
    // UIElement's composable overridable hook executes instead of the Slider
    // implementation. An instance PointerWheelChanged handler is too late.
    void OnPointerWheelChanged(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    winrt::event_token WheelInput(Windows::Foundation::EventHandler<Microsoft::UI::Xaml::Input::PointerRoutedEventArgs> const& handler);
    void WheelInput(winrt::event_token const& token) noexcept;
private:
    winrt::event<Windows::Foundation::EventHandler<Microsoft::UI::Xaml::Input::PointerRoutedEventArgs>> wheelInput_;
};
}
namespace winrt::IIMA::factory_implementation {
struct WheelSlider : WheelSliderT<WheelSlider, implementation::WheelSlider> {};
}

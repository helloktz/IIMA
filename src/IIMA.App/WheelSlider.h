#pragma once
// IINA PlaySlider/VolumeSlider scrollWheel guards, Windows typed-wheel adapter.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "WheelSlider.g.h"
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <vector>
#include <cstdint>
#include <optional>
#include <utility>
namespace winrt::IIMA::implementation {
struct WheelSlider : WheelSliderT<WheelSlider> {
    WheelSlider();
    void OnApplyTemplate();
    void OnPointerPressed(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    void OnPointerReleased(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    void SetTrackingEnabled(bool enabled);
    bool Tracking() const noexcept { return tracking_; }
    void CancelTracking();
    std::optional<std::pair<double,double>> HorizontalTravel();
    std::uint64_t TemplateRevision() const noexcept {return templateRevision_;}
    winrt::event_token TrackingStarted(Windows::Foundation::EventHandler<bool> const& handler);
    void TrackingStarted(winrt::event_token const& token) noexcept;
    winrt::event_token TrackingEnded(Windows::Foundation::EventHandler<bool> const& handler);
    void TrackingEnded(winrt::event_token const& token) noexcept;
    // UIElement's composable overridable hook executes instead of the Slider
    // implementation. An instance PointerWheelChanged handler is too late.
    void OnPointerWheelChanged(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
    winrt::event_token WheelInput(Windows::Foundation::EventHandler<Microsoft::UI::Xaml::Input::PointerRoutedEventArgs> const& handler);
    void WheelInput(winrt::event_token const& token) noexcept;
private:
    void BeginTracking();
    void QueueTrackingEnd(bool cancelled);
    bool trackingEnabled_{}, tracking_{}, endQueued_{}, endCancelled_{};
    std::uint64_t trackingEpoch_{};
    std::uint64_t templateRevision_{};
    std::optional<std::uint32_t> trackingPointerId_;
    Microsoft::UI::Xaml::FrameworkElement horizontalTrack_{nullptr}, verticalTrack_{nullptr};
    struct ThumbHooks {
        Microsoft::UI::Xaml::Controls::Primitives::Thumb thumb{nullptr};
        winrt::event_token started{}, completed{};
    };
    std::vector<ThumbHooks> thumbs_;
    winrt::event<Windows::Foundation::EventHandler<bool>> trackingStarted_, trackingEnded_;
    winrt::event<Windows::Foundation::EventHandler<Microsoft::UI::Xaml::Input::PointerRoutedEventArgs>> wheelInput_;
};
}
namespace winrt::IIMA::factory_implementation {
struct WheelSlider : WheelSliderT<WheelSlider, implementation::WheelSlider> {};
}

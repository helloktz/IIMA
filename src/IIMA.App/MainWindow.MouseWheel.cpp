// IINA PlayerWindowController.scrollWheel and AppData wheel maps.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.System.h>
#include <array>
#include <algorithm>
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireMouseWheel() {
    WireSliderWheels();
    auto weak = get_weak();
    // Register on the video surface only. ScrollViewer, editor and
    // playlist events keep their native behavior; the two playback sliders use
    // a separate preemptive subclass. No root handled-events hook.
    VideoPanel().PointerWheelChanged([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        auto self = weak.get();
        if (!self || self->closing_ || self->pickerOpen_ || args.Handled() ||
            !self->KeyboardPlaybackAllowed()) return;
        if (args.KeyModifiers() != Windows::System::VirtualKeyModifiers::None) return;
        auto const& state = self->session_->Snapshot();
        if (state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded) return;
        auto properties = args.GetCurrentPoint(self->VideoPanel()).Properties();
        auto delta = properties.MouseWheelDelta();
        if (delta == 0) return;
        // Windows delivers signed units of WHEEL_DELTA (120); high-resolution
        // wheels may send fractions. Do not import Cocoa pixel/phase heuristics.
        // Positive vertical means away/up; positive horizontal means right.
        const double steps = std::clamp(static_cast<double>(delta) / 120.0, -32.0, 32.0);
        const bool horizontal = properties.IsHorizontalMouseWheel();
        const double amount = steps * (horizontal ? 1.0 : 2.0);
        auto action = self->preferences_.Integer(horizontal ?
            "horizontalScrollAction" : "verticalScrollAction");
        switch (action) {
        case 0:
            // Native relative add avoids losing rapid ticks while waiting for
            // property-change events. mpv's volume-max remains authoritative.
            self->session_->AdjustVolumeByWheel(amount);
            args.Handled(true);
            break;
        case 1: {
            if (!state.seekable) return;
            constexpr std::array<double, 5> amounts{0, 0.5, 1, 2, 4};
            auto level = std::clamp<std::int64_t>(self->preferences_.Integer("relativeSeekAmount"), 1, 4);
            auto choice = self->preferences_.Integer("useExactSeek");
            auto mode = self->preferences_.Boolean("wheelExactSeek") || choice == 1
                ? iima::RelativeSeekMode::Exact : choice == 2
                ? iima::RelativeSeekMode::Automatic : iima::RelativeSeekMode::Relative;
            self->session_->SeekRelativeWithMode(amount * amounts[static_cast<std::size_t>(level)],mode);
            args.Handled(true);
            break;
        }
        case 4: {
            constexpr std::array<double, 5> amounts{0, 0.001, 0.002, 0.005, 0.01};
            auto level = std::clamp<std::int64_t>(self->preferences_.Integer("playbackSpeedScrollAmount"), 1, 4);
            if (!std::isfinite(state.speed) || state.speed <= 0) return;
            // Same observed-speed formula, bounds and millisecond rounding as
            // IINA; never optimistically rewrite the playback snapshot.
            double target = std::round(1000 * std::clamp(state.speed + state.speed *
                amounts[static_cast<std::size_t>(level)] * amount, 0.05, 4.0)) / 1000;
            self->session_->SetSpeed(target);
            args.Handled(true);
            break;
        }
        default: break; // none, or an unsupported restored choice.
        }
    });
}
}

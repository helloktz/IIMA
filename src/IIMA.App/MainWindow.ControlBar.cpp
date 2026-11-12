// IINA MainWindowController showUI/hideUI/createTimer/mouseMoved adaptation.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <chrono>
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
namespace {
bool DescendantOf(DependencyObject child, DependencyObject const& parent) {
    for (auto node = child; node; node = Media::VisualTreeHelper::GetParent(node))
        if (node == parent) return true;
    return false;
}
}
void MainWindow::WireControlBarVisibility() {
    controlBarTimer_ = DispatcherQueue().CreateTimer();
    controlBarTimer_.IsRepeating(false);
    auto weak = get_weak();
    controlBarTimer_.Tick([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->CanHideControlBar()) {
            self->BottomControlsBorder().Visibility(Visibility::Collapsed);
            self->CancelThumbnailPreview();
        }
    });
    Root().PointerMoved([weak](auto const&, Input::PointerRoutedEventArgs const&) {
        if (auto self = weak.get()) self->ControlBarActivity();
    });
    Root().PointerPressed([weak](auto const&, Input::PointerRoutedEventArgs const&) {
        if (auto self = weak.get()) self->ControlBarActivity();
    });
    Root().PreviewKeyDown([weak](auto const&, Input::KeyRoutedEventArgs const&) {
        if (auto self = weak.get()) self->ControlBarActivity();
    });
    BottomControlsBorder().PointerEntered([weak](auto const&, Input::PointerRoutedEventArgs const&) {
        if (auto self = weak.get()) {
            self->controlBarHovered_ = true; self->ControlBarActivity();
        }
    });
    BottomControlsBorder().PointerExited([weak](auto const&, Input::PointerRoutedEventArgs const&) {
        if (auto self = weak.get()) {
            self->controlBarHovered_ = false; self->UpdateControlBarPolicy();
        }
    });
    BottomControlsBorder().GotFocus([weak](auto const&, RoutedEventArgs const&) {
        if (auto self = weak.get()) self->ControlBarActivity();
    });
    BottomControlsBorder().LostFocus([weak](auto const&, RoutedEventArgs const&) {
        if (auto self = weak.get()) {
            // Wait until FocusManager has committed the replacement focus.
            self->DispatcherQueue().TryEnqueue([weak] {
                if (auto current = weak.get()) current->UpdateControlBarPolicy();
            });
        }
    });
    Activated([weak](auto const&, WindowActivatedEventArgs const& args) {
        if (auto self = weak.get()) {
            self->controlBarInactive_ = args.WindowActivationState() == WindowActivationState::Deactivated;
            self->ControlBarActivity();
        }
    });
}
bool MainWindow::CanHideControlBar() {
    if (closing_ || pickerOpen_ || controlBarInactive_ || controlBarHovered_ ||
        !preferences_.Boolean("enableControlBarAutoHide") ||
        AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay)
        return false;
    auto const& state = session_->Snapshot();
    if (state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded)
        return false;
    auto focused = Input::FocusManager::GetFocusedElement(Root().XamlRoot()).try_as<DependencyObject>();
    return !DescendantOf(focused, BottomControlsBorder());
}
void MainWindow::ControlBarActivity() {
    if (closing_) return;
    if (controlBarTimer_) controlBarTimer_.Stop();
    if (AppWindow().Presenter().Kind() != Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay)
        BottomControlsBorder().Visibility(Visibility::Visible);
    UpdateControlBarPolicy();
}
void MainWindow::UpdateControlBarPolicy() {
    if (!controlBarTimer_ || closing_) return;
    UpdateBufferingIndicator(session_->Snapshot());
    const bool compact = AppWindow().Presenter().Kind() ==
        Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay;
    if (compact || interactiveCrop_.Active() || cropOriginal_) {
        controlBarTimer_.Stop(); controlBarHovered_ = false;
        BottomControlsBorder().Visibility(Visibility::Collapsed); return;
    }
    if (!CanHideControlBar()) {
        controlBarTimer_.Stop(); BottomControlsBorder().Visibility(Visibility::Visible); return;
    }
    auto timeout = preferences_.Number("controlBarAutoHideTimeout");
    // WinUI disallows a zero timer interval. Zero means next-dispatch hide,
    // subject to the same hover/focus/lifecycle gates, not a busy loop.
    auto interval = std::chrono::milliseconds(static_cast<long long>(std::max(1.0, timeout * 1000)));
    if (controlBarTimer_.Interval() != interval) {
        controlBarTimer_.Stop(); controlBarTimer_.Interval(interval);
    }
    // Property notifications must not perpetually postpone the idle deadline.
    if (BottomControlsBorder().Visibility() == Visibility::Visible && !controlBarTimer_.IsRunning())
        controlBarTimer_.Start();
}
}

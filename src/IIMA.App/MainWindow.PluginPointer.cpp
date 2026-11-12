// IINA JavascriptAPIInput / PlayerWindowController mouse event adaptation.
// Copyright (c) 2023 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <array>
#include <cmath>
#include <stdexcept>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WirePluginPointer() {
    auto weak = get_weak();
    VideoPanel().PointerPressed([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        if (auto self = weak.get()) self->PluginPointerEvent(args, 0);
    });
    VideoPanel().PointerReleased([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        if (auto self = weak.get()) self->PluginPointerEvent(args, 1);
    });
    VideoPanel().PointerMoved([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        if (auto self = weak.get()) self->PluginPointerEvent(args, 2);
    });
    auto cancel = [weak](auto const&, Input::PointerRoutedEventArgs const&) {
        if (auto self = weak.get()) self->pluginPointer_.Reset();
    };
    VideoPanel().PointerCanceled(cancel);
    VideoPanel().PointerCaptureLost(cancel);
    VideoPanel().PointerExited(cancel);
    Activated([weak](auto const&, WindowActivatedEventArgs const& args) {
        if (auto self = weak.get(); self && args.WindowActivationState() == WindowActivationState::Deactivated)
            self->pluginPointer_.Reset();
    });
}
void MainWindow::PluginPointerEvent(Input::PointerRoutedEventArgs const& args, unsigned event) {
    // Events belong to the video panel, not editor/sidebar/native controls. Do
    // not capture the mouse: plugins may observe but cannot interfere with UI.
    if (closing_ || !pluginRuntime_ || pickerOpen_ || args.Handled() || !KeyboardPlaybackAllowed() ||
        args.Pointer().PointerDeviceType() != Windows::Devices::Input::PointerDeviceType::Mouse) {
        pluginPointer_.Reset(); return;
    }
    try {
        auto point = args.GetCurrentPoint(Root());
        auto position = point.Position();
        const double x = position.X, y = Root().ActualHeight() - position.Y;
        auto properties = point.Properties();
        const double pressure = properties.Pressure();
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(pressure)) return;
        auto id = point.PointerId();
        auto now = point.Timestamp() / 1000; // Windows pointer timestamp is microseconds.
        constexpr std::array names{"*mouse", "*rightMouse", "*otherMouse"};
        auto publish = [&](unsigned button, char const* kind, unsigned count) {
            if (!pluginRuntime_->PublishMouseInput(names[button], kind, x, y, count, pressure))
                throw std::runtime_error("Plugin mouse event queue unavailable.");
        };
        using Update = Windows::UI::Input::PointerUpdateKind;
        auto update = properties.PointerUpdateKind();
        std::optional<unsigned> button;
        if (event == 0) {
            if (update == Update::LeftButtonPressed) button = 0;
            else if (update == Update::RightButtonPressed) button = 1;
            else if (update == Update::MiddleButtonPressed) button = 2;
            if (!button) return; // X buttons are not silently mapped to middle.
            HWND hwnd{}; check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
            auto dpi = GetDpiForWindow(hwnd);
            double scale = Root().XamlRoot().RasterizationScale();
            if (!dpi || !std::isfinite(scale) || scale <= 0) return;
            auto count = pluginPointer_.Press(*button, id, now, x, y, GetDoubleClickTime(),
                GetSystemMetricsForDpi(SM_CXDOUBLECLK, dpi) / scale,
                GetSystemMetricsForDpi(SM_CYDOUBLECLK, dpi) / scale);
            publish(*button, "down", count);
        } else if (event == 1) {
            if (update == Update::LeftButtonReleased) button = 0;
            else if (update == Update::RightButtonReleased) button = 1;
            else if (update == Update::MiddleButtonReleased) button = 2;
            if (button) if (auto count = pluginPointer_.Release(*button, id)) publish(*button, "up", *count);
        } else {
            const std::array pressed{properties.IsLeftButtonPressed(), properties.IsRightButtonPressed(), properties.IsMiddleButtonPressed()};
            for (unsigned i = 0; i < pressed.size(); ++i) if (pressed[i]) {
                // At most ~30Hz per button; raw pointer coalescing is not exposed
                // as a reliable all-samples event stream to the JS worker.
                if (auto count = pluginPointer_.Drag(i, id, now)) publish(i, "drag", *count);
            }
        }
    } catch (...) {
        StopLocalPlugin();
        PluginRuntimeStatus().Text(L"无法完整投递插件鼠标事件，插件已停止。");
    }
}
}

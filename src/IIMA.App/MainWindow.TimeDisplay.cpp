// IINA DurationDisplayTextField.updateText/switchMode/setPrecision and
// PlayerCore remaining-time selection. Copyright (c) 2017 lhc and contributors.
// GPL-3.0; references/iina/LICENSE and references/iina-source.txt.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/VideoTime.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireTimeDisplay() {
    auto weak = get_weak();
    TimeModeButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && self->userDataLoaded_) {
            self->ApplyAdvancedSetting("showRemainingTime", !self->preferences_.Boolean("showRemainingTime"));
        }
    });
    Controls::MenuFlyout menu;
    for (int precision = 0; precision <= 3; ++precision) {
        Controls::MenuFlyoutItem item;
        constexpr wchar_t const* labels[]{L"时间精度：1秒", L"时间精度：100毫秒", L"时间精度：10毫秒", L"时间精度：1毫秒"};
        item.Text(labels[precision]);
        item.Click([weak, precision](auto const&, auto const&) {
            if (auto self = weak.get(); self && !self->closing_ && self->userDataLoaded_)
                self->ApplyAdvancedSetting("timeDisplayPrecision", static_cast<std::int64_t>(precision));
        });
        menu.Items().Append(item);
    }
    TimeModeButton().ContextFlyout(menu);
}
void MainWindow::UpdateTimeDisplay(iima::PlaybackSnapshot const& state) {
    const auto precision = static_cast<unsigned>(std::clamp<std::int64_t>(
        preferences_.Integer("timeDisplayPrecision"), 0, 3));
    const bool loaded = state.media == iima::MediaState::Loaded || state.media == iima::MediaState::Ended;
    auto format = [precision, loaded](std::optional<double> seconds) -> hstring {
        if (!loaded || !seconds || !std::isfinite(*seconds) || *seconds < 0) return L"--:--";
        return to_hstring(iima::VideoTime(*seconds).StringRepresentation(precision));
    };
    TimeLabel().Text(format(state.position));
    const bool remaining = preferences_.Boolean("showRemainingTime");
    const bool scaled = preferences_.Boolean("scaleRemainingTime");
    auto value = remaining ? (scaled ? state.playtimeRemaining : state.timeRemaining) : state.duration;
    // Never derive an estimated end wall-clock time or fill unavailable stream
    // properties with zero. The two remaining properties come from mpv itself.
    auto text = format(value);
    if (remaining && text != L"--:--") text = L"−" + text;
    TimeModeButton().Content(box_value(text));
    auto label = remaining ? (scaled ? L"按播放速度计算的剩余时间" : L"媒体剩余时间") : L"媒体总时长";
    Automation::AutomationProperties::SetName(TimeModeButton(), hstring(label) + L" " + text + L"；点击切换显示模式");
    Controls::ToolTipService::SetToolTip(TimeModeButton(), box_value(hstring(label) +
        L"。点击切换总时长/剩余时间，右击修改精度。"));
}
}

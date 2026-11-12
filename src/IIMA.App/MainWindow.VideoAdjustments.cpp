// SidebarVideoPane.EqualizerView and MPVController property changes, adapted from IINA.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireVideoAdjustments() {
    auto weak = get_weak();
    auto wire = [weak](Controls::Slider slider, Controls::Button reset, std::string property) {
        slider.ValueChanged([weak, property](auto const&, auto const& args) {
            if (auto self = weak.get(); self && !self->closing_ && !self->projectingVideoAdjustments_) {
                auto value = args.NewValue();
                if (std::isfinite(value)) self->session_->SetVideoEqualizer(property, static_cast<int>(std::round(value)));
            }
        });
        reset.Click([weak, property](auto const&, auto const&) {
            if (auto self = weak.get(); self && !self->closing_)
                self->session_->SetVideoEqualizer(property, 0);
        });
    };
    wire(BrightnessSlider(), ResetBrightnessButton(), "brightness");
    wire(ContrastSlider(), ResetContrastButton(), "contrast");
    wire(SaturationSlider(), ResetSaturationButton(), "saturation");
    wire(GammaSlider(), ResetGammaButton(), "gamma");
    wire(HueSlider(), ResetHueButton(), "hue");
    DeinterlaceCheck().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && !self->projectingVideoAdjustments_) {
            auto checked = self->DeinterlaceCheck().IsChecked();
            if (checked) self->session_->SetDeinterlace(checked.Value());
            // Third-state is an observed auto/unknown mode, not a request to guess
            // an automatic backend policy. Restore from authoritative state.
            else self->UpdateVideoAdjustments(self->session_->Snapshot());
        }
    });
    UpdateVideoAdjustments(session_->Snapshot());
}
void MainWindow::UpdateVideoAdjustments(iima::PlaybackSnapshot const& state) {
    struct Guard { bool& value; explicit Guard(bool& v):value(v){value=true;} ~Guard(){value=false;} } guard(projectingVideoAdjustments_);
    bool active = !closing_ && state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded;
    bool complete = true;
    auto project = [&](Controls::Slider slider, Controls::Button reset, std::optional<int> value) {
        slider.IsEnabled(active && value.has_value());
        reset.IsEnabled(active && value.has_value());
        // A disabled zero thumb is not an authoritative zero; status explicitly
        // identifies unavailable controls. No speculative persistence is used.
        slider.Value(value.value_or(0));
        complete = complete && value.has_value();
    };
    project(BrightnessSlider(), ResetBrightnessButton(), state.videoBrightness);
    project(ContrastSlider(), ResetContrastButton(), state.videoContrast);
    project(SaturationSlider(), ResetSaturationButton(), state.videoSaturation);
    project(GammaSlider(), ResetGammaButton(), state.videoGamma);
    project(HueSlider(), ResetHueButton(), state.videoHue);
    DeinterlaceCheck().IsEnabled(active && state.deinterlaceMode.has_value());
    if (state.deinterlace) DeinterlaceCheck().IsChecked(*state.deinterlace);
    else DeinterlaceCheck().IsChecked(nullptr);
    std::wstring status = !active ? L"等待媒体加载。" : complete ? L"数值由播放器反馈；修改后等待实际属性更新。" : L"部分属性暂不可用，对应控件已禁用。";
    if (state.deinterlaceMode == "auto") status += L" 反交错当前为播放器自动模式。";
    else if (!state.deinterlaceMode) status += L" 反交错状态未知。";
    VideoAdjustmentStatus().Text(status);
}
}

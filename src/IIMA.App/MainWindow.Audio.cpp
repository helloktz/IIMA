// Audio equalizer preset actions derived from IINA SidebarAudioPane.
// Copyright © 2024 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireAudioControls() {
    for (auto const& preset : iima::Equalizations::Presets())
        AudioEqualizerBox().Items().Append(box_value(to_hstring(preset.key)));
    AudioEqualizerBox().SelectedIndex(0);
    auto weak = get_weak();
    LoadExternalAudioButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->LoadExternalAudio();
    });
    ApplyAudioEqualizerButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            auto index = self->AudioEqualizerBox().SelectedIndex();
            auto presets = iima::Equalizations::Presets();
            if (index >= 0 && static_cast<std::size_t>(index) < presets.size())
                self->session_->SetAudioEqualizer(presets[index].profile);
        }
    });
    RemoveAudioEqualizerButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->RemoveAudioEqualizer();
    });
}
}

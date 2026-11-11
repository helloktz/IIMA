// Defaults/operations follow IINA Preference.swift, PlayerCore.setLoopMode,
// toggleMute, setAudioDelay, setSubDelay and MPVController.handlePropertyChange.
// Adapted behavior: Copyright © 2016 lhc; GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <cmath>
#include <algorithm>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireSettingsControls()
{
    auto weak = get_weak();
    auto boolean = [weak](Controls::CheckBox const& box, bool iima::UserSettings::*field,
                         void (iima::PlayerSession::*setter)(bool)) {
        auto changed = [weak, field, setter](auto const& sender, auto const&) {
            if (auto self = weak.get(); self && self->userDataLoaded_ && !self->updating_ && !self->closing_) {
                auto value = sender.template as<Controls::CheckBox>().IsChecked();
                if (!value) return;
                self->userData_.settings.*field = value.Value();
                (self->session_.get()->*setter)(value.Value());
                self->MarkUserDataDirty();
            }
        };
        box.Checked(changed); box.Unchecked(changed);
    };
    boolean(PauseWhenOpenCheck(), &iima::UserSettings::pauseWhenOpen, &iima::PlayerSession::SetPauseWhenOpen);
    boolean(AutoPlayNextCheck(), &iima::UserSettings::autoPlayNext, &iima::PlayerSession::SetAutoPlayNext);
    boolean(MuteCheck(), &iima::UserSettings::muted, &iima::PlayerSession::SetMuted);
    boolean(SubtitlesVisibleCheck(), &iima::UserSettings::subtitlesVisible, &iima::PlayerSession::SetSubtitlesVisible);
    auto number = [weak](Controls::NumberBox const& box, double iima::UserSettings::*field,
                        void (iima::PlayerSession::*setter)(double)) {
        box.ValueChanged([weak, field, setter](auto const&, Controls::NumberBoxValueChangedEventArgs const& args) {
            if (auto self = weak.get(); self && self->userDataLoaded_ && !self->updating_ && !self->closing_) {
                if (!std::isfinite(args.NewValue())) return; // Empty/intermediate edit is not a player command.
                self->userData_.settings.*field = args.NewValue();
                if (field == &iima::UserSettings::maxVolume)
                    self->userData_.settings.volume = std::min(self->userData_.settings.volume, args.NewValue());
                (self->session_.get()->*setter)(args.NewValue());
                self->MarkUserDataDirty();
            }
        });
    };
    number(MaxVolumeBox(), &iima::UserSettings::maxVolume, &iima::PlayerSession::SetMaxVolume);
    number(AudioDelayBox(), &iima::UserSettings::audioDelay, &iima::PlayerSession::SetAudioDelay);
    number(SubtitleDelayBox(), &iima::UserSettings::subtitleDelay, &iima::PlayerSession::SetSubtitleDelay);
    for (auto text : {L"不循环", L"单文件循环", L"播放列表循环"}) RepeatModeBox().Items().Append(box_value(text));
    for (auto text : {L"视频与字幕", L"仅视频", L"当前视频输出（不含控件）"}) ScreenshotModeBox().Items().Append(box_value(text));
    RepeatModeBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->updating_ && !self->closing_) {
            auto index = self->RepeatModeBox().SelectedIndex();
            if (index < 0 || index > 2) return;
            auto mode = static_cast<iima::RepeatMode>(index);
            self->userData_.settings.repeatMode = mode;
            self->session_->SetRepeatMode(mode);
            self->MarkUserDataDirty();
        }
    });
    ScreenshotModeBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->userDataLoaded_ && !self->updating_ && !self->closing_) {
            auto index = self->ScreenshotModeBox().SelectedIndex();
            if (index < 0 || index > 2) return;
            auto mode = static_cast<iima::ScreenshotMode>(index);
            self->userData_.settings.screenshotMode = mode;
            self->session_->SetScreenshotMode(mode);
            self->MarkUserDataDirty();
        }
    });
}
void MainWindow::ApplySettingsSnapshot(iima::PlaybackSnapshot const& state)
{
    // Called while updating_ is true. Confirmed mpv values do not generate commands.
    // Keep policy preferences separate from the current file's playback facts.
    if (userDataLoaded_) {
        PauseWhenOpenCheck().IsChecked(userData_.settings.pauseWhenOpen);
        AutoPlayNextCheck().IsChecked(userData_.settings.autoPlayNext);
        ScreenshotModeBox().SelectedIndex(static_cast<int>(userData_.settings.screenshotMode));
    }
    MuteCheck().IsChecked(state.muted);
    SubtitlesVisibleCheck().IsChecked(state.subtitlesVisible);
    RepeatModeBox().SelectedIndex(static_cast<int>(state.repeatMode));
    auto projectNumber = [](Controls::NumberBox const& box, double value) {
        // Do not replace a partially typed number on each time-pos notification.
        if (box.FocusState() == FocusState::Unfocused) box.Value(value);
    };
    projectNumber(MaxVolumeBox(), state.maxVolume);
    projectNumber(AudioDelayBox(), state.audioDelay);
    projectNumber(SubtitleDelayBox(), state.subtitleDelay);
}
}

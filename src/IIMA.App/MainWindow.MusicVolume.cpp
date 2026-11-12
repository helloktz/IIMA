// IINA MiniPlayerWindowController updateVolume / muteButtonAction / volume popover.
// Copyright © 2017 lhc. GPL-3.0; references/iina/LICENSE.
// Windows adaptation: native flyout dismissal and an explicit mute button.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <cmath>
#include <algorithm>
using namespace winrt;
namespace winrt::IIMA::implementation {
bool MainWindow::MusicVolumeAvailable() {
    auto const& s=session_->Snapshot();
    return !closing_ && userDataLoaded_ && musicMode_.IsMusic() && !musicTransition_ && !fullscreen_ &&
        !fullscreenTransition_ && !compactTransition_ && !pickerOpen_ && !inputResolving_ &&
        pendingFiles_.empty() && activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ &&
        !session_->PlaylistEditBusy() && !s.mediaOriginPending &&
        AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped &&
        s.session==iima::SessionState::Ready && s.media==iima::MediaState::Loaded &&
        std::isfinite(s.volume) && std::isfinite(s.maxVolume) && s.maxVolume>0;
}
bool MainWindow::MusicVolumeContextValid() {
    auto const& s=session_->Snapshot();
    return musicVolumeOpen_ && MusicVolumeAvailable() && musicVolumeGeneration_==s.mediaGeneration &&
        musicVolumeIntent_==s.mediaOpenIntentRevision && musicVolumeInput_==inputGeneration_;
}
void MainWindow::CloseMusicVolume() {
    // Invalidate before releasing native capture: late ValueChanged/Closed must
    // not send the previous media's draft to a new entry.
    musicVolumeOpen_=false;
    MusicVolumeSlider().CancelTracking();
    MusicVolumeFlyout().Hide();
}
void MainWindow::WireMusicVolume() {
    auto weak=get_weak();
    MusicVolumeFlyout().Opening([weak](auto const&,auto const&) {
        if(auto self=weak.get()) {
            auto const& s=self->session_->Snapshot();
            self->musicVolumeGeneration_=s.mediaGeneration;
            self->musicVolumeIntent_=s.mediaOpenIntentRevision;
            self->musicVolumeInput_=self->inputGeneration_;
            self->musicVolumeOpen_=self->MusicVolumeAvailable();
            self->CancelMouseClick();
            self->UpdateMusicVolume();
        }
    });
    MusicVolumeFlyout().Opened([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->MusicVolumeContextValid()) self->CloseMusicVolume();
    });
    MusicVolumeFlyout().Closing([weak](auto const&,auto const&) {
        if(auto self=weak.get()) {
            self->musicVolumeOpen_=false;
            self->MusicVolumeSlider().CancelTracking();
        }
    });
    MusicVolumeFlyout().Closed([weak](auto const&,auto const&) {
        if(auto self=weak.get()) {
            self->musicVolumeOpen_=false;
            self->MusicVolumeSlider().CancelTracking();
        }
    });
    MusicVolumeSlider().Unloaded([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && self->musicVolumeOpen_) self->CloseMusicVolume();
    });
    MusicVolumeSlider().SetTrackingEnabled(true);
    MusicVolumeSlider().TrackingStarted([weak](auto const&,bool) {
        if(auto self=weak.get();self && !self->MusicVolumeContextValid()) self->CloseMusicVolume();
    });
    MusicVolumeSlider().TrackingEnded([weak](auto const&,bool cancelled) {
        if(auto self=weak.get();self && !self->closing_) {
            // Close on capture cancellation, so late native ValueChanged from
            // the abandoned gesture cannot become a keyboard-style commit.
            if(cancelled && self->musicVolumeOpen_) { self->CloseMusicVolume(); return; }
            if(!cancelled && !self->musicVolumeUpdating_ && self->MusicVolumeContextValid())
                self->session_->SetVolume(self->MusicVolumeSlider().Value());
            self->UpdateMusicVolume();
        }
    });
    MusicVolumeSlider().ValueChanged([weak](auto const&,auto const& args) {
        if(auto self=weak.get();self && !self->musicVolumeUpdating_ && !self->MusicVolumeSlider().Tracking() && self->MusicVolumeContextValid())
            self->session_->SetVolume(args.NewValue());
    });
    MusicMuteButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && self->MusicVolumeContextValid())
            self->session_->ToggleMute();
    });
}
void MainWindow::UpdateMusicVolume() {
    if(musicVolumeUpdating_) return;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{musicVolumeUpdating_};
    musicVolumeUpdating_=true;
    const bool available=MusicVolumeAvailable();
    MusicVolumeButton().IsEnabled(available);
    if(musicVolumeOpen_ && !MusicVolumeContextValid()) CloseMusicVolume();
    auto const& s=session_->Snapshot();
    const bool valid=MusicVolumeContextValid();
    MusicVolumeSlider().IsEnabled(valid);
    MusicMuteButton().IsEnabled(valid);
    MusicMuteButton().Content(box_value(s.muted ? L"取消静音" : L"静音"));
    const auto value=!available ? hstring(L"不可用") :
        s.volume<0 || s.volume>1000000 ? hstring(L"超出显示范围") :
        to_hstring(static_cast<int>(std::lround(s.volume)))+L"%";
    MusicVolumeButton().Content(box_value(available && s.muted ? L"音量（静音）" : L"音量"));
    MusicVolumeValue().Text(available && s.muted ? value+L" · 静音" : value);
    if(available) {
        if(MusicVolumeSlider().Maximum()!=s.maxVolume) MusicVolumeSlider().CancelTracking();
        MusicVolumeSlider().Maximum(s.maxVolume);
        // Native pointer draft survives ordinary observation ticks. Labels and
        // mute are still observations. Pointer dragging commits only on release;
        // cancelling discards this draft, not any earlier committed command.
        if(!MusicVolumeSlider().Tracking()) MusicVolumeSlider().Value(std::clamp(s.volume,0.0,s.maxVolume));
    }
}
}

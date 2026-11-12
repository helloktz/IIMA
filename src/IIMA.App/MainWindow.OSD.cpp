// IINA OSDMessage / MainWindowController.displayOSD Windows projection.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/OSD/OSDMessage.h"
#include <chrono>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireOSD() {
    osdTimer_ = DispatcherQueue().CreateTimer();
    osdTimer_.IsRepeating(false);
    osdTimer_.Interval(std::chrono::seconds(1)); // Preference.osdAutoHideTimeout
    auto weak = get_weak();
    osdTimer_.Tick([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->OSDBorder().Visibility(Visibility::Collapsed);
    });
}
void MainWindow::UpdateOSD(iima::PlaybackSnapshot const& state) {
    if (state.media != iima::MediaState::Loaded) {
        osdHaveState_ = false;
        OSDBorder().Visibility(Visibility::Collapsed);
        if (osdTimer_) osdTimer_.Stop();
        return;
    }
    iima::OSDMessage message;
    bool changed = false;
    if (osdHaveState_) {
        if (state.paused != osdPaused_) {
            message.kind = state.paused ? iima::OSDKind::Pause : iima::OSDKind::Resume; changed = true;
        } else if (state.volume != osdVolume_) {
            message.kind = iima::OSDKind::Volume; message.value = state.volume; changed = true;
        } else if (state.speed != osdSpeed_) {
            message.kind = iima::OSDKind::Speed; message.value = state.speed; changed = true;
        } else if (state.muted != osdMuted_) {
            message.kind = state.muted ? iima::OSDKind::Mute : iima::OSDKind::Unmute; changed = true;
        }
    }
    osdHaveState_ = true; osdPaused_ = state.paused; osdVolume_ = state.volume;
    osdSpeed_ = state.speed; osdMuted_ = state.muted;
    if (!changed) return;
    iima::OSDPreferences preferences;
    preferences.maximumVolume = state.maxVolume;
    iima::OSDDispatchContext dispatch;
    dispatch.windowLoaded = true; dispatch.playbackActive = true;
    dispatch.miniPlayer = AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay;
    if (!message.ShouldDispatch(preferences, dispatch)) return;
    iima::OSDPlaybackContext context;
    if (state.position) context.position = iima::VideoTime(*state.position);
    if (state.duration) context.duration = iima::VideoTime(*state.duration);
    context.chapterCount = state.chapters.size();
    context.currentChapter = state.chapterIndex.value_or(-1);
    auto presentation = message.Present(preferences, context);
    if (!presentation) return;
    OSDTitle().Text(to_hstring(presentation->title));
    OSDDetail().Text(to_hstring(presentation->detail));
    auto progress = presentation->type == iima::OSDType::WithProgress || presentation->type == iima::OSDType::WithPosition;
    OSDProgress().Visibility(progress ? Visibility::Visible : Visibility::Collapsed);
    OSDProgress().Value(presentation->progress);
    OSDBorder().Visibility(Visibility::Visible);
    osdTimer_.Stop(); osdTimer_.Start();
}
}

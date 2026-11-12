// IINA OSDMessage / MainWindowController.displayOSD Windows projection.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/OSD/OSDMessage.h"
#include <chrono>
#include <algorithm>
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
    if (state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded) {
        playbackOSD_.Reset();
        OSDBorder().Visibility(Visibility::Collapsed);
        if (osdTimer_) osdTimer_.Stop();
        return;
    }
    auto changes = playbackOSD_.Observe(state);
    if (!preferences_.Boolean("enableOSD")) {
        OSDBorder().Visibility(Visibility::Collapsed);
        if (osdTimer_) osdTimer_.Stop();
        return;
    }
    iima::OSDPreferences preferences;
    preferences.maximumVolume = state.maxVolume;
    preferences.disablePauseResume = preferences_.Boolean("disableOSDPauseResumeMsgs");
    preferences.disableSpeed = preferences_.Boolean("disableOSDSpeedMsg");
    iima::OSDDispatchContext dispatch;
    dispatch.windowLoaded = true; dispatch.playbackActive = true;
    dispatch.miniPlayer = AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay;
    if (dispatch.miniPlayer) {
        OSDBorder().Visibility(Visibility::Collapsed);
        if (osdTimer_) osdTimer_.Stop();
        return;
    }
    std::optional<iima::OSDMessage> selected;
    for (auto const& change : changes) if (change.ShouldDispatch(preferences, dispatch)) selected = change;
    if (!selected) return;
    auto const& message = *selected;
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
    osdTimer_.Stop();
    auto timeout = preferences_.Number("osdAutoHideTimeout");
    osdTimer_.Interval(std::chrono::milliseconds(static_cast<long long>(std::max(0.001, timeout) * 1000)));
    osdTimer_.Start();
}
}

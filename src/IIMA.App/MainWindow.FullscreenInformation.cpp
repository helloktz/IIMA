// IINA AdditionalInfoView and MainWindowController fadeable-view policy.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/FullscreenInformation.h"
#include "../IIMA.Core/Network/MediaUrl.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireFullscreenInformation() {
    auto weak=get_weak();
    fullscreenInfoTimer_=DispatcherQueue().CreateTimer();
    fullscreenInfoTimer_.Interval(std::chrono::seconds(1));
    fullscreenInfoTimer_.IsRepeating(true);
    fullscreenInfoTimer_.Tick([weak](auto const&,auto const&) {
        if (auto self=weak.get()) self->UpdateFullscreenInformation(true);
    });
    fullscreenInfoVisibilityToken_=BottomControlsBorder().RegisterPropertyChangedCallback(
        UIElement::VisibilityProperty(),[weak](auto const&,auto const&) {
            if (auto self=weak.get()) self->UpdateFullscreenInformation();
        });
}
void MainWindow::UpdateFullscreenInformation(bool refreshTelemetry) {
    if (!fullscreenInfoTimer_) return;
    auto const& state=session_->Snapshot();
    const bool visible=!closing_ && !controlBarInactive_ && !cropOriginal_ && !interactiveCrop_.Active() &&
        state.session==iima::SessionState::Ready && state.media==iima::MediaState::Loaded &&
        !musicMode_.IsMusic() && preferences_.Boolean("displayTimeAndBatteryInFullScreen") &&
        AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen &&
        BottomControlsBorder().Visibility()==Visibility::Visible;
    const bool wasVisible=FullscreenInformationBorder().Visibility()==Visibility::Visible;
    FullscreenInformationBorder().Visibility(visible?Visibility::Visible:Visibility::Collapsed);
    if (!visible) { fullscreenInfoTimer_.Stop(); return; }
    std::wstring caption=L"媒体";
    if (iima::CanPersistMediaPath(state.path)) {
        try { caption=std::filesystem::path(to_hstring(state.path).c_str()).filename().native(); }
        catch (...) { caption=L"媒体"; }
    } else if (iima::IsNetworkMediaPath(state.path)) caption=L"网络媒体";
    for (auto& c:caption) if (c<32 || c==127) c=L' ';
    if (caption.size()>200) {
        caption.resize(200);
        if (caption.back()>=0xd800 && caption.back()<=0xdbff) caption.pop_back();
        caption+=L"…";
    }
    if (caption.empty()) caption=L"媒体";
    if (FullscreenInformationTitle().Text()!=hstring(caption)) FullscreenInformationTitle().Text(caption);
    if (!wasVisible || refreshTelemetry) {
        auto information=iima::ReadFullscreenInformation();
        FullscreenClockText().Text(information.localTime);
        FullscreenClockText().Visibility(information.localTime.empty()?Visibility::Collapsed:Visibility::Visible);
        FullscreenBatteryText().Visibility(information.batteryPercent?Visibility::Visible:Visibility::Collapsed);
        FullscreenBatteryText().Text(information.batteryPercent ?
            L"电池 "+to_hstring(*information.batteryPercent)+L"%"+(information.charging?L"（充电）":L"") : L"");
    }
    if (!fullscreenInfoTimer_.IsRunning()) fullscreenInfoTimer_.Start();
}
void MainWindow::CloseFullscreenInformation() {
    if (fullscreenInfoTimer_) fullscreenInfoTimer_.Stop();
    if (fullscreenInfoVisibilityToken_) {
        BottomControlsBorder().UnregisterPropertyChangedCallback(UIElement::VisibilityProperty(),*fullscreenInfoVisibilityToken_);
        fullscreenInfoVisibilityToken_.reset();
    }
    FullscreenInformationBorder().Visibility(Visibility::Collapsed);
}
}

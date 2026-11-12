// IINA AccessibilityPreferences.adjustedDuration/motionReductionEnabled.
// Copyright (c) 2021 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.UI.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
bool MainWindow::AnimationsAllowed() const {
    return systemAnimationsEnabled_ && !preferences_.Boolean("disableAnimations");
}
void MainWindow::RefreshAccessibility() {
    if (closing_) return;
    BOOL enabled{};
    // A failed query is conservative: text and determinate feedback remain
    // available, but do not assume the user permits decorative movement.
    systemAnimationsEnabled_=SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0) && enabled;
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW)};
    const bool highContrast=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) &&
        (contrast.dwFlags&HCF_HIGHCONTRASTON);
    auto color=Windows::UI::Colors::Yellow();
    if (highContrast) {
        const auto rgb=GetSysColor(COLOR_HIGHLIGHT);
        color={255,GetRValue(rgb),GetGValue(rgb),GetBValue(rgb)};
    }
    Media::SolidColorBrush selection(color);
    InteractiveCropRectangle().BorderBrush(selection);
    for (auto const& handle:cropHandles_) handle.Background(selection);
    // Stock WinUI controls follow OS accessibility settings themselves. This
    // policy covers custom player feedback and programmatic settings scrolling,
    // not video content, decode timing or all framework-owned transitions.
    UpdateBufferingIndicator(session_->Snapshot());
}
}

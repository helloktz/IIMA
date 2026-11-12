// IINA BufferIndicatorView Windows native playback feedback.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Playback/BufferingIndicator.h"
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::UpdateBufferingIndicator(iima::PlaybackSnapshot const& state) {
    auto model=iima::ProjectBufferingIndicator(state,preferences_.Boolean("showBufferingThrobber"),
        preferences_.Boolean("showSeekingThrobber"));
    const bool hidden=closing_ || cropOriginal_ || interactiveCrop_.Active() ||
        AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay ||
        model.kind==iima::BufferingIndicatorKind::Hidden;
    BufferingOverlay().Visibility(hidden?Visibility::Collapsed:Visibility::Visible);
    BufferingSpinner().IsActive(!hidden && AnimationsAllowed());
    BufferingSpinner().Visibility(AnimationsAllowed()?Visibility::Visible:Visibility::Collapsed);
    if (hidden) {
        BufferingProgress().Visibility(Visibility::Collapsed);
        BufferingProgressText().Text(L""); BufferingDetailsText().Text(L""); return;
    }
    auto title=model.kind==iima::BufferingIndicatorKind::Opening ? L"正在打开网络流…" :
        model.kind==iima::BufferingIndicatorKind::Buffering ? L"正在缓冲…" : L"正在定位…";
    // Do not announce percentage every observation; only the phase label is a
    // polite live region. No fake ETA, throughput or seek-complete percentage.
    if (BufferingTitleText().Text()!=title) BufferingTitleText().Text(title);
    BufferingProgress().Visibility(model.percent?Visibility::Visible:Visibility::Collapsed);
    if (model.percent) BufferingProgress().Value(*model.percent);
    BufferingProgressText().Text(model.percent?to_hstring(*model.percent)+L"%":L"");
    BufferingDetailsText().Text(to_hstring(model.details));
}
}

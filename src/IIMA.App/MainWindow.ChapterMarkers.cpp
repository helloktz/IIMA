// IINA PlaySliderCell.drawBar chapter positions (showChapterPos).
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Windows adaptation: non-interactive one-physical-pixel ticks on the actual
// slider travel. Skip chapter zero; no synthetic chapters or playback commands.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <cmath>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::ClearChapterMarkers() {
    if(ChapterMarkers().Children().Size()) ChapterMarkers().Children().Clear();
    chapterMarkerPositions_.clear();ChapterMarkers().Visibility(Visibility::Collapsed);
}
void MainWindow::UpdateChapterMarkers() {
    if(chapterMarkersUpdating_) return;
    struct Guard {bool& value;~Guard(){value=false;}} guard{chapterMarkersUpdating_};chapterMarkersUpdating_=true;
    try {
        auto const& s=session_->Snapshot();
        if(closing_ || !userDataLoaded_ || !preferences_.Boolean("showChapterPos") || !Root().XamlRoot() ||
            !PositionSlider().IsLoaded() || inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty() ||
            s.session!=iima::SessionState::Ready || s.media!=iima::MediaState::Loaded || s.mediaOriginPending ||
            !s.duration || !std::isfinite(*s.duration) || *s.duration<=0 || s.chapters.size()<2 || s.chapters.size()>512) {
            ClearChapterMarkers();return;
        }
        auto slider=get_self<implementation::WheelSlider>(PositionSlider());
        auto travel=slider->HorizontalTravel();auto center=slider->HorizontalTrackCenterY();
        const auto scale=Root().XamlRoot().RasterizationScale();
        if(!travel || !center || !std::isfinite(scale) || scale<=0) {ClearChapterMarkers();return;}
        auto origin=PositionSlider().TransformToVisual(ChapterMarkers()).TransformPoint({0,0});
        if(!std::isfinite(origin.X) || !std::isfinite(origin.Y)) {ClearChapterMarkers();return;}
        std::vector<double> positions;positions.reserve(s.chapters.size()-1);
        double previous=-1;
        for(std::size_t i=0;i<s.chapters.size();++i) {
            const auto seconds=s.chapters[i].time;
            if(!std::isfinite(seconds) || seconds<0 || seconds>*s.duration || seconds<previous) {ClearChapterMarkers();return;}
            previous=seconds;
            if(i) positions.push_back(origin.X+travel->first+(seconds / *s.duration)*travel->second);
        }
        const double y=origin.Y+*center;
        const auto foreground=PositionSlider().Foreground();
        auto first=ChapterMarkers().Children().Size() ? ChapterMarkers().Children().GetAt(0).try_as<Shapes::Rectangle>() : nullptr;
        if(positions==chapterMarkerPositions_ && y==chapterMarkerY_ && scale==chapterMarkerScale_ &&
            ChapterMarkers().Children().Size()==positions.size() && first && first.Fill()==foreground) return;
        ChapterMarkers().Children().Clear();
        for(double x:positions) {
            Shapes::Rectangle tick;tick.Width(1.0/scale);tick.Height(6);tick.IsHitTestVisible(false);
            tick.Fill(foreground);tick.Opacity(0.8);
            Controls::Canvas::SetLeft(tick,x-0.5/scale);Controls::Canvas::SetTop(tick,y-3);
            ChapterMarkers().Children().Append(tick);
        }
        chapterMarkerPositions_=std::move(positions);chapterMarkerY_=y;chapterMarkerScale_=scale;
        ChapterMarkers().Visibility(Visibility::Visible);
    } catch(...) {ClearChapterMarkers();}
}
}

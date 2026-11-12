// IINA PlaySliderLoopKnob / PlaySliderCell. Copyright © 2021 lhc.
// GPL-3.0; references/iina/LICENSE. Windows: a separate marker lane leaves the
// play thumb unobstructed; A/B keep their identities when crossing. Release
// commits once, unlike the upstream live-drag writes. No pause or seek is sent.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Playback/PlaybackActions.h"
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
bool MainWindow::ABMarkerAvailable() {
    auto const& s=session_->Snapshot();
    return Root().XamlRoot() && !closing_ && userDataLoaded_ && !pickerOpen_ && !controlBarInactive_ && !oscDragging_ &&
        !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ &&
        !scrubToken_ && !scrubStarting_ && !session_->ActiveScrubToken() && !session_->PlaylistEditBusy() &&
        !fullscreenTransition_ && !compactTransition_ && !musicTransition_ &&
        AppWindow().Presenter().Kind()!=Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay &&
        s.session==iima::SessionState::Ready && s.media==iima::MediaState::Loaded && !s.mediaOriginPending &&
        s.seekable && s.duration && std::isfinite(*s.duration) && *s.duration>=iima::ABLoopState::MinimumEditedPoint &&
        s.abLoopA && std::isfinite(*s.abLoopA) && (!s.abLoopB || std::isfinite(*s.abLoopB));
}
void MainWindow::WireABLoopMarkers() {
    auto weak=get_weak();
    auto wire=[weak](Controls::Primitives::Thumb const& thumb,bool pointB) {
        thumb.DragStarted([weak,pointB](auto const&,auto const&) {if(auto self=weak.get()) self->BeginABMarker(pointB);});
        thumb.DragDelta([weak](auto const&,auto const& args) {
            if(auto self=weak.get();self && self->abMarkerDragging_) {
                self->UpdateABLoopMarkers();
                if(!self->abMarkerDragging_ || !std::isfinite(args.HorizontalChange())) return;
                self->abMarkerDraft_=std::clamp(self->abMarkerDraft_+args.HorizontalChange()/self->abMarkerTravel_*self->abMarkerDuration_,0.0,self->abMarkerDuration_);
                self->UpdateABLoopMarkers();
            }
        });
        thumb.DragCompleted([weak](auto const&,auto const& args) {if(auto self=weak.get()) self->FinishABMarker(args.Canceled());});
        thumb.Unloaded([weak](auto const&,auto const&) {if(auto self=weak.get()) self->CancelABMarker();});
        thumb.Tapped([](auto const&,Input::TappedRoutedEventArgs const& e){e.Handled(true);});
        thumb.DoubleTapped([](auto const&,Input::DoubleTappedRoutedEventArgs const& e){e.Handled(true);});
        thumb.RightTapped([](auto const&,Input::RightTappedRoutedEventArgs const& e){e.Handled(true);});
        thumb.PointerWheelChanged([](auto const&,Input::PointerRoutedEventArgs const& e){e.Handled(true);});
    };
    wire(ABLoopThumbA(),false);wire(ABLoopThumbB(),true);
    PositionSlider().LayoutUpdated([weak](auto const&,auto const&) {if(auto self=weak.get()) self->UpdateABLoopMarkers();});
}
void MainWindow::BeginABMarker(bool pointB) {
    if(abMarkerDragging_ || !ABMarkerAvailable() || (pointB && !session_->Snapshot().abLoopB)) {
        (pointB ? ABLoopThumbB() : ABLoopThumbA()).CancelDrag();return;
    }
    auto geometry=get_self<implementation::WheelSlider>(PositionSlider())->HorizontalTravel();
    if(!geometry) {(pointB ? ABLoopThumbB() : ABLoopThumbA()).CancelDrag();return;}
    try {abMarkerOrigin_=PositionSlider().TransformToVisual(Root()).TransformPoint({0,0});}
    catch(...) {(pointB ? ABLoopThumbB() : ABLoopThumbA()).CancelDrag();return;}
    abMarkerWindowSize_=AppWindow().Size();
    auto const& s=session_->Snapshot();
    abMarkerB_=pointB;abMarkerA_=s.abLoopA;abMarkerBValue_=s.abLoopB;
    abMarkerDuration_=*s.duration;abMarkerDraft_=std::clamp(*(pointB ? s.abLoopB : s.abLoopA),0.0,abMarkerDuration_);
    abMarkerStart_=geometry->first;abMarkerTravel_=geometry->second;abMarkerScale_=Root().XamlRoot().RasterizationScale();
    abMarkerTemplate_=get_self<implementation::WheelSlider>(PositionSlider())->TemplateRevision();
    abMarkerEditIntent_=session_->ABLoopEditIntentRevision();
    abMarkerGeneration_=s.mediaGeneration;abMarkerIntent_=s.mediaOpenIntentRevision;abMarkerInput_=inputGeneration_;
    abMarkerPresenter_=AppWindow().Presenter().Kind();abMarkerMusic_=musicMode_.IsMusic();abMarkerOscPosition_=preferences_.Integer("oscPosition");
    abMarkerDragging_=true;
    CancelMouseClick();CancelThumbnailPreview();ControlBarActivity();
}
void MainWindow::CancelABMarker() {
    if(!abMarkerDragging_) return;
    abMarkerDragging_=false;
    ABLoopThumbA().CancelDrag();ABLoopThumbB().CancelDrag();
    UpdateABLoopMarkers();
}
void MainWindow::FinishABMarker(bool cancelled) {
    if(!abMarkerDragging_) return;
    if(cancelled) {CancelABMarker();return;}
    UpdateABLoopMarkers();
    if(!abMarkerDragging_) return;
    const auto pointB=abMarkerB_;const auto seconds=abMarkerDraft_;
    const auto a=abMarkerA_,b=abMarkerBValue_;
    abMarkerDragging_=false;
    if(!cancelled && !session_->EditABLoopPoint(pointB,seconds,a,b) && !closing_)
        ShowError(L"循环端点已改变或操作未被接受；保留播放器观测值。");
    UpdateABLoopMarkers();UpdateControlBarPolicy();
}
void MainWindow::UpdateABLoopMarkers() {
    if(abMarkerUpdating_) return;
    struct Guard {bool& value;~Guard(){value=false;}} guard{abMarkerUpdating_};abMarkerUpdating_=true;
    auto const& s=session_->Snapshot();
    auto geometry=get_self<implementation::WheelSlider>(PositionSlider())->HorizontalTravel();
    std::optional<Windows::Foundation::Point> origin;
    try {origin=PositionSlider().TransformToVisual(Root()).TransformPoint({0,0});}catch(...){}
    const auto windowSize=AppWindow().Size();
    const bool ready=ABMarkerAvailable() && geometry.has_value() && origin.has_value();
    if(abMarkerDragging_ && (!ready || s.mediaGeneration!=abMarkerGeneration_ || s.mediaOpenIntentRevision!=abMarkerIntent_ ||
        inputGeneration_!=abMarkerInput_ || session_->ABLoopEditIntentRevision()!=abMarkerEditIntent_ ||
        Root().XamlRoot().RasterizationScale()!=abMarkerScale_ || get_self<implementation::WheelSlider>(PositionSlider())->TemplateRevision()!=abMarkerTemplate_ || s.abLoopA!=abMarkerA_ || s.abLoopB!=abMarkerBValue_ || *s.duration!=abMarkerDuration_ ||
        origin->X!=abMarkerOrigin_.X || origin->Y!=abMarkerOrigin_.Y ||
        windowSize.Width!=abMarkerWindowSize_.Width || windowSize.Height!=abMarkerWindowSize_.Height ||
        geometry->first!=abMarkerStart_ || geometry->second!=abMarkerTravel_ || AppWindow().Presenter().Kind()!=abMarkerPresenter_ ||
        musicMode_.IsMusic()!=abMarkerMusic_ || preferences_.Integer("oscPosition")!=abMarkerOscPosition_)) CancelABMarker();
    const bool visible=!closing_ && geometry && s.media==iima::MediaState::Loaded && s.duration &&
        std::isfinite(*s.duration) && *s.duration>0 && s.abLoopA && std::isfinite(*s.abLoopA) &&
        (!s.abLoopB || std::isfinite(*s.abLoopB));
    ABLoopMarkers().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    ABLoopThumbA().IsEnabled(ready);ABLoopThumbB().IsEnabled(ready);
    if(!visible) return;
    ABLoopMarkers().Height(s.abLoopB ? 44 : 22);
    Controls::Canvas::SetTop(ABLoopThumbB(),22);
    auto project=[&](Controls::Primitives::Thumb const& thumb,bool pointB) {
        auto value=pointB ? s.abLoopB : s.abLoopA;
        thumb.Visibility(value ? Visibility::Visible : Visibility::Collapsed);
        if(!value) return;
        const double seconds=abMarkerDragging_ && pointB==abMarkerB_ ? abMarkerDraft_ : *value;
        const auto text=(pointB ? hstring(L"循环终点B：") : hstring(L"循环起点A："))+to_hstring(seconds)+
            (abMarkerDragging_ && pointB==abMarkerB_ ? L"秒（拖动草稿）" : L"秒");
        Automation::AutomationProperties::SetName(thumb,text);
        Automation::AutomationProperties::SetHelpText(thumb,L"左右键调整一秒，Home/End到首尾；Enter不跳转。精确值可在A/B循环编辑中输入。");
        Controls::ToolTipService::SetToolTip(thumb,box_value(text));
        Controls::Canvas::SetLeft(thumb,geometry->first+std::clamp(seconds / *s.duration,0.0,1.0)*geometry->second-thumb.Width()/2);
    };
    project(ABLoopThumbA(),false);project(ABLoopThumbB(),true);
}
void MainWindow::ABMarkerKey(bool pointB,Input::KeyRoutedEventArgs const& args) {
    using Key=Windows::System::VirtualKey;
    const auto key=args.Key();
    if(key==Key::Escape) {CancelABMarker();args.Handled(true);return;}
    if(key==Key::Space || key==Key::Enter || key==Key::Up || key==Key::Down || key==Key::PageUp || key==Key::PageDown) {args.Handled(true);return;}
    if(key!=Key::Left && key!=Key::Right && key!=Key::Home && key!=Key::End) return;
    args.Handled(true);
    if(args.KeyStatus().WasKeyDown || abMarkerDragging_ || !ABMarkerAvailable()) return;
    auto const& s=session_->Snapshot();auto current=pointB ? s.abLoopB : s.abLoopA;
    if(!current) return;
    auto geometry=get_self<implementation::WheelSlider>(PositionSlider())->HorizontalTravel();if(!geometry) return;
    const auto direction=(geometry->second<0 ? -1.0 : 1.0)*(key==Key::Left ? -1.0 : 1.0);
    const double value=key==Key::Home ? 0 : key==Key::End ? *s.duration : std::clamp(*current+direction,0.0,*s.duration);
    if(!session_->EditABLoopPoint(pointB,value,s.abLoopA,s.abLoopB)) ShowError(L"循环端点操作未被接受；请等待观测更新。");
}
}

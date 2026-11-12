// IINA OSCFloatingView.initPosition/updatePosition/mouseDragged/mouseUp.
// Copyright © 2026 lhc. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <cmath>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireControlBarLayout() {
    auto weak=get_weak();
    auto layout=[weak](auto const&,auto const&) {if(auto self=weak.get()) self->UpdateControlBarLayout();};
    OscLayoutHost().SizeChanged(layout);
    BottomControlsBorder().SizeChanged([weak](auto const&,auto const&) {
        if(auto self=weak.get()) {self->CancelControlBarDrag();self->UpdateControlBarLayout();}
    });
    TopStatusBorder().SizeChanged(layout);MusicHeader().SizeChanged(layout);
    VideoPanel().LayoutUpdated(layout); // Covers DPI and sidebar geometry without changing video bounds.
    ThumbnailPreviewBorder().SizeChanged([weak](auto const&,auto const&) {if(auto self=weak.get()) self->UpdateControlBarOverlays();});
    OscDragHandle().DragStarted([weak](auto const&,auto const&) {
        auto self=weak.get();if(!self) return;
        if(self->closing_ || !self->userDataLoaded_ || self->pickerOpen_ || self->interactiveCrop_.Active() || self->cropOriginal_ ||
            self->preferences_.Integer("oscPosition")!=0 || self->oscViewport_.width<=0 || self->oscViewport_.height<=0) {
            self->OscDragHandle().CancelDrag();return;
        }
        self->CancelMouseClick();self->CancelThumbnailPreview();
        self->oscDragBaseline_={self->preferences_.Number("controlBarPositionHorizontal"),self->preferences_.Number("controlBarPositionVertical")};
        self->oscDragPosition_=self->oscDragBaseline_;
        self->oscDragScale_=self->VideoPanel().CompositionScaleX();
        self->oscDragMode_=static_cast<std::int64_t>(self->AppWindow().Presenter().Kind());
        self->oscDragFullscreen_=self->fullscreen_;self->oscDragMusic_=self->musicMode_.IsMusic();
        self->oscDragging_=true;self->ControlBarActivity();
    });
    OscDragHandle().DragDelta([weak](auto const&,Controls::Primitives::DragDeltaEventArgs const& args) {
        auto self=weak.get();if(!self || !self->oscDragging_) return;
        self->UpdateControlBarLayout();if(!self->oscDragging_) return;
        auto current=self->oscRect_;current.y-=self->oscTopInset_;
        if(auto position=iima::settings::MoveFloatingControlBar(self->oscViewport_,{current.width,current.height},current,
            {args.HorizontalChange(),args.VerticalChange()},self->preferences_.Boolean("controlBarStickToCenter"))) {
            self->oscDragPosition_=*position;self->UpdateControlBarLayout();
        }
    });
    OscDragHandle().DragCompleted([weak](auto const&,Controls::Primitives::DragCompletedEventArgs const& args) {
        if(auto self=weak.get()) self->FinishControlBarDrag(args.Canceled());
    });
    Root().PreviewKeyDown([weak](auto const&,Input::KeyRoutedEventArgs const& args) {
        if(auto self=weak.get();self && self->oscDragging_ && args.Key()==Windows::System::VirtualKey::Escape) {
            self->CancelControlBarDrag();args.Handled(true);
        }
    });
}
void MainWindow::CancelControlBarDrag() {
    if(!oscDragging_) return;
    oscDragging_=false;
    OscDragHandle().CancelDrag();
    CancelThumbnailPreview();UpdateControlBarLayout();
}
void MainWindow::FinishControlBarDrag(bool cancel) {
    if(!oscDragging_) return;
    // Layout/mode/preference changes cancel before committing the gesture.
    UpdateControlBarLayout();if(!oscDragging_) return;
    auto position=oscDragPosition_;oscDragging_=false;
    if(!cancel && !closing_ && !pickerOpen_) {
        auto next=preferences_;std::string error;
        if(next.Set("controlBarPositionHorizontal",position.horizontal,error) &&
            next.Set("controlBarPositionVertical",position.vertical,error)) {
            preferences_=std::move(next); // Both scalars become visible atomically to UI projection.
            userData_.settings.advancedPreferences=preferences_.Overrides();MarkUserDataDirty();
            PopulateAdvancedSettings(); // Refresh numeric editors without replacing their saved search/filter controls.
        }
    }
    UpdateControlBarLayout();UpdateControlBarPolicy();
}
void MainWindow::UpdateControlBarLayout() {
    if(oscLayoutUpdating_ || closing_ || !userDataLoaded_) return;
    struct Guard {bool& b;Guard(bool& value):b(value){b=true;}~Guard(){b=false;}} guard(oscLayoutUpdating_);
    double width=OscLayoutHost().ActualWidth(),height=OscLayoutHost().ActualHeight();
    if(!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0) {
        oscViewport_={};CancelControlBarDrag();return;
    }
    double top=TopStatusBorder().Visibility()==Visibility::Visible ? TopStatusBorder().ActualHeight() : 0;
    if(MusicHeader().Visibility()==Visibility::Visible) top=std::max(top,MusicHeader().Margin().Top+MusicHeader().ActualHeight()+8);
    top=std::clamp(top,0.0,std::max(0.0,height-1));
    iima::GeometrySize viewport{width,height-top};
    auto mode=preferences_.Integer("oscPosition");
    iima::settings::FloatingControlBarPosition position{preferences_.Number("controlBarPositionHorizontal"),preferences_.Number("controlBarPositionVertical")};
    if(oscDragging_ && (mode!=0 || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ || controlBarInactive_ ||
        viewport.width!=oscViewport_.width || viewport.height!=oscViewport_.height || top!=oscTopInset_ ||
        VideoPanel().CompositionScaleX()!=oscDragScale_ || static_cast<std::int64_t>(AppWindow().Presenter().Kind())!=oscDragMode_ ||
        fullscreen_!=oscDragFullscreen_ || musicMode_.IsMusic()!=oscDragMusic_ ||
        position.horizontal!=oscDragBaseline_.horizontal || position.vertical!=oscDragBaseline_.vertical)) CancelControlBarDrag();
    oscViewport_=viewport;oscTopInset_=top;
    OscDragHandle().Visibility(mode==0 ? Visibility::Visible : Visibility::Collapsed);
    double targetWidth=mode==0 ? std::min(width,560.0) : width;
    if(BottomControlsBorder().Width()!=targetWidth) BottomControlsBorder().Width(targetWidth);
    if(BottomControlsBorder().MaxHeight()!=viewport.height) BottomControlsBorder().MaxHeight(viewport.height);
    double barHeight=std::clamp(BottomControlsBorder().ActualHeight(),1.0,viewport.height);
    iima::GeometryRect rect{0,mode==1 ? top : height-barHeight,targetWidth,barHeight};
    if(mode==0) {
        auto projected=iima::settings::ProjectFloatingControlBar(viewport,{targetWidth,barHeight},oscDragging_ ? oscDragPosition_ : position);
        if(!projected) return;
        rect=*projected;rect.y+=top;
    }
    oscRect_=rect;
    if(Controls::Canvas::GetLeft(BottomControlsBorder())!=rect.x) Controls::Canvas::SetLeft(BottomControlsBorder(),rect.x);
    if(Controls::Canvas::GetTop(BottomControlsBorder())!=rect.y) Controls::Canvas::SetTop(BottomControlsBorder(),rect.y);
    UpdateControlBarOverlays();
}
void MainWindow::UpdateControlBarOverlays() {
    if(closing_ || !userDataLoaded_) return;
    double width=OscLayoutHost().ActualWidth(),height=OscLayoutHost().ActualHeight();
    if(width<=0 || height<=0) return;
    // Top information remains below the safe top strip and a visible top OSC.
    double infoTop=oscTopInset_+8;
    double infoWidth=FullscreenInformationBorder().ActualWidth(),infoHeight=FullscreenInformationBorder().ActualHeight();
    double infoLeft=width-FullscreenInformationBorder().Margin().Right-infoWidth;
    bool overlaps=infoLeft<oscRect_.x+oscRect_.width && infoLeft+infoWidth>oscRect_.x &&
        infoTop<oscRect_.y+oscRect_.height && infoTop+infoHeight>oscRect_.y;
    if(BottomControlsBorder().Visibility()==Visibility::Visible &&
        (preferences_.Integer("oscPosition")==1 || overlaps)) infoTop=oscRect_.y+oscRect_.height+8;
    infoTop=std::clamp(infoTop,0.0,std::max(0.0,height-FullscreenInformationBorder().ActualHeight()));
    auto infoMargin=FullscreenInformationBorder().Margin();
    if(infoMargin.Top!=infoTop) {infoMargin.Top=infoTop;FullscreenInformationBorder().Margin(infoMargin);}
    if(ThumbnailPreviewBorder().Visibility()!=Visibility::Visible) return;
    auto preview=ThumbnailPreviewBorder();
    if(preview.MaxWidth()!=width) preview.MaxWidth(width);
    if(preview.MaxHeight()!=height) preview.MaxHeight(height);
    double pw=std::min(width,preview.ActualWidth()),ph=std::min(height,preview.ActualHeight());
    double x=oscRect_.x+(oscRect_.width-pw)/2;
    double y=oscRect_.y-ph-8;
    if(y<oscTopInset_) y=oscRect_.y+oscRect_.height+8;
    x=std::clamp(x,0.0,std::max(0.0,width-pw));y=std::clamp(y,0.0,std::max(0.0,height-ph));
    preview.HorizontalAlignment(HorizontalAlignment::Left);preview.VerticalAlignment(VerticalAlignment::Top);
    auto margin=preview.Margin();
    if(margin.Left!=x || margin.Top!=y || margin.Right!=0 || margin.Bottom!=0) preview.Margin({x,y,0,0});
}
}

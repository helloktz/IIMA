// Live crop surface adapted from IINA InteractiveModeController and CropBoxView.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.System.h>
#include <charconv>
#include <cmath>
#include <array>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
std::optional<double> CropNumber(iima::MediaProperties const& map, char const* key) {
    auto it = map.find(key); if (it == map.end()) return {};
    double value{};
    auto [end, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
    if (ec != std::errc{} || end != it->second.data() + it->second.size() || !std::isfinite(value)) return {};
    return value;
}
bool SameRect(iima::CropRect const& a, iima::CropRect const& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
}
namespace winrt::IIMA::implementation {
std::optional<iima::CropMapping> MainWindow::InteractiveCropMapping() const {
    auto const& s = session_->Snapshot();
    if (s.session != iima::SessionState::Ready || s.media != iima::MediaState::Loaded ||
        !s.voConfigured || !s.compositionAvailable || !s.videoFilters.complete ||
        !s.videoFilters.entries.empty() || s.videoRotation != std::optional<std::int64_t>{0}) return {};
    auto w = CropNumber(s.videoParameters, "w"), h = CropNumber(s.videoParameters, "h");
    auto rotate = CropNumber(s.videoParameters, "rotate");
    auto ow = CropNumber(s.videoOutputParameters, "w"), oh = CropNumber(s.videoOutputParameters, "h");
    auto outputRotate = CropNumber(s.videoOutputParameters, "rotate");
    if (!w || !h || !ow || !oh || !rotate || !outputRotate || *rotate != 0 || *outputRotate != 0 ||
        *w != *ow || *h != *oh || *w < 1 || *h < 1 || *w > 65536 || *h > 65536 ||
        std::floor(*w) != *w || std::floor(*h) != *h) return {};
    auto uncropped = [&](iima::MediaProperties const& values) {
        if (!values.contains("crop-x") && !values.contains("crop-y") &&
            !values.contains("crop-w") && !values.contains("crop-h")) return true;
        return CropNumber(values,"crop-x") == std::optional<double>{0} &&
            CropNumber(values,"crop-y") == std::optional<double>{0} &&
            CropNumber(values,"crop-w") == w && CropNumber(values,"crop-h") == h;
    };
    if (!uncropped(s.videoParameters) || !uncropped(s.videoOutputParameters)) return {};
    auto dw = CropNumber(s.osdDimensions, "w"), dh = CropNumber(s.osdDimensions, "h");
    auto left = CropNumber(s.osdDimensions, "ml"), right = CropNumber(s.osdDimensions, "mr");
    auto top = CropNumber(s.osdDimensions, "mt"), bottom = CropNumber(s.osdDimensions, "mb");
    if (!dw || !dh || !left || !right || !top || !bottom || *dw <= 0 || *dh <= 0) return {};
    const double width = VideoPanel().ActualWidth(), height = VideoPanel().ActualHeight();
    // The pinned D3D composition path renders in physical pixels. Refuse stale
    // OSD dimensions during resize rather than stretching an old viewport map.
    if (width <= 0 || height <= 0 ||
        std::abs(std::ceil(width * VideoPanel().CompositionScaleX()) - *dw) > 1 ||
        std::abs(std::ceil(height * VideoPanel().CompositionScaleY()) - *dh) > 1) return {};
    iima::CropMapping map;
    map.sourceWidth = static_cast<int>(*w); map.sourceHeight = static_cast<int>(*h);
    map.viewport = {0, 0, width, height};
    map.image = {*left * width / *dw, *top * height / *dh,
                 (*dw - *left - *right) * width / *dw, (*dh - *top - *bottom) * height / *dh};
    map.mappingConfirmed = true; map.mediaGeneration = navigationEpoch_;
    return iima::IsCropMappingValid(map) ? std::optional{map} : std::nullopt;
}
void MainWindow::WireInteractiveCrop() {
    auto weak = get_weak();
    for (int n = 0; n < 8; ++n) {
        Controls::Border handle;
        handle.Width(10); handle.Height(10); handle.Background(Media::SolidColorBrush(Windows::UI::Colors::Yellow()));
        handle.IsHitTestVisible(false); cropHandles_.push_back(handle);
        InteractiveCropCanvas().Children().Append(handle);
    }
    StartInteractiveCropButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->StartInteractiveCrop();
    });
    InteractiveCropCancelButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->FinishInteractiveCrop(false);
    });
    InteractiveCropConfirmButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->FinishInteractiveCrop(true);
    });
    InteractiveCropAspectButton().Click([weak](auto const&, auto const&) {
        auto self = weak.get(); if (!self || !self->interactiveCrop_.Active() || !self->cropMapping_) return;
        constexpr std::array<iima::CropPoint, 7> aspects{{{0,0},{4,3},{16,9},{16,10},{21,9},{3,2},{5,4}}};
        auto index = self->InteractiveCropAspect().SelectedIndex();
        if (index < 0 || index >= static_cast<int>(aspects.size())) return;
        if (index == 0) self->interactiveCrop_.SetPixels({0,0,self->cropMapping_->sourceWidth,self->cropMapping_->sourceHeight});
        else self->interactiveCrop_.SelectCenteredAspect(aspects[index].x, aspects[index].y);
        self->PaintInteractiveCrop();
    });
    InteractiveCropNumbersButton().Click([weak](auto const&, auto const&) {
        auto self = weak.get(); if (!self || !self->interactiveCrop_.Active()) return;
        std::array<double,4> numbers{self->InteractiveCropX().Value(),self->InteractiveCropY().Value(),
            self->InteractiveCropWidth().Value(),self->InteractiveCropHeight().Value()};
        for (auto value : numbers) if (!std::isfinite(value) || value < 0 || value > 65536 || std::floor(value) != value) {
            self->InteractiveCropStatus().Text(L"请输入范围内的整数坐标。"); return;
        }
        if (!self->interactiveCrop_.SetPixels({static_cast<int>(numbers[0]),static_cast<int>(numbers[1]),
                static_cast<int>(numbers[2]),static_cast<int>(numbers[3])})) {
            self->InteractiveCropStatus().Text(L"裁剪区域必须位于源图像内且至少1×1像素。"); return;
        }
        self->PaintInteractiveCrop();
    });
    InteractiveCropCanvas().PointerPressed([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        auto self = weak.get(); if (!self || !self->interactiveCrop_.Active() || self->cropPointer_) return;
        auto point = args.GetCurrentPoint(self->InteractiveCropCanvas());
        if (!point.Properties().IsLeftButtonPressed()) return;
        iima::CropPoint p{point.Position().X,point.Position().Y};
        auto handle = self->interactiveCrop_.HitTest(p, 8);
        if (!self->interactiveCrop_.BeginDrag(handle,p)) return;
        if (!self->InteractiveCropCanvas().CapturePointer(args.Pointer())) {
            self->interactiveCrop_.CancelDrag(); return;
        }
        self->cropPointer_ = point.PointerId(); args.Handled(true); self->PaintInteractiveCrop();
    });
    InteractiveCropCanvas().PointerMoved([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        auto self = weak.get(); if (!self || !self->cropPointer_) return;
        auto point = args.GetCurrentPoint(self->InteractiveCropCanvas());
        if (*self->cropPointer_ != point.PointerId()) return;
        self->interactiveCrop_.DragTo({point.Position().X,point.Position().Y});
        self->PaintInteractiveCrop(); args.Handled(true);
    });
    InteractiveCropCanvas().PointerReleased([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        auto self = weak.get(); if (!self || !self->cropPointer_ || *self->cropPointer_ != args.Pointer().PointerId()) return;
        self->interactiveCrop_.EndDrag(); self->cropPointer_.reset();
        self->InteractiveCropCanvas().ReleasePointerCapture(args.Pointer());
        self->PaintInteractiveCrop(); args.Handled(true);
    });
    auto cancel = [weak](auto const&, Input::PointerRoutedEventArgs const&) {
        if (auto self = weak.get(); self && self->cropPointer_) {
            self->cropPointer_.reset(); self->interactiveCrop_.CancelDrag(); self->PaintInteractiveCrop();
        }
    };
    InteractiveCropCanvas().PointerCanceled(cancel);
    InteractiveCropCanvas().PointerCaptureLost(cancel);
    Root().PreviewKeyDown([weak](auto const&, Input::KeyRoutedEventArgs const& args) {
        auto self = weak.get(); if (!self || !self->interactiveCrop_.Active()) return;
        if (args.Key() == Windows::System::VirtualKey::Escape) {
            self->FinishInteractiveCrop(false); args.Handled(true);
        } else if (args.Key() == Windows::System::VirtualKey::Enter) {
            // Numeric edits are staged explicitly; Enter in a text editor must
            // not commit an older visual rectangle behind the user's draft.
            auto focus = Input::FocusManager::GetFocusedElement(self->Root().XamlRoot()).try_as<DependencyObject>();
            for (auto node=focus; node; node=Media::VisualTreeHelper::GetParent(node))
                if (node.try_as<Controls::TextBox>() || node.try_as<Controls::NumberBox>()) return;
            self->FinishInteractiveCrop(true); args.Handled(true);
        }
    });
    Activated([weak](auto const&, WindowActivatedEventArgs const& args) {
        if (auto self = weak.get(); self && self->interactiveCrop_.Active() &&
            args.WindowActivationState() == WindowActivationState::Deactivated) {
            self->cropPointer_.reset(); self->interactiveCrop_.CancelDrag();
            self->InteractiveCropCanvas().ReleasePointerCaptures(); self->PaintInteractiveCrop();
        }
    });
    VideoPanel().SizeChanged([weak](auto const&, SizeChangedEventArgs const&) {
        if (auto self = weak.get()) self->UpdateInteractiveCrop(self->session_->Snapshot());
    });
}
void MainWindow::StartInteractiveCrop() {
    if (closing_ || pickerOpen_ || interactiveCrop_.Active()) return;
    if (AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay) {
        ShowError(L"请先退出悬浮小窗再交互裁剪。"); return;
    }
    auto mapping = InteractiveCropMapping();
    if (!mapping) {
        ShowError(L"交互裁剪需要已确认的未旋转视频映射。请移除现有视频滤镜/裁剪，恢复旋转为0，等待画面尺寸稳定后重试。"); return;
    }
    if (!interactiveCrop_.Begin(*mapping)) return;
    cropMapping_ = mapping;
    auto const& state = session_->Snapshot();
    interactiveCropEpoch_ = navigationEpoch_; interactiveCropEntry_ = state.entryId;
    interactiveCropFilterRevision_ = state.videoFilters.revision;
    interactiveCropWasPlaying_ = !state.paused;
    CancelMouseClick(); CancelThumbnailPreview();
    InteractiveCropResume().IsChecked(true);
    InteractiveCropConfirmButton().IsEnabled(true);
    session_->Pause(true);
    PlayerSidebar().IsEnabled(false);
    InteractiveCropCanvas().Visibility(Visibility::Visible);
    InteractiveCropToolbar().Visibility(Visibility::Visible);
    InteractiveCropCancelButton().Focus(FocusState::Programmatic);
    UpdateControlBarPolicy(); PaintInteractiveCrop();
}
void MainWindow::PaintInteractiveCrop() {
    if (!interactiveCrop_.Active() || !cropMapping_) return;
    auto rect = interactiveCrop_.DisplayRect(); if (!rect) return;
    Media::RectangleGeometry clip;
    clip.Rect({0,0,static_cast<float>(cropMapping_->viewport.width),static_cast<float>(cropMapping_->viewport.height)});
    InteractiveCropCanvas().Clip(clip);
    Controls::Canvas::SetLeft(InteractiveCropRectangle(), rect->x);
    Controls::Canvas::SetTop(InteractiveCropRectangle(), rect->y);
    InteractiveCropRectangle().Width(rect->width); InteractiveCropRectangle().Height(rect->height);
    std::array<iima::CropPoint,8> points{{{rect->x,rect->y},{rect->x+rect->width/2,rect->y},{rect->Right(),rect->y},
        {rect->Right(),rect->y+rect->height/2},{rect->Right(),rect->Bottom()},
        {rect->x+rect->width/2,rect->Bottom()},{rect->x,rect->Bottom()},{rect->x,rect->y+rect->height/2}}};
    for (std::size_t n=0;n<points.size();++n) {
        Controls::Canvas::SetLeft(cropHandles_[n],points[n].x-5);
        Controls::Canvas::SetTop(cropHandles_[n],points[n].y-5);
    }
    auto pixels = iima::QuantizeCropRect(interactiveCrop_.Selection(),cropMapping_->sourceWidth,cropMapping_->sourceHeight);
    if (!pixels) return;
    InteractiveCropX().Value(pixels->x); InteractiveCropY().Value(pixels->y);
    InteractiveCropWidth().Value(pixels->width); InteractiveCropHeight().Value(pixels->height);
    InteractiveCropStatus().Text(L"草稿：("+to_hstring(pixels->x)+L", "+to_hstring(pixels->y)+L") "+
        to_hstring(pixels->width)+L" × "+to_hstring(pixels->height)+L"；确认才提交滤镜。");
}
void MainWindow::UpdateInteractiveCrop(iima::PlaybackSnapshot const& state) {
    if (!interactiveCrop_.Active()) return;
    if (navigationEpoch_ != interactiveCropEpoch_ || state.entryId != interactiveCropEntry_ ||
        state.media != iima::MediaState::Loaded || state.videoFilters.revision != interactiveCropFilterRevision_) {
        FinishInteractiveCrop(false,false); return;
    }
    auto mapping = InteractiveCropMapping();
    if (!cropMapping_ || CropNumber(state.videoParameters,"w") != std::optional<double>{cropMapping_->sourceWidth} ||
        CropNumber(state.videoParameters,"h") != std::optional<double>{cropMapping_->sourceHeight}) {
        FinishInteractiveCrop(false,false);
        if (!closing_) ShowError(L"源视频尺寸已改变，交互裁剪已取消；未自动恢复播放。");
        return;
    }
    if (!mapping) {
        // During resize, wait for the matching mpv OSD geometry. Preserve source
        // pixels, but never accept pointer input or confirm with a stale map.
        cropPointer_.reset(); interactiveCrop_.CancelDrag(); InteractiveCropCanvas().ReleasePointerCaptures();
        InteractiveCropCanvas().Visibility(Visibility::Collapsed);
        InteractiveCropConfirmButton().IsEnabled(false);
        InteractiveCropStatus().Text(L"等待兼容且稳定的视频映射；可取消退出。旋转/内建裁剪等不支持的变换需先恢复。");
        return;
    }
    InteractiveCropCanvas().Visibility(Visibility::Visible);
    InteractiveCropConfirmButton().IsEnabled(true);
    if (!SameRect(mapping->viewport,cropMapping_->viewport) || !SameRect(mapping->image,cropMapping_->image)) {
        cropPointer_.reset(); InteractiveCropCanvas().ReleasePointerCaptures();
        interactiveCrop_.UpdateMapping(*mapping); cropMapping_ = mapping; PaintInteractiveCrop();
    }
}
void MainWindow::FinishInteractiveCrop(bool commit, bool restorePlayback) {
    if (InteractiveCropToolbar().Visibility() != Visibility::Visible) return;
    auto const& state = session_->Snapshot();
    const bool current = navigationEpoch_ == interactiveCropEpoch_ && state.entryId == interactiveCropEntry_ &&
        state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded;
    std::optional<iima::CropCommit> result;
    if (commit && current && state.videoFilters.revision == interactiveCropFilterRevision_ && InteractiveCropMapping())
        result = interactiveCrop_.Confirm();
    const bool resume = restorePlayback && current && interactiveCropWasPlaying_ &&
        InteractiveCropResume().IsChecked().Value();
    interactiveCrop_.Cancel(); cropMapping_.reset(); cropPointer_.reset();
    InteractiveCropCanvas().ReleasePointerCaptures();
    InteractiveCropCanvas().Visibility(Visibility::Collapsed);
    InteractiveCropToolbar().Visibility(Visibility::Collapsed);
    PlayerSidebar().IsEnabled(true);
    if (result && !result->removeManagedCrop) session_->AddFilter(false,result->filterExpression);
    else if (commit && !result && !closing_) ShowError(L"裁剪草稿已失效，未提交滤镜。");
    if (resume) session_->Pause(false);
    ControlBarActivity();
    if (!closing_) StartInteractiveCropButton().Focus(FocusState::Programmatic);
}
}

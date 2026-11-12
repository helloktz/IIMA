// IINA PlayerCore.generateThumbnails / MainWindowController thumbnail hover.
// Copyright (c) lhc and contributors. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
#include "WheelSlider.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include "Platform/ThumbnailDiskCache.h"
#include "../IIMA.Core/Media/VideoTime.h"
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <cmath>
#include <thread>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
namespace {
bool HoverAncestorsVisible(DependencyObject node) {
    for(;node;node=Media::VisualTreeHelper::GetParent(node))
        if(auto element=node.try_as<UIElement>();element && element.Visibility()!=Visibility::Visible) return false;
    return true;
}
}
void MainWindow::WireThumbnailPreview() {
    auto weak = get_weak();
    ClearThumbnailCacheButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->ClearThumbnailCache();
    });
    thumbnailTimer_ = DispatcherQueue().CreateTimer();
    thumbnailTimer_.IsRepeating(false);
    thumbnailTimer_.Interval(std::chrono::milliseconds(180));
    thumbnailTimer_.Tick([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->BeginThumbnailRequest();
    });
    // IINA displays time/chapter before independently checking thumbnail readiness.
    PositionSlider().PointerMoved([weak](auto const&, Input::PointerRoutedEventArgs const& args) {
        auto self = weak.get();if(!self) return;
        try {
            auto const& state=self->session_->Snapshot();
            if(self->closing_ || self->controlBarInactive_ || self->oscDragging_ || self->fullscreenTransition_ ||
                self->compactTransition_ || self->musicTransition_ || !HoverAncestorsVisible(self->PositionSlider()) || self->scrubToken_ || self->scrubStarting_ || self->abMarkerDragging_ ||
                self->session_->ActiveScrubToken() || self->inputResolving_ || !self->pendingFiles_.empty() ||
                !self->activationInputs_.empty() || self->pickerOpen_ || self->interactiveCrop_.Active() || self->cropOriginal_ ||
                state.session!=iima::SessionState::Ready || state.media!=iima::MediaState::Loaded || state.mediaOriginPending ||
                !state.seekable || !state.duration || !std::isfinite(*state.duration) || *state.duration<=0 ||
                !self->PositionSlider().IsEnabled()) {self->CancelThumbnailPreview();return;}
            auto slider=self->PositionSlider();
            auto native=get_self<implementation::WheelSlider>(slider);
            auto geometry=native->HorizontalTravel();
            const auto x=args.GetCurrentPoint(slider).Position().X;
            if(!geometry || !std::isfinite(x) || !std::isfinite(geometry->second) || geometry->second==0) {self->CancelThumbnailPreview();return;}
            const auto fraction=std::clamp((x-geometry->first)/geometry->second,0.0,1.0);
            const auto seconds=std::floor(fraction * *state.duration);
            const bool same=self->thumbnailWanted_ && self->ThumbnailPreviewCurrent() && seconds==self->thumbnailSeconds_;
            if(!same) {
                self->thumbnailStop_.request_stop();++self->thumbnailGeneration_;
                self->thumbnailTimer_.Stop();
                self->ThumbnailPreviewImage().Source(nullptr);
                self->ThumbnailPreviewImage().Visibility(Visibility::Collapsed);
            }
            self->thumbnailSeconds_=seconds;self->thumbnailWanted_=true;
            self->thumbnailEntry_=state.entryId;self->thumbnailPath_=state.path;
            self->thumbnailMediaGeneration_=state.mediaGeneration;self->thumbnailOpenIntent_=state.mediaOpenIntentRevision;
            self->thumbnailInput_=self->inputGeneration_;self->thumbnailDuration_=*state.duration;
            self->thumbnailTemplate_=native->TemplateRevision();self->thumbnailTravelStart_=geometry->first;self->thumbnailTravel_=geometry->second;
            self->thumbnailOrigin_=slider.TransformToVisual(self->Root()).TransformPoint({0,0});
            self->thumbnailScale_=self->Root().XamlRoot().RasterizationScale();
            self->thumbnailPresenter_=self->AppWindow().Presenter().Kind();self->thumbnailMusic_=self->musicMode_.IsMusic();
            self->thumbnailFullscreen_=self->fullscreen_;self->thumbnailOscPosition_=self->preferences_.Integer("oscPosition");
            self->UpdateThumbnailTime(seconds);
            self->ThumbnailPreviewBorder().Visibility(Visibility::Visible);
            self->UpdateControlBarOverlays();
            if(!same && self->preferences_.Boolean("enableThumbnailPreview") && !iima::IsNetworkMediaPath(state.path)) self->thumbnailTimer_.Start();
        } catch(...) {self->CancelThumbnailPreview();}
    });
    PositionSlider().LayoutUpdated([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && self->thumbnailWanted_ && !self->ThumbnailPreviewCurrent()) self->CancelThumbnailPreview();
    });
    PositionSlider().PointerExited([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CancelThumbnailPreview();
    });
    PositionSlider().PointerCanceled([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CancelThumbnailPreview();
    });
}
bool MainWindow::ThumbnailPreviewCurrent() {
    try {
        auto const& state=session_->Snapshot();
        if(!thumbnailWanted_ || closing_ || controlBarInactive_ || oscDragging_ || fullscreenTransition_ || compactTransition_ ||
            musicTransition_ || !HoverAncestorsVisible(PositionSlider()) ||
            AppWindow().Presenter().Kind()!=thumbnailPresenter_ || musicMode_.IsMusic()!=thumbnailMusic_ ||
            fullscreen_!=thumbnailFullscreen_ || preferences_.Integer("oscPosition")!=thumbnailOscPosition_ || scrubToken_ || scrubStarting_ || abMarkerDragging_ || session_->ActiveScrubToken() ||
            inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty() || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ ||
            state.session!=iima::SessionState::Ready || state.media!=iima::MediaState::Loaded || state.mediaOriginPending ||
            !state.seekable || !state.duration || !std::isfinite(*state.duration) || *state.duration<=0 || *state.duration!=thumbnailDuration_ ||
            state.entryId!=thumbnailEntry_ || state.path!=thumbnailPath_ || state.mediaGeneration!=thumbnailMediaGeneration_ ||
            state.mediaOpenIntentRevision!=thumbnailOpenIntent_ || inputGeneration_!=thumbnailInput_ || !PositionSlider().IsEnabled()) return false;
        auto native=get_self<implementation::WheelSlider>(PositionSlider());auto geometry=native->HorizontalTravel();
        auto origin=PositionSlider().TransformToVisual(Root()).TransformPoint({0,0});
        return geometry && geometry->first==thumbnailTravelStart_ && geometry->second==thumbnailTravel_ &&
            native->TemplateRevision()==thumbnailTemplate_ && Root().XamlRoot().RasterizationScale()==thumbnailScale_ &&
            origin.X==thumbnailOrigin_.X && origin.Y==thumbnailOrigin_.Y;
    } catch(...) {return false;}
}
void MainWindow::UpdateThumbnailTime(double seconds) {
    // PlaybackInfo.getChapter(forVideoTime:) selects the last chapter at/before
    // the hovered time. Never use the decoded keyframe's timestamp as the label.
    std::wstring title;
    auto const& chapters=session_->Snapshot().chapters;
    if(chapters.size()<=4096) for(auto it=chapters.rbegin();it!=chapters.rend();++it) {
        if(!std::isfinite(it->time) || it->time<0 || it->time>seconds) continue;
        if(it->title.size()<=4096) try {
            auto text=to_hstring(it->title);
            for(auto ch:text) {
                if(title.size()>=256) break;
                title+=ch<32 || (ch>=127 && ch<=159) || (ch>=0x2028 && ch<=0x202e) || (ch>=0x2066 && ch<=0x2069) || ch==0x200e || ch==0x200f || ch==0x061c ? L' ' : ch;
            }
            if(!title.empty() && title.back()>=0xd800 && title.back()<=0xdbff) title.pop_back();
        } catch(...) {}
        break;
    }
    auto text=to_hstring(iima::VideoTime(seconds).StringRepresentation());
    if(!title.empty()) text=hstring(title)+L"\n"+text;
    if(ThumbnailPreviewTime().Text()!=text) ThumbnailPreviewTime().Text(text);
}
void MainWindow::CancelThumbnailPreview() {
    thumbnailStop_.request_stop();
    ++thumbnailGeneration_;
    thumbnailWanted_ = false;
    if (thumbnailTimer_) thumbnailTimer_.Stop();
    ThumbnailPreviewBorder().Visibility(Visibility::Collapsed);
    ThumbnailPreviewImage().Source(nullptr);
    ThumbnailPreviewImage().Visibility(Visibility::Collapsed);
}
void MainWindow::UpdateThumbnailMedia(iima::PlaybackSnapshot const& state) {
    const bool mediaChanged=thumbnailEntry_!=state.entryId || thumbnailPath_!=state.path ||
        thumbnailMediaGeneration_!=state.mediaGeneration || thumbnailOpenIntent_!=state.mediaOpenIntentRevision;
    if(mediaChanged || state.media!=iima::MediaState::Loaded) {
        CancelThumbnailPreview();thumbnailMemory_.clear();
        thumbnailEntry_=state.entryId;thumbnailPath_=state.path;
        thumbnailMediaGeneration_=state.mediaGeneration;thumbnailOpenIntent_=state.mediaOpenIntentRevision;
    } else if(thumbnailWanted_) {
        if(!ThumbnailPreviewCurrent()) CancelThumbnailPreview();
        else UpdateThumbnailTime(thumbnailSeconds_);
    }
}
void MainWindow::BeginThumbnailRequest() {
    if (!ThumbnailPreviewCurrent() || thumbnailBusy_ || thumbnailPath_.empty() ||
        !preferences_.Boolean("enableThumbnailPreview") || iima::IsNetworkMediaPath(thumbnailPath_)) return;
    try {
    for (auto const& cached : thumbnailMemory_) {
        if (cached.requestedSeconds == thumbnailSeconds_) {
            auto copy = cached; copy.generation = thumbnailGeneration_;
            DisplayThumbnail(std::move(copy)); return;
        }
    }
    thumbnailBusy_ = true;
    thumbnailStop_ = std::stop_source{};
    iima::ThumbnailRequest request;
    request.path = std::filesystem::path(to_hstring(thumbnailPath_).c_str());
    request.seconds = thumbnailSeconds_;
    request.generation = thumbnailGeneration_;
    auto width = std::clamp(preferences_.Integer("thumbnailWidth"), std::int64_t(32), std::int64_t(512));
    request.maximumWidth = static_cast<std::uint32_t>(width * 2);
    request.maximumHeight = static_cast<std::uint32_t>(width * 2);
    auto weak = get_weak();
    auto queue = DispatcherQueue();
    auto stop = thumbnailStop_.get_token();
    auto maximumCacheMiB = preferences_.Integer("maxThumbnailPreviewCacheSize");
        std::thread([weak, queue, request = std::move(request), stop, maximumCacheMiB] {
            auto result = iima::ExtractThumbnailCached(request, maximumCacheMiB, stop);
            queue.TryEnqueue([weak, result = std::move(result)]() mutable {
                if (auto self = weak.get(); self && !self->closing_) {
                    self->thumbnailBusy_ = false;
                    try {
                    if (result.generation == self->thumbnailGeneration_ && self->ThumbnailPreviewCurrent()) {
                        if (result.status == iima::ThumbnailExtractionStatus::Ready) {
                            // Memory tier complements the source-keyed disk cache.
                            // Never retain these entries across media change.
                            self->thumbnailMemory_.push_back(result);
                            std::size_t bytes{};
                            for (auto const& image : self->thumbnailMemory_) bytes += image.jpeg.size();
                            while (self->thumbnailMemory_.size() > 24 || bytes > 16 * 1024 * 1024) {
                                bytes -= self->thumbnailMemory_.front().jpeg.size();
                                self->thumbnailMemory_.pop_front();
                            }
                            self->DisplayThumbnail(std::move(result));
                        }
                    } else if (self->ThumbnailPreviewCurrent()) self->thumbnailTimer_.Start();
                    } catch(...) { /* Keep lightweight hover text on image failure. */ }
                }
            });
        }).detach();
    } catch (...) {
        thumbnailBusy_ = false;
        // Preview failure never changes main playback or launches repeated work.
    }
}
winrt::fire_and_forget MainWindow::DisplayThumbnail(iima::ThumbnailExtractionResult result) {
    auto lifetime = get_strong();
    try {
        Windows::Storage::Streams::InMemoryRandomAccessStream stream;
        Windows::Storage::Streams::DataWriter writer(stream);
        writer.WriteBytes(array_view<std::uint8_t const>(result.jpeg));
        co_await writer.StoreAsync();
        writer.DetachStream();
        stream.Seek(0);
        Media::Imaging::BitmapImage image;
        co_await image.SetSourceAsync(stream);
        if (!ThumbnailPreviewCurrent() || !preferences_.Boolean("enableThumbnailPreview") || result.generation != thumbnailGeneration_) co_return;
        ThumbnailPreviewImage().Source(image);
        ThumbnailPreviewImage().Visibility(Visibility::Visible);
        ThumbnailPreviewBorder().Visibility(Visibility::Visible);
        UpdateControlBarOverlays();
    } catch (...) {
        if (!closing_ && result.generation == thumbnailGeneration_)
            ThumbnailPreviewImage().Visibility(Visibility::Collapsed);
    }
}
}

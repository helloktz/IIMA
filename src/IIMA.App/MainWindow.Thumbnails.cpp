// IINA PlayerCore.generateThumbnails / MainWindowController thumbnail hover.
// Copyright (c) lhc and contributors. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
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
    PositionSlider().PointerMoved([weak](auto const& sender, Input::PointerRoutedEventArgs const& args) {
        auto self = weak.get();
        if (!self || self->closing_ || !self->preferences_.Boolean("enableThumbnailPreview")) return;
        auto const& state = self->session_->Snapshot();
        if (state.media != iima::MediaState::Loaded || !state.duration || *state.duration <= 0) return;
        auto slider = sender.as<Controls::Slider>();
        auto width = slider.ActualWidth();
        if (width <= 0) return;
        const auto fraction = std::clamp(args.GetCurrentPoint(slider).Position().X / width, 0.0, 1.0);
        // Quantize requests to seconds, not pointer pixels. This is an on-demand
        // preview, not IINA's pre-generated whole-file FFmpeg thumbnail atlas.
        const auto seconds = std::floor(fraction * *state.duration);
        if (self->thumbnailWanted_ && seconds == self->thumbnailSeconds_) return;
        self->thumbnailStop_.request_stop();
        ++self->thumbnailGeneration_;
        self->thumbnailSeconds_ = seconds;
        self->thumbnailWanted_ = true;
        self->ThumbnailPreviewBorder().Visibility(Visibility::Collapsed);
        self->thumbnailTimer_.Stop();
        self->thumbnailTimer_.Start();
    });
    PositionSlider().PointerExited([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CancelThumbnailPreview();
    });
    PositionSlider().PointerCanceled([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->CancelThumbnailPreview();
    });
}
void MainWindow::CancelThumbnailPreview() {
    thumbnailStop_.request_stop();
    ++thumbnailGeneration_;
    thumbnailWanted_ = false;
    if (thumbnailTimer_) thumbnailTimer_.Stop();
    ThumbnailPreviewBorder().Visibility(Visibility::Collapsed);
    ThumbnailPreviewImage().Source(nullptr);
}
void MainWindow::UpdateThumbnailMedia(iima::PlaybackSnapshot const& state) {
    if (thumbnailEntry_ != state.entryId || thumbnailPath_ != state.path ||
        state.media != iima::MediaState::Loaded || !preferences_.Boolean("enableThumbnailPreview")) {
        CancelThumbnailPreview();
        thumbnailMemory_.clear();
        thumbnailEntry_ = state.entryId;
        thumbnailPath_ = state.path;
    }
}
void MainWindow::BeginThumbnailRequest() {
    if (closing_ || !thumbnailWanted_ || thumbnailBusy_ || thumbnailPath_.empty()) return;
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
    try {
        std::thread([weak, queue, request = std::move(request), stop, maximumCacheMiB] {
            auto result = iima::ExtractThumbnailCached(request, maximumCacheMiB, stop);
            queue.TryEnqueue([weak, result = std::move(result)]() mutable {
                if (auto self = weak.get(); self && !self->closing_) {
                    self->thumbnailBusy_ = false;
                    if (result.generation == self->thumbnailGeneration_ && self->thumbnailWanted_) {
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
                    } else if (self->thumbnailWanted_) self->thumbnailTimer_.Start();
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
        if (closing_ || !thumbnailWanted_ || result.generation != thumbnailGeneration_) co_return;
        ThumbnailPreviewImage().Source(image);
        ThumbnailPreviewTime().Text(to_hstring(iima::VideoTime(result.actualSeconds).StringRepresentation()));
        ThumbnailPreviewBorder().Visibility(Visibility::Visible);
        UpdateControlBarOverlays();
    } catch (...) {
        if (!closing_ && result.generation == thumbnailGeneration_)
            ThumbnailPreviewBorder().Visibility(Visibility::Collapsed);
    }
}
}

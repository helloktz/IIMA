// IINA PlayerCore.setCrop / InteractiveMode source-pixel crop rules.
// Copyright lhc and contributors. SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Geometry/CropSelection.h"
#include <charconv>
#include <cmath>
using namespace winrt;
namespace {
std::optional<int> Dimension(iima::MediaProperties const& properties, char const* key) {
    auto found = properties.find(key); if (found == properties.end()) return {};
    int value{};
    auto [end, error] = std::from_chars(found->second.data(), found->second.data() + found->second.size(), value);
    if (error != std::errc{} || end != found->second.data() + found->second.size() || value <= 0 || value > 65536) return {};
    return value;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireCropControls() {
    auto weak = get_weak();
    LoadCropSizeButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->LoadCropDraft();
    });
    ApplyCropButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplyCropDraft();
    });
    ResetCropButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->RemoveVideoFilter("iina_crop");
    });
}
void MainWindow::LoadCropDraft() {
    if (closing_) return;
    auto const& state = session_->Snapshot();
    auto width = Dimension(state.videoParameters, "w"), height = Dimension(state.videoParameters, "h");
    if (state.media != iima::MediaState::Loaded || !width || !height) {
        ShowError(L"尚未获得视频像素尺寸。"); return;
    }
    cropDraftEntry_ = state.entryId; cropDraftPath_ = state.path;
    cropDraftWidth_ = *width; cropDraftHeight_ = *height;
    CropX().Value(0); CropY().Value(0); CropWidth().Value(*width); CropHeight().Value(*height);
    CropSourceLabel().Text(to_hstring(*width) + L" × " + to_hstring(*height) + L" 源像素（左上原点）");
}
void MainWindow::ApplyCropDraft() {
    if (closing_ || !cropDraftEntry_) return;
    auto const& state = session_->Snapshot();
    if (state.media != iima::MediaState::Loaded || state.entryId != *cropDraftEntry_ || state.path != cropDraftPath_ ||
        Dimension(state.videoParameters, "w") != cropDraftWidth_ || Dimension(state.videoParameters, "h") != cropDraftHeight_) {
        ShowError(L"视频或尺寸已变化，请重新读取尺寸。"); return;
    }
    if (!state.videoFilters.complete) { ShowError(L"滤镜状态尚不可用。"); return; }
    // No guessing how arbitrary scale/rotate/crop filters transform coordinates.
    // The numerical editor is limited to the source or our single managed crop.
    std::optional<std::size_t> managed;
    for (std::size_t index = 0; index < state.videoFilters.entries.size(); ++index) {
        auto const& row = state.videoFilters.entries[index];
        if (!row.filter || !row.filter->Label() || *row.filter->Label() != "iina_crop" || managed) {
            ShowError(L"请先移除其他视频滤镜，再使用源像素裁剪。"); return;
        }
        managed = index;
    }
    iima::CropRect rect{CropX().Value(), CropY().Value(), CropWidth().Value(), CropHeight().Value()};
    // Numeric input is exact, unlike drag edge snapping. Out-of-range input is
    // rejected, not silently clipped to a different crop than the displayed form.
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0 ||
        rect.Right() > cropDraftWidth_ || rect.Bottom() > cropDraftHeight_ ||
        std::floor(rect.x) != rect.x || std::floor(rect.y) != rect.y ||
        std::floor(rect.width) != rect.width || std::floor(rect.height) != rect.height) {
        ShowError(L"请输入视频范围内的整数裁剪坐标和尺寸。"); return;
    }
    auto pixels = iima::QuantizeCropRect(rect, cropDraftWidth_, cropDraftHeight_, 0);
    if (!pixels) { ShowError(L"裁剪区域无效。"); return; }
    if (!pixels->x && !pixels->y && pixels->width == cropDraftWidth_ && pixels->height == cropDraftHeight_) {
        if (managed) session_->EditFilters(false, iima::FilterChainEditor::Remove(state.videoFilters, state.videoFilters.revision, *managed));
        return;
    }
    auto crop = iima::MPVFilter::Crop(pixels->width, pixels->height, pixels->x, pixels->y);
    iima::MPVFilter named(crop.Name(), std::string("iina_crop"), crop.Params());
    if (managed) session_->EditFilters(false, iima::FilterChainEditor::Replace(state.videoFilters, state.videoFilters.revision, *managed, named));
    else session_->AddFilter(false, named.StringFormat());
}
}

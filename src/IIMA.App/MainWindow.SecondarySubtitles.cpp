// IINA SidebarSubtitlesPane: TrackSelector, VisibilitySwitch, SubPositionDelayView.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/TrackPresentation.h"
#include <cmath>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireSecondarySubtitles() {
    auto weak = get_weak();
    SecondarySubtitleBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && !self->projectingSubtitles_) {
            if (auto item = self->SecondarySubtitleBox().SelectedItem().try_as<Controls::ComboBoxItem>())
                self->session_->SelectSecondarySubtitle(unbox_value<std::int64_t>(item.Tag()));
        }
    });
    SecondarySubtitlesVisibleCheck().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && !self->projectingSubtitles_) {
            auto value = self->SecondarySubtitlesVisibleCheck().IsChecked();
            if (value) self->session_->SetSecondarySubtitlesVisible(value.Value());
        }
    });
    LoadSecondarySubtitleButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->LoadSecondarySubtitleDraft();
    });
    ApplySecondarySubtitleButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplySecondarySubtitleDraft();
    });
    ResetSecondaryDelayButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_)
            self->session_->SetSecondarySubtitleDelay(0);
    });
}
void MainWindow::LoadSecondarySubtitleDraft() {
    if (closing_) return;
    auto const& state = session_->Snapshot();
    if (state.media != iima::MediaState::Loaded || !state.secondarySubtitleDelay ||
        !state.secondarySubtitlePosition || !state.secondarySubtitleScale) {
        ShowError(L"副字幕参数尚不可用，请等待播放器反馈。"); return;
    }
    secondaryDraftEntry_ = state.entryId;
    secondaryDraftPath_ = state.path;
    SecondarySubtitleDelayBox().Value(*state.secondarySubtitleDelay);
    SecondarySubtitlePositionBox().Value(*state.secondarySubtitlePosition);
    SecondarySubtitleScaleBox().Value(*state.secondarySubtitleScale);
    ApplySecondarySubtitleButton().IsEnabled(true);
}
void MainWindow::ApplySecondarySubtitleDraft() {
    if (closing_) return;
    auto const& state = session_->Snapshot();
    if (!secondaryDraftEntry_ || state.media != iima::MediaState::Loaded ||
        *secondaryDraftEntry_ != state.entryId || secondaryDraftPath_ != state.path) {
        ShowError(L"媒体已变化，请重新读取副字幕参数。"); return;
    }
    auto delay = SecondarySubtitleDelayBox().Value();
    auto position = SecondarySubtitlePositionBox().Value();
    auto scale = SecondarySubtitleScaleBox().Value();
    if (!std::isfinite(delay) || !std::isfinite(position) || !std::isfinite(scale) ||
        position < 0 || position > 150 || std::floor(position) != position || scale < 0 || scale > 100) {
        ShowError(L"延迟必须为有限数字，位置为0到150的整数，缩放为0到100。"); return;
    }
    // Independent mpv options, not an atomic transaction. Each command's error
    // is reported by the normal session path; observed labels remain truth.
    session_->SetSecondarySubtitleDelay(delay);
    session_->SetSubtitlePosition(static_cast<int>(position), false);
    session_->SetSubtitleScale(scale, false);
}
void MainWindow::UpdateSecondarySubtitles(iima::PlaybackSnapshot const& state) {
    struct Guard { bool& flag; explicit Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(projectingSubtitles_);
    const bool active = !closing_ && state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded;
    if (!active || (secondaryDraftEntry_ && (*secondaryDraftEntry_ != state.entryId || secondaryDraftPath_ != state.path)))
        secondaryDraftEntry_.reset();
    if (secondaryTracksRevision_ != state.tracksRevision) {
        secondaryTracksRevision_ = state.tracksRevision;
        SecondarySubtitleBox().Items().Clear();
        Controls::ComboBoxItem off;
        off.Content(box_value(L"关闭")); off.Tag(box_value(std::int64_t{0}));
        SecondarySubtitleBox().Items().Append(off);
        for (auto const& track : state.tracks) {
            if (track.type != "sub") continue;
            Controls::ComboBoxItem item;
            auto info = iima::TrackPresentation::FromMediaTrack(track);
            auto label = info ? info->ReadableTitle() : std::to_string(track.id);
            if (track.external) label += " [外部]";
            item.Content(box_value(to_hstring(label))); item.Tag(box_value(track.id));
            SecondarySubtitleBox().Items().Append(item);
        }
    }
    auto select = [](Controls::ComboBox box, std::optional<std::int64_t> id) {
        int index = -1;
        if (id) {
            if (*id == -2) index = 0; // mpv track property 'no', not missing state.
            else if (*id >= 0) for (std::uint32_t row = 1; row < box.Items().Size(); ++row) {
                auto item = box.Items().GetAt(row).as<Controls::ComboBoxItem>();
                if (unbox_value<std::int64_t>(item.Tag()) == *id) { index = static_cast<int>(row); break; }
            }
        }
        box.PlaceholderText(id && *id == -1 ? L"自动选择中" : L"状态未就绪");
        box.SelectedIndex(index);
    };
    // track-list.selected may be true for TWO subtitle tracks. Never infer the
    // primary slot by whichever selected row happened to be enumerated last.
    select(SubtitleBox(), state.subtitleId);
    select(SecondarySubtitleBox(), state.secondarySubtitleId);
    SubtitleBox().IsEnabled(active);
    SecondarySubtitleBox().IsEnabled(active);
    SecondarySubtitlesVisibleCheck().IsEnabled(active && state.secondarySubtitlesVisible.has_value());
    SecondarySubtitlesVisibleCheck().IsChecked(state.secondarySubtitlesVisible
        ? box_value(*state.secondarySubtitlesVisible).as<Windows::Foundation::IReference<bool>>() : nullptr);
    const bool valuesKnown = state.secondarySubtitleDelay && state.secondarySubtitlePosition && state.secondarySubtitleScale;
    LoadSecondarySubtitleButton().IsEnabled(active && valuesKnown);
    ApplySecondarySubtitleButton().IsEnabled(active && secondaryDraftEntry_.has_value() && valuesKnown);
    ResetSecondaryDelayButton().IsEnabled(active && state.secondarySubtitleDelay.has_value());
    auto value = [](std::optional<double> number) -> hstring { return number ? to_hstring(*number) : L"未知"; };
    SecondarySubtitleStatus().Text(L"当前延迟 " + value(state.secondarySubtitleDelay) + L" 秒；位置 " +
        value(state.secondarySubtitlePosition) + L"；缩放 " + value(state.secondarySubtitleScale));
    SubtitleLayoutStatus().Text(L"当前主字幕位置 " + value(state.subtitlePosition) + L"；缩放 " + value(state.subtitleScale));
    bool imageOnly = true, selectedAny = false, ass = false;
    for (auto const& track : state.tracks) {
        if (track.type != "sub" || ((!state.subtitleId || track.id != *state.subtitleId) &&
            (!state.secondarySubtitleId || track.id != *state.secondarySubtitleId))) continue;
        selectedAny = true;
        if (auto info = iima::TrackPresentation::FromMediaTrack(track)) {
            imageOnly = imageOnly && info->IsImageSubtitle();
            ass = ass || info->IsAssSubtitle();
        } else imageOnly = false;
    }
    SubtitleStyleWarning().Text(selectedAny && imageOnly ?
        L"图形字幕不支持文本样式，副字幕可能与主字幕重叠。" : ass ?
        L"ASS字幕的内置排版可能覆盖样式；缩放或位置调整可能破坏排版。" : L"");
    ApplySubtitleStyleButton().IsEnabled(active && (!selectedAny || !imageOnly));
}
}

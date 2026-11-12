// IINA MenuController.updateTracks/updateChapterList and AppDelegate recent documents.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
// Windows recent items use IIMA's existing playback history, not NSDocumentController.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/TrackPresentation.h"
#include "../IIMA.Core/Media/MediaChapter.h"
#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
// A normal item with an observed check icon does not optimistically toggle when
// clicked. Only a fresh snapshot used to rebuild the menu changes its checkmark.
Controls::MenuFlyoutItem DynamicItem(hstring const& text, bool enabled, bool checked = false) {
    Controls::MenuFlyoutItem item;
    item.Text(text);
    item.IsEnabled(enabled);
    if (checked) item.Icon(Controls::SymbolIcon{Controls::Symbol::Accept});
    return item;
}
std::optional<std::int64_t> SelectedTrack(iima::PlaybackSnapshot const& state,
    std::string const& type, bool secondary) {
    if (secondary) return state.secondarySubtitleId;
    if (type == "video") return state.videoId;
    if (type == "audio") return state.audioId;
    return state.subtitleId;
}
void ShowMenuTarget(Controls::Control const& target) {
    for (DependencyObject parent = target; parent; parent = Media::VisualTreeHelper::GetParent(parent))
        if (auto expander = parent.try_as<Controls::Expander>()) expander.IsExpanded(true);
    auto weak = make_weak(target);
    target.DispatcherQueue().TryEnqueue([weak] {
        if (auto current = weak.get(); current && current.IsLoaded()) {
            current.StartBringIntoView();
            current.Focus(FocusState::Programmatic);
        }
    });
}
bool TrackIdentityMatches(iima::MediaTrack const& current, iima::MediaTrack const& captured) {
    // Selection is mutable state, not identity. A replaced external track with
    // a reused numeric ID must not receive an action from an old open menu.
    return current.id == captured.id && current.type == captured.type &&
        current.sourceId == captured.sourceId && current.external == captured.external &&
        current.externalPath == captured.externalPath && current.title == captured.title &&
        current.lang == captured.lang;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::FillTrackMenu(Controls::MenuFlyoutSubItem const& menu, std::string type, bool secondary) {
    menu.Items().Clear();
    if ((type != "video" && type != "audio" && type != "sub") || (secondary && type != "sub")) return;
    auto const& state = session_->Snapshot();
    const bool enabled = !closing_ && userDataLoaded_ && !pickerOpen_ &&
        state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded &&
        !session_->PlaylistEditBusy();
    const auto generation = state.mediaGeneration, intent = state.mediaOpenIntentRevision;
    const auto selected = SelectedTrack(state, type, secondary);
    auto weak = get_weak();
    auto append = [&](hstring const& label, std::int64_t id, std::optional<iima::MediaTrack> track) {
        const bool checked = selected && (id == 0 ? (*selected == 0 || *selected == -2) : *selected == id);
        auto item = DynamicItem(label, enabled, checked);
        item.Click([weak, generation, intent, type, secondary, id, track = std::move(track)](auto const&, auto const&) {
            auto self = weak.get();
            if (!self || self->closing_ || !self->userDataLoaded_ || self->pickerOpen_ || self->interactiveCrop_.Active() || self->cropOriginal_) return;
            auto const& current = self->session_->Snapshot();
            if (current.session != iima::SessionState::Ready || current.media != iima::MediaState::Loaded ||
                current.mediaGeneration != generation || current.mediaOpenIntentRevision != intent ||
                self->session_->PlaylistEditBusy()) {
                self->ShowError(L"媒体已变化，请重新打开轨道菜单。"); return;
            }
            if (track && std::none_of(current.tracks.begin(), current.tracks.end(), [&](auto const& value) {
                return TrackIdentityMatches(value, *track);
            })) { self->ShowError(L"轨道列表已变化，请重新打开菜单。"); return; }
            if (secondary) self->session_->SelectSecondarySubtitle(id);
            else self->session_->SelectTrack(type, id);
        });
        menu.Items().Append(item);
    };
    append(L"关闭", 0, {});
    std::size_t shown{}, total{};
    const iima::TrackLabelStrings strings{"无标题", "关闭", "默认"};
    for (auto const& track : state.tracks) {
        if (track.type != type || track.id <= 0) continue;
        ++total;
        if (shown == 99) continue; // 99 tracks plus the off slot: 100 selections.
        auto presentation = iima::TrackPresentation::FromMediaTrack(track);
        auto label = presentation ? presentation->ReadableTitle(strings, [](std::string_view code) {
            return std::optional<std::string>{std::string(code)};
        }) : "#" + std::to_string(track.id);
        if (track.external) label += " [外部]";
        append(to_hstring(label), track.id, track);
        ++shown;
    }
    if (total > shown) {
        auto more = DynamicItem(L"仅显示前99条轨道；打开侧栏查看全部", !closing_ && userDataLoaded_ && !pickerOpen_);
        more.Click([weak, type, secondary](auto const&, auto const&) {
            if (auto self = weak.get(); self && !self->closing_ && self->userDataLoaded_ && !self->pickerOpen_ && !self->interactiveCrop_.Active() && !self->cropOriginal_) {
                auto target = type == "video" ? self->VideoTrackBox() : type == "audio" ? self->AudioBox() :
                    secondary ? self->SecondarySubtitleBox() : self->SubtitleBox();
                ShowMenuTarget(target);
            }
        });
        menu.Items().Append(Controls::MenuFlyoutSeparator{});
        menu.Items().Append(more);
    }
}
void MainWindow::FillChapterMenu(Controls::MenuFlyoutSubItem const& menu) {
    menu.Items().Clear();
    auto const& state = session_->Snapshot();
    const auto generation = state.mediaGeneration, intent = state.mediaOpenIntentRevision;
    const bool enabled = !closing_ && userDataLoaded_ && !pickerOpen_ && state.seekable &&
        state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded &&
        !session_->PlaylistEditBusy();
    auto weak = get_weak();
    const auto count = std::min<std::size_t>(100, state.chapters.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto const chapter = state.chapters[index];
        const bool valid = std::isfinite(chapter.time) && chapter.time >= 0;
        iima::MediaChapter presentation{chapter.title.empty() ? std::nullopt : std::optional{chapter.title},
            valid ? chapter.time : 0, chapter.index};
        auto label = to_hstring(presentation.Time().StringRepresentation() + "  " + presentation.Title("章节"));
        auto item = DynamicItem(label, enabled && valid,
            state.chapterIndex && *state.chapterIndex == chapter.index);
        item.Click([weak, generation, intent, index, chapter](auto const&, auto const&) {
            auto self = weak.get();
            if (!self || self->closing_ || !self->userDataLoaded_ || self->pickerOpen_ || self->interactiveCrop_.Active() || self->cropOriginal_) return;
            auto const& current = self->session_->Snapshot();
            if (current.session != iima::SessionState::Ready || current.media != iima::MediaState::Loaded ||
                !current.seekable || current.mediaGeneration != generation || current.mediaOpenIntentRevision != intent ||
                self->session_->PlaylistEditBusy() || index >= current.chapters.size() ||
                !(current.chapters[index] == chapter)) {
                self->ShowError(L"媒体或章节已变化，请重新打开章节菜单。"); return;
            }
            self->session_->SelectChapter(index);
        });
        menu.Items().Append(item);
    }
    if (state.chapters.empty()) menu.Items().Append(DynamicItem(L"暂无章节", false));
    if (state.chapters.size() > count) {
        auto more = DynamicItem(L"仅显示前100章；打开章节侧栏查看全部", !closing_ && userDataLoaded_ && !pickerOpen_);
        more.Click([weak](auto const&, auto const&) {
            if (auto self = weak.get(); self && !self->closing_ && self->userDataLoaded_ && !self->pickerOpen_ && !self->interactiveCrop_.Active() && !self->cropOriginal_)
                ShowMenuTarget(self->ChapterBox());
        });
        menu.Items().Append(Controls::MenuFlyoutSeparator{});
        menu.Items().Append(more);
    }
}
void MainWindow::FillRecentMenu(Controls::MenuFlyoutSubItem const& menu) {
    menu.Items().Clear();
    auto weak = get_weak();
    const auto count = userDataLoaded_ ? std::min<std::size_t>(12, userData_.history.size()) : 0;
    for (std::size_t index = 0; index < count; ++index) {
        auto const path = userData_.history[index].path;
        const auto availability = historyAvailability_.find(path);
        const bool missing = availability != historyAvailability_.end() && availability->second == iima::HistoryAvailability::Missing;
        auto label = to_hstring(iima::HistoryFileName(path));
        if (missing) label = label + L"（上次检查缺失）";
        auto item = DynamicItem(label, !closing_ && !pickerOpen_ && !historyDeletionBusy_ && !playlistDeletionBusy_ && !path.empty() && !missing);
        Controls::ToolTipService::SetToolTip(item, box_value(to_hstring(path)));
        item.Click([weak, path](auto const&, auto const&) {
            auto self = weak.get();
            if (!self || self->closing_ || !self->userDataLoaded_ || self->pickerOpen_ || self->interactiveCrop_.Active() || self->cropOriginal_ ||
                self->historyDeletionBusy_ || self->playlistDeletionBusy_) return;
            if (std::none_of(self->userData_.history.begin(), self->userData_.history.end(),
                [&](auto const& entry) { return entry.path == path; })) {
                self->ShowError(L"此历史记录已移除，请重新打开最近播放菜单。"); return;
            }
            auto found = self->historyAvailability_.find(path);
            if (found != self->historyAvailability_.end() && found->second == iima::HistoryAvailability::Missing) {
                self->ShowError(L"上次检查未找到此文件，请在历史侧栏刷新后重试。"); return;
            }
            self->QueueFiles({path}, false, true);
        });
        menu.Items().Append(item);
    }
    if (!count) menu.Items().Append(DynamicItem(userDataLoaded_ ? L"暂无最近播放记录" : L"正在读取最近播放记录…", false));
    if (userDataLoaded_ && userData_.history.size() > count) {
        auto more = DynamicItem(L"仅显示最近12项；打开历史侧栏查看全部", !closing_ && !pickerOpen_);
        more.Click([weak](auto const&, auto const&) {
            if (auto self = weak.get(); self && !self->closing_ && self->userDataLoaded_ && !self->pickerOpen_ && !self->interactiveCrop_.Active() && !self->cropOriginal_)
                ShowMenuTarget(self->HistoryView());
        });
        menu.Items().Append(Controls::MenuFlyoutSeparator{});
        menu.Items().Append(more);
    }
}
}

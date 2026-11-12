// Playback semantics follow IINA PlayerCore.swift / SidebarPlaylistPane.swift.
// Copyright © 2016 lhc. Adapted logic: GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/MediaChapter.h"
#include "../IIMA.Core/Media/TrackPresentation.h"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <unordered_set>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.System.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
constexpr double speeds[]{0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
std::string Filename(std::string const& path)
{
    auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
hstring TrackLabel(iima::MediaTrack const& track)
{
    auto presentation = iima::TrackPresentation::FromMediaTrack(track);
    if (!presentation) return to_hstring(track.id);
    iima::TrackLabelStrings strings{"无标题", "关闭", "默认"};
    auto name = presentation->ReadableString(strings, [](std::string_view code) {
        // Until localized ISO639 resources are wired, retain the actual code.
        return std::optional<std::string>{std::string(code)};
    });
    if (track.external) name += " [外部]";
    return to_hstring(name);
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WirePlaybackControls()
{
    auto weak = get_weak();
    RevealPlaylistButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RevealSelection(false);
    });
    SortPlaylistButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->SortPlaylist(iima::PlaylistSort::DisplayNameAscending);
    });
    ShufflePlaylistButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->ShufflePlaylist(self->session_->PlaylistShuffled());
    });
    ClearPlaylistButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->ClearPlaylist();
    });
    SavePlaylistButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ExecuteApplicationCommand(iima::ApplicationCommand::SavePlaylist);
    });
    AppendButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->OpenFile(true); });
    SubtitleButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->OpenSubtitle(); });
    PreviousButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->session_->PlaylistPrevious(); });
    NextButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->session_->PlaylistNext(); });
    PlaylistView().DoubleTapped([weak](auto const&, Input::DoubleTappedRoutedEventArgs const& args) {
        if (auto self = weak.get(); self && !self->closing_) {
            // A double click targets the clicked row, not an unrelated first
            // selected row left over from an Extended Ctrl/Shift selection.
            auto element = args.OriginalSource().try_as<DependencyObject>();
            while (element && element != self->PlaylistView()) {
                if (auto item = element.try_as<Controls::ListViewItem>()) {
                    self->PlaylistView().SelectedItems().Clear();
                    self->PlaylistView().SelectedItems().Append(item);
                    self->PlaySelected();
                    args.Handled(true);
                    break;
                }
                element = Media::VisualTreeHelper::GetParent(element);
            }
        }
    });
    PlaySelectedButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->PlaySelected(); });
    RemoveButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RemoveSelectedPlaylistItems();
    });
    PlaySelectedNewWindowButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get()) self->PlaySelectedPlaylistInNewWindow();
    });
    OpenPlaylistBrowserButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get()) self->OpenSelectedPlaylistInBrowser();
    });
    QueueSelectedNextButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->QueueSelectedPlaylistNext();
    });
    RefreshPlaylistDurationsButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get();self && !self->closing_) self->UpdatePlaylistDurations(true);
    });
    PlaylistView().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->updating_ && !self->closing_)
            self->UpdatePlaylistSelection();
    });
    MoveUpButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->MoveSelected(-1); });
    MoveDownButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->MoveSelected(1); });
    auto trackChanged = [weak](Controls::ComboBox const& box, std::string type) {
        if (auto self = weak.get(); self && !self->updating_ && !self->closing_) {
            if (auto item = box.SelectedItem().try_as<Controls::ComboBoxItem>())
                self->session_->SelectTrack(type, unbox_value<int64_t>(item.Tag()));
        }
    };
    VideoTrackBox().SelectionChanged([trackChanged](auto const& box, auto const&) {
        trackChanged(box.template as<Controls::ComboBox>(), "video");
    });
    CycleVideoTrackButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->CycleTrack("video");
    });
    CycleAudioTrackButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->CycleTrack("audio");
    });
    CycleSubtitleTrackButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->CycleTrack("sub");
    });
    AudioBox().SelectionChanged([trackChanged](auto const& box, auto const&) { trackChanged(box.template as<Controls::ComboBox>(), "audio"); });
    SubtitleBox().SelectionChanged([trackChanged](auto const& box, auto const&) { trackChanged(box.template as<Controls::ComboBox>(), "sub"); });
    PreviousChapterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->StepChapter(-1);
    });
    NextChapterButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) self->session_->StepChapter(1);
    });
    ChapterBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->updating_ && !self->closing_ && self->ChapterBox().SelectedIndex() >= 0)
            self->session_->SelectChapter(static_cast<size_t>(self->ChapterBox().SelectedIndex()));
    });
    for (auto speed : speeds) SpeedBox().Items().Append(box_value(to_hstring(speed) + L"×"));
    SpeedBox().SelectionChanged([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->updating_ && !self->closing_) {
            auto index = self->SpeedBox().SelectedIndex();
            if (index >= 0 && index < static_cast<int>(std::size(speeds))) {
                self->session_->SetSpeed(speeds[index]);
                self->userData_.settings.speed = speeds[index]; self->MarkUserDataDirty();
            }
        }
    });
    VolumeSlider().ValueChanged([weak](auto const&, auto const& args) {
        if (auto self = weak.get(); self && !self->updating_ && !self->closing_) {
            self->session_->SetVolume(args.NewValue());
        }
    });

}
void MainWindow::PlaySelected()
{
    auto rows = SelectedPlaylistRows();
    // Like a table's first selected row, playback starts at the topmost selected
    // entry; multi-selection does not create a replacement playlist.
    if (!rows.empty()) session_->PlaylistPlay(rows.front());
}
void MainWindow::MoveSelected(int delta)
{
    auto rows = SelectedPlaylistRows();
    if (rows.size() != 1) return; // Batch movement uses the explicit Play Next operation.
    auto target = static_cast<std::int64_t>(rows.front()) + delta;
    if (target >= 0 && target < static_cast<std::int64_t>(playlist_.size()))
        session_->PlaylistMove(rows.front(), static_cast<std::size_t>(target));
}
void MainWindow::ApplyCollections(iima::PlaybackSnapshot const& state)
{
    auto editing = session_->PlaylistEditBusy();
    LoadExternalAudioButton().IsEnabled(!closing_ && !editing &&
        state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded);
    SortPlaylistButton().IsEnabled(!editing);
    ShufflePlaylistButton().IsEnabled(!editing);
    ClearPlaylistButton().IsEnabled(!editing);
    ShufflePlaylistButton().Content(box_value(session_->PlaylistShuffled() ? L"恢复顺序" : L"随机排序"));
    // Called under updating_: property projections must never issue commands.
    if (playlistRevision_ != state.playlistRevision) {
        playlistRevision_ = state.playlistRevision;
        auto selected = SelectedPlaylistIds();
        std::unordered_set<std::int64_t> selectedIds(selected.begin(), selected.end());
        playlist_ = state.playlist;
        PlaylistView().Items().Clear();
        for (size_t i = 0; i < playlist_.size(); ++i) {
            auto const& entry = playlist_[i];
            auto text = (entry.current ? L"▶ " : L"") + to_hstring(entry.title.empty() ? Filename(entry.path) : entry.title);
            Controls::ListViewItem item;
            Controls::Grid row;
            Controls::ColumnDefinition titleColumn, durationColumn;
            titleColumn.Width(GridLength{1,GridUnitType::Star});
            durationColumn.Width(GridLength{1,GridUnitType::Auto});
            row.ColumnDefinitions().Append(titleColumn); row.ColumnDefinitions().Append(durationColumn);
            Controls::TextBlock title, duration;
            title.Text(text); title.TextTrimming(TextTrimming::CharacterEllipsis);
            duration.Text(L"—"); duration.Margin(Thickness{8,0,0,0});
            Controls::Grid::SetColumn(duration,1);
            row.Children().Append(title); row.Children().Append(duration);
            Controls::RowDefinition headingRow, progressRow;
            headingRow.Height(GridLength{1,GridUnitType::Auto});
            progressRow.Height(GridLength{1,GridUnitType::Auto});
            row.RowDefinitions().Append(headingRow); row.RowDefinitions().Append(progressRow);
            Controls::StackPanel progress;
            progress.Spacing(2); progress.Margin(Thickness{0,3,0,3});
            progress.IsHitTestVisible(false); progress.Visibility(Visibility::Collapsed);
            Controls::TextBlock progressText; progressText.FontSize(11);
            Controls::ProgressBar progressBar; progressBar.Minimum(0); progressBar.Maximum(100);
            progressBar.Height(3); progressBar.IsIndeterminate(false);
            progress.Children().Append(progressText); progress.Children().Append(progressBar);
            Controls::Grid::SetRow(progress,1); Controls::Grid::SetColumnSpan(progress,2);
            row.Children().Append(progress);
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            item.Content(row);
            item.Tag(box_value(entry.id));
            PlaylistView().Items().Append(item);
            if (selectedIds.contains(entry.id) ||
                (selected.empty() && entry.current)) PlaylistView().SelectedItems().Append(item);
        }
    }
    UpdatePlaylistSelection();
    if (tracksRevision_ != state.tracksRevision) {
        tracksRevision_ = state.tracksRevision;
        tracks_ = state.tracks;
        auto populate = [this](Controls::ComboBox const& box, std::string const& type) {
            box.Items().Clear();
            Controls::ComboBoxItem off; off.Content(box_value(L"关闭")); off.Tag(box_value(int64_t{0}));
            box.Items().Append(off);
            int selected = 0;
            for (auto const& track : tracks_) {
                if (track.type != type) continue;
                Controls::ComboBoxItem item; item.Content(box_value(TrackLabel(track))); item.Tag(box_value(track.id));
                box.Items().Append(item);
                if (track.selected) selected = static_cast<int>(box.Items().Size() - 1);
            }
            box.SelectedIndex(selected);
        };
        populate(VideoTrackBox(), "video"); populate(AudioBox(), "audio"); populate(SubtitleBox(), "sub");
    }
    // vid/aid are authoritative even when track-list has not changed yet.
    // Unknown or auto is not falsely projected as the explicit Off item.
    auto projectTrack = [](Controls::ComboBox const& box, std::optional<std::int64_t> id) {
        int selected = -1;
        if (id && (*id == -2 || *id > 0)) {
            auto tag = *id == -2 ? std::int64_t{0} : *id;
            for (std::uint32_t index = 0; index < box.Items().Size(); ++index) {
                auto item = box.Items().GetAt(index).try_as<Controls::ComboBoxItem>();
                if (item && unbox_value<std::int64_t>(item.Tag()) == tag) {
                    selected = static_cast<int>(index); break;
                }
            }
        }
        box.SelectedIndex(selected);
    };
    projectTrack(VideoTrackBox(), state.videoId);
    projectTrack(AudioBox(), state.audioId);
    const auto canSelectTrack = state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded;
    VideoTrackBox().IsEnabled(canSelectTrack); AudioBox().IsEnabled(canSelectTrack);
    CycleVideoTrackButton().IsEnabled(canSelectTrack);
    CycleAudioTrackButton().IsEnabled(canSelectTrack);
    CycleSubtitleTrackButton().IsEnabled(canSelectTrack);
    if (chaptersRevision_ != state.chaptersRevision) {
        chaptersRevision_ = state.chaptersRevision;
        chapters_ = state.chapters;
        ChapterBox().Items().Clear();
        for (std::size_t row = 0; row < chapters_.size(); ++row) {
            auto const& chapter = chapters_[row];
            iima::MediaChapter model(chapter.title, chapter.time, chapter.index);
            auto next = row + 1 < chapters_.size()
                ? iima::VideoTime(chapters_[row + 1].time) : iima::VideoTime::Infinite();
            ChapterBox().Items().Append(box_value(to_hstring(model.RowTitle(row, "章节")) +
                L"  " + to_hstring(model.IntervalLabel(next))));
        }
    }
    int currentChapter = -1;
    if (state.chapterIndex && *state.chapterIndex >= 0 &&
        static_cast<std::uint64_t>(*state.chapterIndex) < state.chapters.size())
        currentChapter = static_cast<int>(*state.chapterIndex);
    ChapterBox().SelectedIndex(currentChapter);
    const bool canNavigateChapters = canSelectTrack && state.seekable && !state.chapters.empty() && !editing;
    ChapterBox().IsEnabled(canNavigateChapters);
    PreviousChapterButton().IsEnabled(canNavigateChapters);
    NextChapterButton().IsEnabled(canNavigateChapters);
    int speedIndex = -1;
    for (int i = 0; i < static_cast<int>(std::size(speeds)); ++i) if (std::abs(state.speed - speeds[i]) < 0.001) speedIndex = i;
    SpeedBox().SelectedIndex(speedIndex);
    VolumeSlider().Maximum(state.maxVolume);
    VolumeSlider().Value(state.volume);
}
}

// Playback semantics follow IINA PlayerCore.swift / SidebarPlaylistPane.swift.
// Copyright © 2016 lhc. Adapted logic: GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/MediaChapter.h"
#include "../IIMA.Core/Media/TrackPresentation.h"
#include <algorithm>
#include <cmath>
#include <tuple>
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
    PlaylistView().DoubleTapped([weak](auto const&, auto const&) { if (auto self = weak.get()) self->PlaySelected(); });
    PlaySelectedButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->PlaySelected(); });
    RemoveButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->PlaylistView().SelectedIndex() >= 0)
            self->session_->PlaylistRemove(static_cast<size_t>(self->PlaylistView().SelectedIndex()));
    });
    MoveUpButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->MoveSelected(-1); });
    MoveDownButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->MoveSelected(1); });
    auto trackChanged = [weak](Controls::ComboBox const& box, std::string type) {
        if (auto self = weak.get(); self && !self->updating_ && !self->closing_) {
            if (auto item = box.SelectedItem().try_as<Controls::ComboBoxItem>())
                self->session_->SelectTrack(type, unbox_value<int64_t>(item.Tag()));
        }
    };
    AudioBox().SelectionChanged([trackChanged](auto const& box, auto const&) { trackChanged(box.template as<Controls::ComboBox>(), "audio"); });
    SubtitleBox().SelectionChanged([trackChanged](auto const& box, auto const&) { trackChanged(box.template as<Controls::ComboBox>(), "sub"); });
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
            self->userData_.settings.volume = args.NewValue(); self->MarkUserDataDirty();
        }
    });
    auto addKey = [this](Windows::System::VirtualKey key, std::function<void()> action) {
        Input::KeyboardAccelerator accelerator;
        accelerator.Key(key); accelerator.Modifiers(Windows::System::VirtualKeyModifiers::Control);
        accelerator.Invoked([action](auto const&, auto const& args) { action(); args.Handled(true); });
        Root().KeyboardAccelerators().Append(accelerator);
    };
    addKey(Windows::System::VirtualKey::Right, [weak] { if (auto self = weak.get()) self->session_->PlaylistNext(); });
    addKey(Windows::System::VirtualKey::Left, [weak] { if (auto self = weak.get()) self->session_->PlaylistPrevious(); });
    addKey(Windows::System::VirtualKey::L, [weak] { if (auto self = weak.get()) self->OpenFile(true); });
}
void MainWindow::PlaySelected()
{
    auto index = PlaylistView().SelectedIndex();
    if (!closing_ && index >= 0) session_->PlaylistPlay(static_cast<size_t>(index));
}
void MainWindow::MoveSelected(int delta)
{
    auto index = PlaylistView().SelectedIndex();
    auto target = index + delta;
    if (!closing_ && index >= 0 && target >= 0 && target < static_cast<int>(playlist_.size()))
        session_->PlaylistMove(static_cast<size_t>(index), static_cast<size_t>(target));
}
void MainWindow::ApplyCollections(iima::PlaybackSnapshot const& state)
{
    auto editing = session_->PlaylistEditBusy();
    SortPlaylistButton().IsEnabled(!editing);
    ShufflePlaylistButton().IsEnabled(!editing);
    ClearPlaylistButton().IsEnabled(!editing);
    ShufflePlaylistButton().Content(box_value(session_->PlaylistShuffled() ? L"恢复顺序" : L"随机排序"));
    // Called under updating_: property projections must never issue commands.
    if (playlistRevision_ != state.playlistRevision) {
        playlistRevision_ = state.playlistRevision;
        std::optional<int64_t> selected;
        auto index = PlaylistView().SelectedIndex();
        if (index >= 0 && index < static_cast<int>(playlist_.size())) selected = playlist_[index].id;
        playlist_ = state.playlist;
        PlaylistView().Items().Clear();
        int nextSelection = -1;
        for (size_t i = 0; i < playlist_.size(); ++i) {
            auto const& entry = playlist_[i];
            auto text = (entry.current ? L"▶ " : L"") + to_hstring(entry.title.empty() ? Filename(entry.path) : entry.title);
            PlaylistView().Items().Append(box_value(text));
            if ((selected && entry.id == *selected) || (!selected && entry.current)) nextSelection = static_cast<int>(i);
        }
        PlaylistView().SelectedIndex(nextSelection);
    }
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
        populate(AudioBox(), "audio"); populate(SubtitleBox(), "sub");
    }
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
    ChapterBox().SelectedIndex(state.chapterIndex ? static_cast<int>(*state.chapterIndex) : -1);
    int speedIndex = -1;
    for (int i = 0; i < static_cast<int>(std::size(speeds)); ++i) if (std::abs(state.speed - speeds[i]) < 0.001) speedIndex = i;
    SpeedBox().SelectedIndex(speedIndex);
    VolumeSlider().Maximum(state.maxVolume);
    VolumeSlider().Value(state.volume);
}
}

// IINA MenuController menuNeedsUpdate/updateKeyEquivalentsFrom and native actions.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Input/MenuKeyBinding.h"
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool MenuKeyAvailable(iima::KeyMapping const& binding) {
    std::string key = binding.NormalizedMpvKey();
    const auto original = key;
    bool stripped = true;
    while (stripped) {
        stripped = false;
        for (auto prefix : {"Ctrl+", "Alt+", "Shift+"})
            if (key.starts_with(prefix)) { key.erase(0, std::char_traits<char>::length(prefix)); stripped = true; break; }
    }
    if (key == "TAB" || original == "Alt+F4" || original == "Alt+SPACE" ||
        original == "Ctrl+Alt+DEL" || original == "Ctrl+Shift+ESC" || original == "Ctrl+ESC" || original == "Alt+ESC") return false;
    static const std::set<std::string> names{"SPACE","ENTER","ESC","BS","DEL","INS","HOME","END",
        "PGUP","PGDWN","LEFT","RIGHT","UP","DOWN","KP_ENTER","KP_DEC","KP_MULTIPLY","KP_ADD",
        "KP_SUBTRACT","KP_DIVIDE","KP0","KP1","KP2","KP3","KP4","KP5","KP6","KP7","KP8","KP9","PLUS","SHARP"};
    bool supported = names.contains(key);
    if (!supported && key.starts_with('F')) {
        unsigned number{};
        auto [end, error] = std::from_chars(key.data()+1, key.data()+key.size(), number);
        supported = error == std::errc{} && end == key.data()+key.size() && number >= 1 && number <= 24 && key == "F" + std::to_string(number);
    }
    if (!supported) {
        try {
            auto text = to_hstring(key);
            supported = (text.size() == 1 && text[0] >= 32) ||
                (text.size() == 2 && text[0] >= 0xd800 && text[0] <= 0xdbff && text[1] >= 0xdc00 && text[1] <= 0xdfff);
        } catch (...) { return false; }
    }
    if (!supported) return false;
    auto action = iima::ClassifyBindingAction(binding);
    // The typed dispatcher does not yet accept old four-argument seeks or a
    // bare exact flag. Do not advertise those candidates as working shortcuts.
    if (!action.tokens.empty() && action.tokens[0] == "seek")
        return action.tokens.size() == 2 || (action.tokens.size() == 3 &&
            (action.tokens[2] == "relative" || action.tokens[2] == "relative+exact"));
    return true;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WirePlayerMenus() {
    auto weak = get_weak();
    auto top = [this](hstring const& title) {
        Controls::MenuBarItem menu; menu.Title(title); ApplicationMenuBar().Items().Append(menu); return menu;
    };
    auto sub = [](auto const& parent, hstring const& title) {
        Controls::MenuFlyoutSubItem menu; menu.Text(title); parent.Items().Append(menu); return menu;
    };
    bool buildingMediaMenu = false;
    auto add = [this, weak, &buildingMediaMenu](auto const& parent, hstring title, std::function<void()> execute,
        std::function<bool()> enabled = {}, std::vector<std::string> action = {}, bool application = false,
        std::function<bool()> checked = {}, std::function<hstring()> dynamicTitle = {}) {
        PlayerMenuAction row;
        row.item = Controls::MenuFlyoutItem{};
        row.title = dynamicTitle ? std::move(dynamicTitle) : std::function<hstring()>{[title] { return title; }};
        row.execute = std::move(execute); row.enabled = std::move(enabled); row.checked = std::move(checked);
        row.action = std::move(action); row.application = application;
        row.mediaBound = buildingMediaMenu || (application && !row.action.empty() &&
            (row.action[0] == "delete-current-file" || row.action[0] == "show-current-file-in-finder" ||
             row.action[0] == "save-downloaded-sub"));
        const auto index = playerMenuActions_.size();
        row.item.Loaded([weak, index](auto const&, auto const&) {
            if (auto self = weak.get(); self && index < self->playerMenuActions_.size()) {
                if (!self->playerMenuVisibleItems_.insert(index).second) return;
                auto& visible = self->playerMenuActions_[index];
                visible.menuGeneration = self->session_->Snapshot().mediaGeneration;
                visible.menuIntent = self->session_->Snapshot().mediaOpenIntentRevision;
            }
        });
        row.item.Unloaded([weak, index](auto const&, auto const&) {
            auto self = weak.get();
            if (!self) return;
            self->playerMenuVisibleItems_.erase(index);
            if (self->closing_ || !self->playerMenuVisibleItems_.empty() || self->playerMenuRefreshQueued_) return;
            self->playerMenuRefreshQueued_ = true;
            if (!self->DispatcherQueue().TryEnqueue([weak] {
                if (auto current = weak.get()) {
                    current->playerMenuRefreshQueued_ = false;
                    if (!current->closing_) current->RefreshPlayerMenus();
                }
            })) self->playerMenuRefreshQueued_ = false;
        });
        row.item.Click([weak, index](auto const&, auto const&) {
            auto self = weak.get();
            if (!self || self->closing_ || !self->userDataLoaded_ || self->pickerOpen_ || self->interactiveCrop_.Active() || self->cropOriginal_ || index >= self->playerMenuActions_.size()) return;
            // Recheck live availability. Opening a menu never reserves media or
            // grants permission for a destructive/file action after state changes.
            auto selected = self->playerMenuActions_[index];
            if (selected.mediaBound && (selected.menuGeneration != self->session_->Snapshot().mediaGeneration ||
                selected.menuIntent != self->session_->Snapshot().mediaOpenIntentRevision)) {
                self->ShowError(iima::loc::Text(L"TheMediaHasChangedPleaseReopenThe")); return;
            }
            if (selected.enabled && !selected.enabled()) { self->RefreshPlayerMenus(); return; }
            try {
                if (selected.binding) self->DispatchKeyBinding(*selected.binding);
                else if (selected.execute) selected.execute();
            } catch (hresult_error const& error) { self->ShowError(error.message()); }
              catch (std::exception const& error) { self->ShowError(to_hstring(error.what())); }
            self->RefreshPlayerMenus();
        });
        parent.Items().Append(row.item);
        playerMenuActions_.push_back(std::move(row));
    };
    auto app = [this, &add](auto const& parent, hstring const& title, iima::ApplicationCommand command) {
        add(parent, title, [this, command] { ExecuteApplicationCommand(command); },
            [this, command] { return commandRouter_.CanExecute(command); },
            {std::string(iima::ApplicationCommandName(command))}, true);
    };
    auto loaded = [this] { auto const& s = session_->Snapshot(); return s.session == iima::SessionState::Ready && s.media == iima::MediaState::Loaded; };
    auto seekable = [this, loaded] { return loaded() && session_->Snapshot().seekable; };
    auto video = [this, loaded] {
        auto const& state = session_->Snapshot();
        return loaded() && state.voConfigured && std::ranges::any_of(state.tracks, [](auto const& track) {
            return track.type == "video" && track.selected;
        });
    };
    auto normalVideo = [this, video] {
        auto normal = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
        return video() && !musicMode_.IsMusic() && !interactiveCrop_.Active() && !cropOriginal_ && normal &&
            normal.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Restored;
    };
    auto file = top(iima::loc::Text(L"File"));
    app(file, iima::loc::Text(L"OpenFile"), iima::ApplicationCommand::OpenFile);
    add(file, iima::loc::Text(L"OpenFolder"), [this] { OpenFolder(); });
    app(file, iima::loc::Text(L"OpenURL"), iima::ApplicationCommand::OpenUrl);
    add(file, iima::loc::Text(L"AppendFile"), [this] { OpenFile(true); });
    recentMenu_ = sub(file, iima::loc::Text(L"RecentlyPlayed"));
    file.Items().Append(Controls::MenuFlyoutSeparator{});
    app(file, iima::loc::Text(L"SavePlaylist"), iima::ApplicationCommand::SavePlaylist);
    app(file, iima::loc::Text(L"ShowInResourceManager"), iima::ApplicationCommand::RevealCurrentFile);
    app(file, iima::loc::Text(L"MoveToWastebin"), iima::ApplicationCommand::DeleteCurrentFile);
    file.Items().Append(Controls::MenuFlyoutSeparator{});
    add(file, iima::loc::Text(L"CloseTheCurrentWindow"), [this] { BeginClose(); }, {}, {"quit"});

    buildingMediaMenu = true;
    auto playback = top(iima::loc::Text(L"Play"));
    add(playback, iima::loc::Text(L"Play"), [this] { session_->TogglePause(); }, loaded, {"cycle","pause"}, false, {},
        [this] { return hstring{session_->Snapshot().paused ? iima::loc::Text(L"Play") : iima::loc::Text(L"Pause")}; });
    add(playback, iima::loc::Text(L"Stop"), [this] { session_->Stop(); }, [this] {
        auto const& s = session_->Snapshot();
        return s.session == iima::SessionState::Ready &&
            (s.media == iima::MediaState::Loading || s.media == iima::MediaState::Loaded || s.mediaOriginPending);
    }, {"stop"});
    add(playback, iima::loc::Text(L"Previous"), [this] { session_->PlaylistPrevious(); }, loaded, {"playlist-prev"});
    add(playback, iima::loc::Text(L"Next"), [this] { session_->PlaylistNext(); }, loaded, {"playlist-next"});
    add(playback, iima::loc::Text(L"FiveSecondsBack"), [this] { session_->SeekRelative(-5, true); }, seekable, {"seek","-5"});
    add(playback, iima::loc::Text(L"FiveSecondsForward"), [this] { session_->SeekRelative(5, true); }, seekable, {"seek","5"});
    add(playback, iima::loc::Text(L"PreviousFrame"), [this] { session_->StepFrame(true); }, video, {"frame-back-step"});
    add(playback, iima::loc::Text(L"NextFrame"), [this] { session_->StepFrame(); }, video, {"frame-step"});
    chapterMenu_ = sub(playback, iima::loc::Text(L"Chapter"));
    auto speed = sub(playback, iima::loc::Text(L"Speed"));
    for (double value : {0.5, 1.0, 1.25, 1.5, 2.0})
        add(speed, to_hstring(value) + iima::loc::Text(L"Multiply"), [this, value] { session_->SetSpeed(value); }, loaded,
            {"set","speed",std::to_string(value)}, false, [this, value] { return std::abs(session_->Snapshot().speed-value) < 1e-9; });
    add(playback, iima::loc::Text(L"ABCycle"), [this] { session_->CycleABLoop(); }, seekable, {"ab-loop"}, false,
        [this] { return session_->Snapshot().abLoopA.has_value(); });
    auto repeat = sub(playback, iima::loc::Text(L"CycleMode"));
    for (auto [mode, label] : {std::pair{iima::RepeatMode::Off,iima::loc::Text(L"NoLoop")}, {iima::RepeatMode::File,iima::loc::Text(L"LoopFile")}, {iima::RepeatMode::Playlist,iima::loc::Text(L"LoopPlaylist")}})
        add(repeat, label, [this, mode] { session_->SetRepeatMode(mode); }, loaded, {}, false,
            [this, mode] { return session_->Snapshot().repeatMode == mode; });
    app(playback, iima::loc::Text(L"PlaylistSidebar"), iima::ApplicationCommand::PlaylistPanel);

    auto picture = top(iima::loc::Text(L"Video"));
    videoTrackMenu_ = sub(picture, iima::loc::Text(L"VideoTrack"));
    add(picture, iima::loc::Text(L"LoadExternalVideo"), [this] {LoadExternalVideo();}, [this] {return CanLoadExternalAV();});
    auto sizes = sub(picture, iima::loc::Text(L"WindowDimensions"));
    for (double value : {0.5, 1.0, 1.5, 2.0})
        add(sizes, to_hstring(value) + iima::loc::Text(L"Multiply"), [this, value] { ResizeToVideo(value); }, normalVideo);
    add(sizes, iima::loc::Text(L"FitToWorkspace"), [this] { ResizeToVideo({}); }, normalVideo);
    auto rotation = sub(picture, iima::loc::Text(L"Rotate"));
    for (int value : {0, 90, 180, 270})
        add(rotation, to_hstring(value) + L"°", [this, value] { session_->SetVideoRotation(value); }, video, {}, false,
            [this, value] { return session_->Snapshot().videoRotation == value; });
    app(picture, iima::loc::Text(L"FlipUpAndDown"), iima::ApplicationCommand::ToggleFlip);
    app(picture, iima::loc::Text(L"LeftAndRightMirrors"), iima::ApplicationCommand::ToggleMirror);
    add(picture, iima::loc::Text(L"Inverted"), [this] { session_->SetDeinterlace(!session_->Snapshot().deinterlace.value_or(false)); },
        [this, video] { return video() && session_->Snapshot().deinterlace.has_value(); }, {}, false,
        [this] { return session_->Snapshot().deinterlace.value_or(false); });
    add(picture, iima::loc::Text(L"Screenshots"), [this] { SaveScreenshot(); }, video, {"screenshot"});
    add(picture, iima::loc::Text(L"SaveScreenshotAs"), [this] { SaveScreenshot(false,true); }, video);
    savedVideoFilterMenu_ = sub(picture, iima::loc::Text(L"SavedVideoFilters"));
    app(picture, iima::loc::Text(L"VideoSettings"), iima::ApplicationCommand::VideoPanel);

    auto audio = top(iima::loc::Text(L"Audio"));
    audioTrackMenu_ = sub(audio, iima::loc::Text(L"Track"));
    audioDeviceMenu_ = sub(audio, iima::loc::Text(L"OutputDevice"));
    savedAudioFilterMenu_ = sub(audio, iima::loc::Text(L"SavedAudioFilters"));
    add(audio, iima::loc::Text(L"LoadExternalTracks"), [this] { LoadExternalAudio(); }, [this] {return CanLoadExternalAV();});
    add(audio, iima::loc::Text(L"Mute"), [this] { session_->ToggleMute(); }, loaded, {"cycle","mute"}, false,
        [this] { return session_->Snapshot().muted; });
    add(audio, iima::loc::Text(L"IncreaseVolume"), [this] { session_->SetVolume(session_->Snapshot().volume + 5); }, loaded, {"add","volume","5"});
    add(audio, iima::loc::Text(L"VolumeReduction"), [this] { session_->SetVolume(session_->Snapshot().volume - 5); }, loaded, {"add","volume","-5"});
    app(audio, iima::loc::Text(L"AudioSettings"), iima::ApplicationCommand::AudioPanel);

    auto subtitles = top(iima::loc::Text(L"Subtitles"));
    subtitleTrackMenu_ = sub(subtitles, iima::loc::Text(L"PrimarySubtitles"));
    secondaryTrackMenu_ = sub(subtitles, iima::loc::Text(L"Subtitle2"));
    add(subtitles, iima::loc::Text(L"ShowSubtitles"), [this] { session_->SetSubtitlesVisible(!session_->Snapshot().subtitlesVisible); }, loaded,
        {"cycle","sub-visibility"}, false, [this] { return session_->Snapshot().subtitlesVisible; });
    add(subtitles, iima::loc::Text(L"LoadSubtitles"), [this] { OpenSubtitle(); }, [this] {return CanStartSubtitleLoad();});
    add(subtitles, iima::loc::Text(L"ReloadSubtitles"), [this] { StartSubtitleReload(); }, [this] {return CanStartSubtitleReload();});
    app(subtitles, iima::loc::Text(L"FindOnlineSubtitles"), iima::ApplicationCommand::FindOnlineSubtitles);
    app(subtitles, iima::loc::Text(L"SaveDownloadedSubtitles"), iima::ApplicationCommand::SaveDownloadedSubtitle);
    app(subtitles, iima::loc::Text(L"SubtitleSettings"), iima::ApplicationCommand::SubtitlePanel);

    buildingMediaMenu = false;
    auto window = top(iima::loc::Text(L"Window"));
    add(window, iima::loc::Text(L"Fullscreen"), [this] { ToggleFullscreen(); }, {}, {"cycle","fullscreen"}, false, {},
        [this] { return hstring{fullscreen_ ? iima::loc::Text(L"ExitFullScreen") : iima::loc::Text(L"Fullscreen")}; });
    app(window, iima::loc::Text(L"ToggleDrawings"), iima::ApplicationCommand::TogglePictureInPicture);
    app(window, iima::loc::Text(L"ToggleMusicMode"), iima::ApplicationCommand::ToggleMusicMode);
    add(window, iima::loc::Text(L"KeepTops"), [this] { ApplyAdvancedSetting("alwaysFloatOnTop", !preferences_.Boolean("alwaysFloatOnTop")); }, {}, {}, false,
        [this] { return preferences_.Boolean("alwaysFloatOnTop"); });
    window.Items().Append(Controls::MenuFlyoutSeparator{});
    add(window, iima::loc::Text(L"NewPlayerWindow"), [this] { if (newWindow_) newWindow_(); }, [this] { return newWindow_ && NewWindowButton().IsEnabled(); });
    add(window, iima::loc::Text(L"NextPlayerWindow"), [this] { if (nextWindow_) nextWindow_(); }, [this] { return nextWindow_ && NextWindowButton().IsEnabled(); });
    ApplicationMenuBar().PointerEntered([weak](auto const&, auto const&) { if (auto self = weak.get()) self->RefreshPlayerMenus(); });
    ApplicationMenuBar().GotFocus([weak](auto const&, auto const&) { if (auto self = weak.get()) self->RefreshPlayerMenus(); });
    playerMenusReady_ = true;
    RefreshPlayerMenus(true);
}
void MainWindow::RefreshPlayerMenus(bool shortcuts) {
    if (!playerMenusReady_) return;
    ApplicationMenuBar().IsEnabled(userDataLoaded_ && !closing_ && !interactiveCrop_.Active() && !cropOriginal_);
    playerMenuShortcutsDirty_ = playerMenuShortcutsDirty_ || shortcuts;
    shortcuts = playerMenuShortcutsDirty_ && playerMenuVisibleItems_.empty();
    if (shortcuts) playerMenuShortcutsDirty_ = false;
    for (auto& row : playerMenuActions_) {
        row.item.Text(row.title());
        row.item.IsEnabled(!closing_ && userDataLoaded_ && (!row.enabled || row.enabled()));
        row.item.Icon(row.checked && row.checked() ? Controls::SymbolIcon{Controls::Symbol::Accept}.as<Controls::IconElement>() : nullptr);
        if (shortcuts) {
            row.binding.reset();
            if (keyBindings_ && !row.action.empty()) {
                auto match = iima::FindMenuKeyBinding(*keyBindings_, row.action, row.application, {}, MenuKeyAvailable);
                if (match) row.binding = std::move(match->binding);
            }
            row.item.KeyboardAcceleratorTextOverride(row.binding ? to_hstring(iima::DisplayMpvKey(row.binding->NormalizedMpvKey())) : L"");
        }
    }
    auto const& s = session_->Snapshot();
    std::ostringstream key;
    key << s.mediaGeneration << ':' << s.mediaOpenIntentRevision << ':' << int(s.session) << ':' << int(s.media)
        << ':' << s.tracksRevision << ':' << s.chaptersRevision << ':' << s.chapterIndex.value_or(-1)
        << ':' << s.videoId.value_or(-9) << ':' << s.audioId.value_or(-9) << ':' << s.subtitleId.value_or(-9)
        << ':' << s.secondarySubtitleId.value_or(-9) << ':' << s.seekable << ':' << userDataLoaded_ << ':' << pickerOpen_
        << ':' << session_->PlaylistEditBusy() << ':' << s.audioDevicesRevision << ':' << s.audioDevice
        << ':' << s.audioDeviceObserved << ':' << s.audioDevicesObserved << ':' << s.audioFilters.revision << ':' << s.videoFilters.revision
        << ':' << s.audioFilters.complete << ':' << s.videoFilters.complete
        << ':' << savedFilterLibrary_.Revision() << ':' << interactiveCrop_.Active() << ':' << cropOriginal_.has_value();
    // Never replace an open menu's targets under the pointer. Its captured
    // identities remain valid for rejection; rebuild only after all popups close.
    if (playerMenuVisibleItems_.empty() && (shortcuts || key.str() != playerMenuDynamicKey_)) {
        playerMenuDynamicKey_ = key.str();
        FillTrackMenu(videoTrackMenu_, "video"); FillTrackMenu(audioTrackMenu_, "audio");
        FillTrackMenu(subtitleTrackMenu_, "sub"); FillTrackMenu(secondaryTrackMenu_, "sub", true);
        FillChapterMenu(chapterMenu_);
        FillAudioDeviceMenu(audioDeviceMenu_);
        FillSavedFilterMenu(savedVideoFilterMenu_, false);
        FillSavedFilterMenu(savedAudioFilterMenu_, true);
    }
    const auto recentKey = std::to_string(userDataLoaded_) + std::to_string(closing_) +
        std::to_string(pickerOpen_) + std::to_string(historyDeletionBusy_) + std::to_string(playlistDeletionBusy_);
    if (recentKey != playerRecentMenuStateKey_) playerRecentMenuDirty_ = true;
    if (playerMenuVisibleItems_.empty() && (shortcuts || playerRecentMenuDirty_)) {
        FillRecentMenu(recentMenu_); playerRecentMenuDirty_ = false; playerRecentMenuStateKey_ = recentKey;
    }
}
}

// IINA MiniPlayerWindowController and PlayerCore music-mode transitions.
// Copyright (c) 2016-2025 lhc and IINA contributors. GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Media/SystemMediaInfo.h"
#include <winrt/Windows.Graphics.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireMusicMode()
{
    auto weak = get_weak();
    auto toggle = [weak] {
        if (auto self = weak.get(); self && !self->closing_) {
            // Update generation before explicit input, but do not apply the
            // automatic proposal in the middle of a manual request.
            self->UpdateMusicContext();
            if (auto transition = self->musicMode_.Manual(!self->musicMode_.IsMusic()))
                self->ApplyMusicTransition(*transition);
        }
    };
    commandRouter_.Register(iima::ApplicationCommand::ToggleMusicMode, toggle,
        [weak] { auto self = weak.get(); return self && !self->closing_; });
    MusicModeButton().Click([toggle](auto const&, auto const&) { toggle(); });
    MusicReturnButton().Click([toggle](auto const&, auto const&) { toggle(); });
    MusicPlaylistButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_)
            self->ApplyAdvancedSetting("musicModeShowPlaylist", !self->preferences_.Boolean("musicModeShowPlaylist"));
    });
}
std::optional<iima::MusicModePolicy::Transition> MainWindow::UpdateMusicContext()
{
    auto const& state = session_->Snapshot();
    if (state.media == iima::MediaState::Loading && lastMusicMediaState_ != iima::MediaState::Loading)
        ++musicGeneration_;
    lastMusicMediaState_ = state.media;
    auto compact = AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay;
    return musicMode_.Update({musicGeneration_,
        state.session == iima::SessionState::Ready && state.media == iima::MediaState::Loaded && !state.path.empty(),
        !state.tracks.empty(), state.path.find("://") != std::string::npos,
        fullscreen_ || compact, preferences_.Boolean("autoSwitchToMusicMode"), state.tracks});
}
void MainWindow::UpdateMusicMode()
{
    if (closing_ || !userDataLoaded_) return;
    if (auto transition = UpdateMusicContext()) ApplyMusicTransition(*transition);
    if (musicMode_.IsMusic()) {
        auto info = iima::MakeSystemMediaInfo(session_->Snapshot());
        MusicTitleText().Text(to_hstring(info.title.empty() ? "IIMA" : info.title));
        MusicArtistText().Text(to_hstring(info.artist));
        MusicAlbumText().Text(to_hstring(info.album));
    }
}
void MainWindow::ApplyMusicTransition(iima::MusicModePolicy::Transition const& transition)
{
    if (closing_) return;
    CloseMusicVolume();
    CancelAutomaticVideoSize();
    if (!ExitFullscreen() || !ExitCompactOverlay()) { musicMode_.CancelPending(); return; }
    musicTransition_ = true;
    try {
        auto window = AppWindow();
        if (transition.music) {
            beforeMusicBounds_.reset();
            beforeMusicMaximized_ = false;
            auto presenter = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>();
            if (presenter) {
                beforeMusicMaximized_ = presenter.State() == Microsoft::UI::Windowing::OverlappedPresenterState::Maximized;
                if (presenter.State() != Microsoft::UI::Windowing::OverlappedPresenterState::Restored) presenter.Restore();
                auto p = window.Position(); auto s = window.Size();
                beforeMusicBounds_ = Windows::Graphics::RectInt32{p.X, p.Y, s.Width, s.Height};
            }
            window.SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
            fullscreen_ = false;
            FullscreenButton().Content(box_value(L"全屏"));
            window.Resize({preferences_.Boolean("musicModeShowPlaylist") ? 940 : 640,
                preferences_.Boolean("musicModeShowAlbumArt") ? 500 : 290});
        } else if (beforeMusicBounds_) {
            window.SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
            window.MoveAndResize(*beforeMusicBounds_);
            if (beforeMusicMaximized_) window.Presenter().as<Microsoft::UI::Windowing::OverlappedPresenter>().Maximize();
        }
        musicTransition_ = false;
        if (!musicMode_.Commit(transition)) return;
        UpdateMediaIntegration();
        UpdatePresentationLayout();
    } catch (hresult_error const& error) {
        musicTransition_ = false;
        musicMode_.CancelPending();
        ShowError(error.message());
    }
}
void MainWindow::ApplyMusicLayout(bool compact)
{
    auto music = musicMode_.IsMusic() && !compact;
    auto sidebar = !compact && (!music || preferences_.Boolean("musicModeShowPlaylist"));
    PlayerSidebar().Visibility(sidebar ? Visibility::Visible : Visibility::Collapsed);
    Root().ColumnDefinitions().GetAt(1).Width(GridLength{sidebar ? 300.0 : 0.0, GridUnitType::Pixel});
    MusicHeader().Visibility(music ? Visibility::Visible : Visibility::Collapsed);
    MusicModeButton().Content(box_value(music ? L"退出音乐模式" : L"音乐模式"));
    MusicPlaylistButton().Content(box_value(preferences_.Boolean("musicModeShowPlaylist") ? L"隐藏列表" : L"显示列表"));
    // Keep the panel and its native composition lifetime intact. Opacity hides
    // artwork only, without claiming to stop decoding or resize the mpv core.
    VideoPanel().Opacity(music && !preferences_.Boolean("musicModeShowAlbumArt") ? 0.0 : 1.0);
}
}

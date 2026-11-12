// IINA application commands mapped to Windows-native window/control actions.
// Copyright © 2017 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/ShellFileActions.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::WireApplicationCommands() {
    using Command = iima::ApplicationCommand;
    auto weak = get_weak();
    WireCompactOverlayControls();
    ExitCompactOverlayButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_ && !self->pickerOpen_ &&
            (self->compactRestore_ || self->AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay)) {
            self->CancelMouseClick();
            self->ExecuteApplicationCommand(Command::TogglePictureInPicture);
        }
    });
    AppWindow().Changed([weak](auto const&, auto const& args) {
        if (args.DidPresenterChange()) if (auto self = weak.get()) self->UpdatePresentationLayout();
    });
    auto available = [weak] { auto self = weak.get(); return self && !self->closing_; };
    auto orientationAvailable = [weak] {
        auto self = weak.get();
        return self && !self->closing_ && self->session_->Snapshot().session == iima::SessionState::Ready &&
            self->session_->Snapshot().media == iima::MediaState::Loaded;
    };
    commandRouter_.Register(Command::ToggleFlip, [weak] {
        if (auto self = weak.get()) self->session_->ToggleVideoOrientation(false);
    }, orientationAvailable);
    commandRouter_.Register(Command::ToggleMirror, [weak] {
        if (auto self = weak.get()) self->session_->ToggleVideoOrientation(true);
    }, orientationAvailable);
    commandRouter_.Register(Command::RevealCurrentFile, [weak] {
        if (auto self = weak.get()) iima::RevealFile(std::filesystem::path(to_hstring(self->session_->Snapshot().path).c_str()));
    }, [weak] {
        auto self = weak.get();
        return self && !self->closing_ && iima::CanPersistMediaPath(self->session_->Snapshot().path);
    });
    RevealFileButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ExecuteApplicationCommand(Command::RevealCurrentFile);
    });
    commandRouter_.Register(Command::SavePlaylist, [weak] { if (auto self = weak.get()) self->SavePlaylist(); }, available);
    commandRouter_.Register(Command::OpenFile, [weak] { if (auto self = weak.get()) self->OpenFile(); }, available);
    commandRouter_.Register(Command::OpenUrl, [weak] { if (auto self = weak.get()) self->OpenNetworkUrl(); }, available);
    commandRouter_.Register(Command::AudioPanel, [weak] { if (auto self = weak.get()) self->AudioBox().Focus(FocusState::Programmatic); }, available);
    commandRouter_.Register(Command::SubtitlePanel, [weak] { if (auto self = weak.get()) self->SubtitleBox().Focus(FocusState::Programmatic); }, available);
    commandRouter_.Register(Command::VideoPanel, [weak] { if (auto self = weak.get()) self->AspectText().Focus(FocusState::Programmatic); }, available);
    commandRouter_.Register(Command::PlaylistPanel, [weak] { if (auto self = weak.get()) self->PlaylistView().Focus(FocusState::Programmatic); }, available);
    commandRouter_.Register(Command::ChapterPanel, [weak] { if (auto self = weak.get()) self->ChapterBox().Focus(FocusState::Programmatic); }, available);
    commandRouter_.Register(Command::TogglePictureInPicture, [weak] {
        if (auto self = weak.get()) {
            if(self->compactTransition_ || self->closing_ || self->pickerOpen_ || self->interactiveCrop_.Active() || self->cropOriginal_) return;
            if(!self->ExitFullscreen()) return;
            if(self->compactRestore_ || self->AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay)
                self->ExitCompactOverlay();
            else self->EnterCompactOverlay();
        }
    }, available);
    auto resize = [weak](double multiplier) {
        if (auto self = weak.get(); self && !self->fullscreen_) {
            if(self->compactRestore_ && self->AppWindow().Presenter().Kind()==Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped && !self->ExitCompactOverlay(false)) return;
            self->CancelAutomaticVideoSize();
            auto size = self->AppWindow().Size();
            self->AppWindow().Resize({std::clamp(static_cast<int>(size.Width * multiplier), 640, 16384),
                std::clamp(static_cast<int>(size.Height * multiplier), 360, 16384)});
        }
    };
    commandRouter_.Register(Command::BiggerWindow, [resize] { resize(1.1); }, available);
    commandRouter_.Register(Command::SmallerWindow, [resize] { resize(1.0 / 1.1); }, available);
    commandRouter_.Register(Command::FitToScreen, [weak] {
        if (auto self = weak.get()) {
            self->CancelAutomaticVideoSize();
            auto window = self->AppWindow();
            auto display = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(window.Id(),
                Microsoft::UI::Windowing::DisplayAreaFallback::Nearest);
            if (display) {
                if (!self->ExitFullscreen(false) || !self->ExitCompactOverlay(false)) return;
                window.SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::Default);
                if (auto normal = window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) normal.Restore();
                self->fullscreen_ = false;
                self->FullscreenButton().Content(box_value(L"全屏"));
                window.MoveAndResize(display.WorkArea());
            }
        }
    }, available);
}
void MainWindow::UpdatePresentationLayout() {
    CancelMouseClick();
    CancelThumbnailPreview();
    ApplyWindowPreferences();
    auto compact = AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay;
    ApplicationMenuBar().Visibility(compact ? Visibility::Collapsed : Visibility::Visible);
    PlayerSidebar().Visibility(compact ? Visibility::Collapsed : Visibility::Visible);
    TopStatusBorder().Visibility(compact ? Visibility::Collapsed : Visibility::Visible);
    BottomControlsBorder().Visibility(compact ? Visibility::Collapsed : Visibility::Visible);
    ExitCompactOverlayButton().Visibility(compact || compactRestore_ ? Visibility::Visible : Visibility::Collapsed);
    ExitCompactOverlayButton().Content(box_value(!compact && compactRestore_ ? L"重试返回播放器" : L"返回播放器"));
    Root().ColumnDefinitions().GetAt(1).Width(GridLength{compact ? 0.0 : 300.0, GridUnitType::Pixel});
    ApplyMusicLayout(compact);
    UpdatePlaylistMetadata();
    UpdateCompactOverlayControls();
    ControlBarActivity();
}
void MainWindow::ExecuteApplicationCommand(iima::ApplicationCommand command) {
    try {
        if (commandRouter_.Execute(command) != iima::ApplicationCommandRouter::Result::Executed)
            ShowError(L"此操作当前不可用。");
    } catch (hresult_error const& error) { ShowError(error.message()); }
    catch (...) { ShowError(L"执行操作失败。"); }
}
}

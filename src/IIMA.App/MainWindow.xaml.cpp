#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include "../IIMA.Core/BuildInfo.h"
#include "../IIMA.Core/Media/VideoTime.h"
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Graphics.h>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
hstring Clock(std::optional<double> seconds)
{
    if (!seconds || !std::isfinite(*seconds) || *seconds < 0) return L"--:--";
    return to_hstring(iima::VideoTime(*seconds).StringRepresentation());
}
}
namespace winrt::IIMA::implementation {
MainWindow::MainWindow()
{
    InitializeComponent();
    Root().IsEnabled(false);
    Title(L"IIMA " + to_hstring(iima::version()));
    auto weak = get_weak();
    auto dispatcher = DispatcherQueue();
    InitializeDiagnostics();
    session_ = iima::PlayerSession::Create(
        [dispatcher](std::function<void()> work) {
            dispatcher.TryEnqueue([work = std::move(work)] { work(); });
        },
        [weak](iima::PlaybackSnapshot const& state) {
            if (auto self = weak.get(); self && !self->closing_) self->Apply(state);
        });
    std::weak_ptr<iima::PlayerSession> session = session_;
    presenter_ = std::make_unique<iima::VideoPresenter>(VideoPanel(), [session](unsigned w, unsigned h) {
        if (auto current = session.lock()) current->SetCompositionSize(w, h);
    }, [weak](hstring const& error) { if (auto self = weak.get(); self && !self->closing_) self->ShowError(error); });
    WireApplicationCommands();
    WireMusicMode();
    WirePluginInspection();
    WirePluginPointer();
    WireMouseWheel();
    WireControlBarVisibility();
    WireOSD();
    WireAdvancedPlayback();
    OpenButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->ExecuteApplicationCommand(iima::ApplicationCommand::OpenFile); });
    OpenUrlButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->ExecuteApplicationCommand(iima::ApplicationCommand::OpenUrl); });
    CompactOverlayButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->ExecuteApplicationCommand(iima::ApplicationCommand::TogglePictureInPicture); });
    PauseButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->session_->TogglePause(); });
    FullscreenButton().Click([weak](auto const&, auto const&) { if (auto self = weak.get()) self->ToggleFullscreen(); });
    PositionSlider().ValueChanged([weak](auto const&, auto const& args) {
        if (auto self = weak.get(); self && !self->updating_ && !self->closing_) self->session_->Seek(args.NewValue());
    });
    WireKeyboardControls();
    WirePlaybackControls();
    WireMediaInformation();
    WireInspectorWatch();
    WireDiagnosticViewer();
    WireVideoControls();
    WireVideoAdjustments();
    WireCropControls();
    WireFilterEditor();
    WireSavedFilters();
    WireThumbnailPreview();
    WireSubtitleControls();
    WireSecondarySubtitles();
    WireNavigationControls();
    WireABLoopEditor();
    WireOnlineSubtitles();
    WireAudioControls();
    WireEqualizerEditor();
    WireUserDataControls();
    WireSettingsControls();
    WireAssociationControls();
    WireFileActions();
    WirePlaylistDrag();
    WirePlaylistClipboard();
    try {
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        mediaIntegration_ = std::make_unique<iima::MediaIntegration>(hwnd, dispatcher, [weak](iima::SystemMediaCommand command) {
            if (auto self = weak.get(); self && !self->closing_) {
                switch (command) {
                case iima::SystemMediaCommand::Play: self->session_->Pause(false); break;
                case iima::SystemMediaCommand::Pause: self->session_->Pause(true); break;
                case iima::SystemMediaCommand::Previous: self->session_->PlaylistPrevious(); break;
                case iima::SystemMediaCommand::Next: self->session_->PlaylistNext(); break;
                }
            }
        }, [weak](double seconds) {
            if (auto self = weak.get(); self && !self->closing_ && self->session_->Snapshot().seekable)
                self->session_->Seek(seconds);
        });
    } catch (hresult_error const& error) { ShowError(error.message()); }
    try { WireWindowPlaybackPolicy(); }
    catch(hresult_error const& error) { ShowError(error.message()); }
    LoadUserData();
    AppWindow().Resize({1100, 720});
    WireWindowPlacement();
    AppWindow().Closing([weak](auto const&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
        if (auto self = weak.get(); self && !self->closeComplete_) {
            args.Cancel(true);
            self->BeginClose();
        }
    });
    wchar_t executable[32768]{};
    auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable)) {
        ShowError(L"无法定位播放器运行目录。");
        return;
    }
    session_->Start((std::filesystem::path{executable}.parent_path() / L"libmpv-2.dll").wstring());
}
void MainWindow::ShowError(hstring const& message) { StatusText().Text(message); }
winrt::fire_and_forget MainWindow::OpenFile(bool append, bool invertWindowPolicy)
{
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    pickerOpen_ = true;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.FileTypeFilter().Append(L"*");
        auto files = co_await picker.PickMultipleFilesAsync();
        if (files.Size() && !closing_) {
            std::vector<std::string> paths;
            for (auto const& file : files) paths.push_back(to_string(file.Path()));
            QueueFiles(std::move(paths), append, !append, invertWindowPolicy);
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    pickerOpen_ = false;
}
winrt::fire_and_forget MainWindow::OpenSubtitle()
{
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_) co_return;
    pickerOpen_ = true;
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{};
        check_hresult(this->as<::IWindowNative>()->get_WindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        picker.FileTypeFilter().Append(L"*");
        auto file = co_await picker.PickSingleFileAsync();
        if (file && !closing_) session_->AddSubtitle(to_string(file.Path()));
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
    pickerOpen_ = false;
}
void MainWindow::Apply(iima::PlaybackSnapshot const& state)
{
    if (state.media != iima::MediaState::Loaded) {
        cropDraftEntry_.reset();
        subtitleMatchStop_.request_stop();
        ++subtitleMatchGeneration_;
    }
    updating_ = true;
    hstring presentationError;
    if (state.swapChain != swapChain_) {
        try {
            if (state.swapChain) {
                winrt::com_ptr<IDXGISwapChain> chain;
                check_hresult(static_cast<IDXGISwapChain*>(state.swapChain.get())->QueryInterface(__uuidof(IDXGISwapChain), chain.put_void()));
                presenter_->Bind(std::move(chain));
                swapChain_ = state.swapChain;
            } else {
                presenter_->Detach();
                swapChain_.reset();
            }
        } catch (hresult_error const& error) {
            presentationError = error.message();
            presenter_->Detach();
            swapChain_.reset();
        }
    }
    const bool audioOnly = state.media == iima::MediaState::Loaded &&
        iima::AudioOnlyPlaybackEligible(state.tracks, !state.tracks.empty());
    const bool outputReady = audioOnly || (state.swapChain && presenter_->IsBound() && presentationError.empty());
    session_->SetPresentationReady(outputReady);
    UpdateMusicMode();
    if (state.media == iima::MediaState::Loaded && fullscreenPreferenceEntry_ != state.entryId) {
        fullscreenPreferenceEntry_ = state.entryId;
        if (userDataLoaded_ && !musicMode_.IsMusic() && preferences_.Boolean("fullScreenWhenOpen") && !fullscreen_) ToggleFullscreen();
    }
    ApplyCollections(state);
    UpdateSecondarySubtitles(state);
    UpdateNavigationControls(state);
    UpdateABLoopEditor(state);
    UpdateMediaInformation(state);
    UpdateVideoAdjustments(state);
    UpdateFilterEditor(state);
    UpdateThumbnailMedia(state);
    UpdateOnlineSubtitleMedia(state);
    ApplySettingsSnapshot(state);
    UpdateDiagnostics(state);
    UpdateOSD(state);
    UpdateControlBarPolicy();
    UpdatePlugin(state);
    ApplyUserData(state);
    OpenPendingFiles();
    FlushActivation();
    if (mediaIntegration_) mediaIntegration_->Update(state);
    bool loaded = state.media == iima::MediaState::Loaded || state.media == iima::MediaState::Ended;
    PauseButton().IsEnabled(loaded && outputReady);
    PauseButton().Content(box_value(state.paused ? L"播放" : L"暂停"));
    auto seekable = loaded && state.seekable && state.duration && *state.duration > 0;
    PositionSlider().IsEnabled(seekable && outputReady);
    PositionSlider().Maximum(seekable ? *state.duration : 1.0);
    PositionSlider().Value(state.position.value_or(0));
    TimeLabel().Text(Clock(state.position) + L" / " + Clock(state.duration));
    ABLoopLabel().Text(L"A: " + Clock(state.abLoopA) + L"  B: " + Clock(state.abLoopB));
    EmptyLabel().Visibility(loaded ? Visibility::Collapsed : Visibility::Visible);
    if (!presentationError.empty()) ShowError(presentationError);
    else if (!state.error.empty() && !(audioOnly && state.error.starts_with("Composition output"))) ShowError(to_hstring(state.error));
    else if (state.session == iima::SessionState::Ready && !state.compositionAvailable)
        ShowError(L"当前 libmpv 缺少安全的视频合成接口，请安装配套构建。");
    else if (state.media == iima::MediaState::Loading) StatusText().Text(L"正在打开…");
    else if (loaded) StatusText().Text(state.buffering ? L"正在缓冲…" : L"");
    updating_ = false;
}
void MainWindow::ToggleFullscreen()
{
    if (closing_) return;
    try {
        fullscreen_ = !fullscreen_;
        AppWindow().SetPresenter(fullscreen_ ? Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen
                                           : Microsoft::UI::Windowing::AppWindowPresenterKind::Overlapped);
        FullscreenButton().Content(box_value(fullscreen_ ? L"退出全屏" : L"全屏"));
        if (fullscreen_ && preferences_.Boolean("playWhenEnteringFullScreen")) session_->Pause(false);
        if (!fullscreen_ && preferences_.Boolean("pauseWhenLeavingFullScreen") &&
            session_->Snapshot().media == iima::MediaState::Loaded && !session_->Snapshot().paused) session_->Pause(true);
        ApplyWindowPreferences();
    } catch (hresult_error const& error) { fullscreen_ = !fullscreen_; ShowError(error.message()); }
}
void MainWindow::BeginClose()
{
    if (userDataLoaded_ && !discardUserData_ && (!userData_.writable || saveFailed_)) {
        ConfirmUnsavedClose(); return;
    }
    if (closing_) { FinishClose(); return; }
    CapturePlacement();
    SaveWindowPlacement();
    closing_ = true;
    recordingBindingKey_ = false;
    if (jumpDialog_) jumpDialog_.Hide();
    fontCatalogStop_.request_stop();
    revealSelectionStop_.request_stop();
    playlistPasteStop_.request_stop(); ++playlistPasteGeneration_;
    if (subtitleFontDialog_) subtitleFontDialog_.Hide();
    if (subtitleColorDialog_) subtitleColorDialog_.Hide();
    if (openUrlDialog_) openUrlDialog_.Hide();
    pluginInspectionStop_.request_stop();
    StopLocalPlugin();
    CancelThumbnailPreview();
    CancelOnlineSubtitles();
    musicMode_.Close();
    if(windowLifecycle_) windowLifecycle_->Close();
    if(powerLifecycle_) powerLifecycle_->Close();
    inputStop_.request_stop(); ++inputGeneration_;
    subtitleMatchStop_.request_stop(); ++subtitleMatchGeneration_;
    HideScreenshotPreview();
    if (osdTimer_) osdTimer_.Stop();
    if (controlBarTimer_) controlBarTimer_.Stop();
    InvalidateInspectorWatch();
    if (diagnosticTimer_) diagnosticTimer_.Stop();
    if (activationTimer_) activationTimer_.Stop();
    if (activationClosing_) activationClosing_();
    if (mediaIntegration_) mediaIntegration_->Close();
    RecordLastMedia();
    Root().IsEnabled(false);
    if (userDataLoaded_) { saveDirty_ = true; saveFailed_ = false; PumpSave(); }
    presenter_.reset(); // UI detach before event consumer stop / mpv destruction.
    swapChain_.reset();
    auto weak = get_weak();
    session_->CloseAsync([weak] {
        if (auto self = weak.get()) { self->coreClosed_ = true; self->onlineSubtitleFiles_.clear(); self->downloadedSubtitles_.clear(); self->CloseDiagnostics(); self->FinishClose(); }
    });
}
}

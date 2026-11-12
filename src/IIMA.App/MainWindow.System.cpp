// Windows activation adapts IINA AppDelegate.application(openFile:)/handleOpenFile:
// batch repeated OS opens for 0.2 seconds, preserving request order before resolving.
// Copyright © 2016 lhc. Adapted behavior: GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/FileAssociations.h"
#include <ShlObj.h>
#include <microsoft.ui.xaml.window.h>
#include <sstream>
#include <chrono>
#include <iterator>
#include <atomic>

using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::UpdateMediaIntegration() {
    if (closing_ || !mediaIntegration_ || !session_) return;
    mediaIntegration_->Update(session_->Snapshot(), preferences_.Boolean("preventScreenSaver"),
        preferences_.Boolean("allowScreenSaverForAudio"), musicMode_.IsMusic());
}

void MainWindow::ReceiveActivation(std::vector<std::wstring> paths)
{
    if (closing_) return;
    Activate();
    HWND hwnd{};
    if (SUCCEEDED(this->as<::IWindowNative>()->get_WindowHandle(&hwnd))) {
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd); // Best effort: Windows foreground policy still applies.
    }
    if (paths.empty()) return;
    if (activationInputs_.size() + paths.size() > 256) { ShowError(L"一次打开的文件过多，请分批打开。"); return; }
    activationInputs_.insert(activationInputs_.end(), std::make_move_iterator(paths.begin()), std::make_move_iterator(paths.end()));
    InvalidateSubtitleLoad();
    InvalidateExternalAVLoad();
    InvalidateExternalTrackReload();
    InvalidateExternalTrackRemoval();
    InvalidateSubtitleReload();
    CancelScrubbing();
    ClearTrackInspector();
    UpdateInitialWindow(true);
    if (!activationTimer_) {
        activationTimer_ = DispatcherQueue().CreateTimer();
        activationTimer_.IsRepeating(false);
        activationTimer_.Interval(std::chrono::milliseconds(200));
        auto weak = get_weak();
        activationTimer_.Tick([weak](auto const&, auto const&) {
            if (auto self = weak.get(); self && !self->closing_) {
                self->activationDelayElapsed_ = true;
                self->FlushActivation();
            }
        });
    }
    activationDelayElapsed_ = false;
    activationTimer_.Stop(); activationTimer_.Start();
}
void MainWindow::FlushActivation()
{
    if (closing_ || !activationDelayElapsed_ || activationInputs_.empty() || !userDataLoaded_ ||
        session_->Snapshot().session == iima::SessionState::Initializing) return;
    try {
        std::vector<std::string> paths;
        for (auto const& path : activationInputs_) paths.push_back(to_string(path));
        activationInputs_.clear(); activationDelayElapsed_ = false;
        QueueFiles(std::move(paths), false, true);
    } catch (hresult_error const& error) {
        activationInputs_.clear(); activationDelayElapsed_ = false; ShowError(error.message());
    } catch (std::exception const& error) {
        activationInputs_.clear(); activationDelayElapsed_ = false; ShowError(to_hstring(error.what()));
    }
}
void MainWindow::WireAssociationControls()
{
    auto weak = get_weak();
    RegisterAssociationsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) try {
            iima::FileAssociations::Register();
            self->ShowError(L"已注册为可选播放器，默认应用未改变。请在 Windows 设置中选择 IIMA。");
        } catch (hresult_error const& error) { self->ShowError(L"注册未完成，可重试：" + error.message()); }
    });
    UnregisterAssociationsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) try {
            iima::FileAssociations::Unregister();
            self->ShowError(L"已移除此副本的文件关联注册。如原先设为默认，请在 Windows 设置中选择其他应用。");
        } catch (hresult_error const& error) { self->ShowError(error.message()); }
    });
    DefaultAppsButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) try {
            HWND hwnd{}; check_hresult(self->as<::IWindowNative>()->get_WindowHandle(&hwnd));
            iima::FileAssociations::OpenDefaultApps(hwnd);
        } catch (hresult_error const& error) { self->ShowError(error.message()); }
    });
}
void MainWindow::InitializeDiagnostics()
{
    try {
        PWSTR raw{};
        check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw));
        std::filesystem::path path;
        try { path = raw; } catch (...) { CoTaskMemFree(raw); throw; }
        CoTaskMemFree(raw);
        auto weak = get_weak(); auto dispatcher = DispatcherQueue();
        // Every window has its own writer/rotation pair. No two independent workers
        // may rotate or truncate a common player.log.
        static std::atomic<std::uint64_t> sequence{0};
        auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        auto directory = std::to_wstring(stamp) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
        diagnostics_ = std::make_unique<iima::DiagnosticLog>(path / L"IIMA" / L"logs" / directory, [weak, dispatcher](std::string error) {
            dispatcher.TryEnqueue([weak, error = std::move(error)] {
                if (auto self = weak.get()) self->ShowError(L"诊断日志不可用：" + to_hstring(error));
            });
        });
        diagnostics_->Write("app", "starting");
    } catch (std::exception const& e) { diagnosticsClosed_ = true; ShowError(to_hstring(e.what())); }
      catch (hresult_error const& e) { diagnosticsClosed_ = true; ShowError(e.message()); }
}
void MainWindow::UpdateDiagnostics(iima::PlaybackSnapshot const& state)
{
    if (!diagnostics_) return;
    std::ostringstream fields;
    fields << "session=" << static_cast<int>(state.session) << " media=" << static_cast<int>(state.media)
           << " entry=" << state.entryId << " api=" << state.clientApiVersion
           << " mpv=" << state.mpvVersion << " codec=" << state.videoCodec
           << " hwdec=" << state.hwdecCurrent << " vo=" << state.currentVo;
    auto value = fields.str();
    if (value != lastDiagnostics_) { lastDiagnostics_ = value; diagnostics_->Write("playback", std::move(value)); }
}
void MainWindow::CloseDiagnostics()
{
    if (!diagnostics_) { diagnosticsClosed_ = true; FinishClose(); return; }
    diagnostics_->Write("app", "closing after player shutdown");
    auto weak = get_weak(); auto dispatcher = DispatcherQueue();
    diagnostics_->CloseAsync([weak, dispatcher] {
        dispatcher.TryEnqueue([weak] { if (auto self = weak.get()) { self->diagnosticsClosed_ = true; self->FinishClose(); } });
    });
}
}

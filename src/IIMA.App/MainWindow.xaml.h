#pragma once
#include "MainWindow.g.h"
#include "../IIMA.Core/Playback/PlayerSession.h"
#include "Presentation/VideoPresenter.h"
#include "Platform/MediaIntegration.h"
#include "Platform/LocalInputResolver.h"
#include "../IIMA.Core/Diagnostics/DiagnosticLog.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include "../IIMA.Core/Persistence/UserData.h"
#include <winrt/Microsoft.UI.Windowing.h>

namespace winrt::IIMA::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void QueueFiles(std::vector<std::string> paths, bool append = false);
    void ReceiveActivation(std::vector<std::wstring> paths);
    void SetActivationClosing(std::function<void()> callback) { activationClosing_ = std::move(callback); }
private:
    winrt::fire_and_forget OpenFile(bool append = false);
    winrt::fire_and_forget OpenSubtitle();
    void WirePlaybackControls();
    void WireVideoControls();
    void ApplyCollections(iima::PlaybackSnapshot const& state);
    void PlaySelected();
    void MoveSelected(int delta);
    void Apply(iima::PlaybackSnapshot const& state);
    void ToggleFullscreen();
    void BeginClose();
    void WireFileActions();
    void WireSettingsControls();
    void ApplySettingsSnapshot(iima::PlaybackSnapshot const& state);
    void WireAssociationControls();
    void FlushActivation();
    void InitializeDiagnostics();
    void UpdateDiagnostics(iima::PlaybackSnapshot const& state);
    void CloseDiagnostics();
    void OpenPendingFiles();
    winrt::fire_and_forget DropFiles(Microsoft::UI::Xaml::DragEventArgs args);
    winrt::fire_and_forget SaveScreenshot();
    void LoadUserData();
    void WireUserDataControls();
    void ApplyUserData(iima::PlaybackSnapshot const& state);
    void RecordLastMedia();
    void RefreshHistory();
    void MarkUserDataDirty();
    void PumpSave();
    void FinishClose();
    winrt::fire_and_forget ConfirmUnsavedClose();
    void ShowError(winrt::hstring const& message);
    std::shared_ptr<iima::PlayerSession> session_;
    std::unique_ptr<iima::VideoPresenter> presenter_;
    std::optional<uint64_t> playlistRevision_, tracksRevision_, chaptersRevision_;
    std::vector<iima::PlaylistItem> playlist_;
    std::vector<iima::MediaTrack> tracks_;
    std::vector<iima::Chapter> chapters_;
    std::shared_ptr<void> swapChain_;
    std::unique_ptr<iima::MediaIntegration> mediaIntegration_;
    std::vector<std::string> pendingFiles_;
    bool pendingAppend_{};
    std::stop_source inputStop_;
    uint64_t inputGeneration_{};
    Microsoft::UI::Dispatching::DispatcherQueueTimer activationTimer_{nullptr};
    std::vector<std::wstring> activationInputs_;
    bool activationDelayElapsed_{};
    std::function<void()> activationClosing_;
    std::unique_ptr<iima::DiagnosticLog> diagnostics_;
    std::string lastDiagnostics_;
    bool diagnosticsClosed_{};
    iima::UserData userData_;
    struct HistorySample { std::int64_t entryId; std::string path; std::optional<double> position, duration; };
    std::optional<HistorySample> lastMedia_;
    std::optional<int64_t> resumedEntry_;
    bool discardUserData_{}, saveDialogOpen_{}, userDataLoaded_{}, settingsApplied_{}, saving_{}, saveDirty_{}, saveFailed_{}, coreClosed_{};
    bool updating_{}, closing_{}, closeComplete_{}, pickerOpen_{}, fullscreen_{};
};
}
namespace winrt::IIMA::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}

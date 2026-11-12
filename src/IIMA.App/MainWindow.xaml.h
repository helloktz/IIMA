#pragma once
#include "MainWindow.g.h"
#include "../IIMA.Core/Playback/PlayerSession.h"
#include "../IIMA.Core/Commands/ApplicationCommand.h"
#include "Presentation/VideoPresenter.h"
#include "../IIMA.Core/Windows/MusicModePolicy.h"
#include <winrt/Windows.Graphics.h>
#include "Platform/MediaIntegration.h"
#include "Platform/WindowLifecycle.h"
#include "Platform/PowerLifecycle.h"
#include "Platform/WindowPlacement.h"
#include "Platform/LocalInputResolver.h"
#include "Platform/ScreenshotStaging.h"
#include "Platform/ThumbnailExtractor.h"
#include "Platform/AssrtClient.h"
#include "Platform/OpenSubClient.h"
#include "Platform/SubtitleMediaHash.h"
#include "Platform/SubtitleStaging.h"
#include "../IIMA.Core/OnlineSubtitles/SearchTransaction.h"
#include "../IIMA.Core/Filters/SavedFilterLibrary.h"
#include <deque>
#include "Plugins/PluginRuntime.h"
#include "Plugins/PluginPointerState.h"
#include "Plugins/PluginPlaybackProjection.h"
#include "../IIMA.Core/History/HistoryBrowser.h"
#include "../IIMA.Core/Diagnostics/DiagnosticLog.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include "../IIMA.Core/Persistence/UserData.h"
#include "../IIMA.Core/Input/KeyMapping.h"
#include <winrt/Microsoft.UI.Windowing.h>

namespace winrt::IIMA::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    void QueueFiles(std::vector<std::string> paths, bool append = false, bool useWindowPolicy = false, bool invertWindowPolicy = false);
    void ReceiveActivation(std::vector<std::wstring> paths);
    void ConfigureWindowActions(std::function<void()> create, std::function<void()> next);
    void UpdateWindowCount(std::size_t count);
    using OpenInputRouter = std::function<void(std::vector<std::string>, bool, bool, bool)>;
    void SetOpenInputRouter(OpenInputRouter router) { openInputRouter_ = std::move(router); }
    void ReceiveResolvedInput(std::vector<std::string> paths);
    bool CanReuseForInput() const;
    bool HasOpenInput(std::string const& path) const;
    bool IsClosing() const noexcept { return closing_; }
    void SetActivationClosing(std::function<void()> callback) { activationClosing_ = std::move(callback); }
private:
    winrt::fire_and_forget OpenFile(bool append = false, bool invertWindowPolicy = false);
    winrt::fire_and_forget OpenFolder(bool append = false, bool invertWindowPolicy = false);
    winrt::fire_and_forget OpenSubtitle();
    winrt::fire_and_forget OpenNetworkUrl(bool invertWindowPolicy = false);
    Microsoft::UI::Xaml::Controls::ContentDialog openUrlDialog_{nullptr};
    void WireWindowPlacement();
    void CapturePlacement();
    void RestorePlacement(std::optional<iima::WindowPlacementSnapshot> saved);
    void SaveWindowPlacement();
    std::optional<iima::WindowPlacementSnapshot> windowPlacement_;
    bool placementTouched_{}, restoringPlacement_{}, placementSavePending_{}, placementWritable_{true}, musicTransition_{};
    void WirePluginInspection();
    winrt::fire_and_forget StartLocalPlugin();
    void StopLocalPlugin();
    winrt::fire_and_forget RetirePlugin(std::unique_ptr<iima::plugins::PluginRuntime> runtime);
    void ApplyPluginMenu(std::string const& json);
    void ClearPluginMenu();
    std::string PluginKeyBindingsJson() const;
    void UpdatePluginKeyBindings();
    void PublishPluginKey(std::string const& key, bool down, bool repeat);
    void WirePluginPointer();
    void PluginPointerEvent(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args, unsigned event);
    iima::plugins::PluginPointerState pluginPointer_;
    void UpdatePlugin(iima::PlaybackSnapshot const& state);
    void DispatchPluginCommand(iima::plugins::PluginCommand command);
    void DispatchPluginPlaylistCommand(iima::plugins::PluginCommand const& command);
    std::unique_ptr<iima::plugins::PluginRuntime> pluginRuntime_;
    iima::plugins::PlaybackProjection pluginProjection_;
    std::uint64_t pluginGeneration_{}, pluginMediaEpoch_{};
    std::int64_t pluginMediaEntry_{-1};
    iima::MediaState pluginMediaState_{iima::MediaState::Empty};
    bool pluginStarting_{}, pluginStopPending_{}, pluginOsdAllowed_{};
    std::wstring pluginLog_;

    winrt::fire_and_forget InspectPlugin();
    bool pluginInspectionBusy_{};
    std::stop_source pluginInspectionStop_;
    void WireMusicMode();
    std::optional<iima::MusicModePolicy::Transition> UpdateMusicContext();
    void UpdateMusicMode();
    void ApplyMusicTransition(iima::MusicModePolicy::Transition const& transition);
    void ApplyMusicLayout(bool compact);
    iima::MusicModePolicy musicMode_;
    std::uint64_t musicGeneration_{};
    iima::MediaState lastMusicMediaState_{iima::MediaState::Empty};
    std::optional<Windows::Graphics::RectInt32> beforeMusicBounds_;
    void WirePlaybackControls();
    void WireMediaInformation();
    void WireInspectorWatch();
    void LoadInspectorWatch();
    void ApplyInspectorWatch();
    void RefreshInspectorWatch();
    void InvalidateInspectorWatch();
    Microsoft::UI::Dispatching::DispatcherQueueTimer inspectorTimer_{nullptr};
    std::uint64_t inspectorGeneration_{};
    bool inspectorBusy_{};
    void UpdateMediaInformation(iima::PlaybackSnapshot const& state);
    std::optional<std::uint64_t> informationRevision_, audioDevicesRevision_;
    void WireKeyboardControls();
    void ApplyKeyboardConfiguration();
    bool DispatchKeyBinding(iima::KeyMapping const& binding);
    winrt::fire_and_forget ImportKeyBindings();
    winrt::fire_and_forget ExportKeyBindings();
    std::unique_ptr<iima::KeyBindingSet> keyBindings_;
    void WireThumbnailPreview();
    winrt::fire_and_forget ClearThumbnailCache();
    bool clearingThumbnailCache_{};
    void UpdateThumbnailMedia(iima::PlaybackSnapshot const& state);
    void CancelThumbnailPreview();
    void BeginThumbnailRequest();
    winrt::fire_and_forget DisplayThumbnail(iima::ThumbnailExtractionResult result);
    Microsoft::UI::Dispatching::DispatcherQueueTimer thumbnailTimer_{nullptr};
    std::stop_source thumbnailStop_;
    std::uint64_t thumbnailGeneration_{};
    bool thumbnailBusy_{}, thumbnailWanted_{};
    std::string thumbnailPath_;
    std::int64_t thumbnailEntry_{-1};
    double thumbnailSeconds_{};
    std::deque<iima::ThumbnailExtractionResult> thumbnailMemory_;
    void WireCropControls();
    void LoadCropDraft();
    void ApplyCropDraft();
    std::optional<std::int64_t> cropDraftEntry_;
    std::string cropDraftPath_;
    int cropDraftWidth_{}, cropDraftHeight_{};
    void WireVideoControls();
    void WireVideoAdjustments();
    void UpdateVideoAdjustments(iima::PlaybackSnapshot const& state);
    bool projectingVideoAdjustments_{};
    void WireFilterEditor();
    void WireSavedFilters();
    void LoadSavedFilters();
    void RefreshSavedFilters();
    void EditSavedFilter(int action);
    void ApplySavedFilter(bool audio, std::size_t index);
    iima::SavedFilterLibrary savedFilterLibrary_;
    bool refreshingSavedFilters_{};
    void UpdateFilterEditor(iima::PlaybackSnapshot const& state);
    void EditSelectedFilter(int operation);
    iima::FilterChainSnapshot displayedFilters_;
    std::optional<std::uint64_t> filterRevision_;
    void WireABLoopEditor();
    void UpdateABLoopEditor(iima::PlaybackSnapshot const& state);
    void LoadABLoopDraft();
    void ApplyABLoopDraft(bool pointB);
    std::optional<std::uint64_t> abDraftEpoch_;
    std::optional<double> abDraftA_, abDraftB_;
    void WireNavigationControls();
    void UpdateNavigationControls(iima::PlaybackSnapshot const& state);
    void ApplyCustomSpeed(double requested);
    winrt::fire_and_forget JumpToTime();
    bool projectingNavigation_{};
    std::uint64_t navigationEpoch_{};
    std::int64_t navigationEntry_{-1};
    std::string navigationPath_;
    Microsoft::UI::Xaml::Controls::ContentDialog jumpDialog_{nullptr};
    void WireSubtitleControls();
    void ApplySubtitleColorDraft(unsigned kind);
    void UpdateSubtitleStyleFeedback(iima::PlaybackSnapshot const& state);
    void WireSecondarySubtitles();
    void UpdateSecondarySubtitles(iima::PlaybackSnapshot const& state);
    void LoadSecondarySubtitleDraft();
    void ApplySecondarySubtitleDraft();
    bool projectingSubtitles_{};
    std::uint64_t secondaryTracksRevision_{~std::uint64_t{0}};
    std::optional<std::int64_t> secondaryDraftEntry_;
    std::string secondaryDraftPath_;
    void FindLocalSubtitle();
    void WireOnlineSubtitles();
    void SearchOnlineSubtitles();
    void DownloadOnlineSubtitle();
    winrt::fire_and_forget SaveOnlineSubtitle();
    void CancelOnlineSubtitles();
    void UpdateOnlineSubtitleMedia(iima::PlaybackSnapshot const& state);
    void RefreshOnlineSubtitleButtons();
    iima::SubtitleSearchTransaction onlineSubtitleTransaction_;
    std::stop_source onlineSubtitleStop_;
    bool onlineSubtitleWorkerActive_{};
    std::vector<iima::assrt::SearchResult> onlineSubtitleResults_;
    int onlineSubtitleProvider_{};
    std::string onlineSubtitleApiKey_;
    std::string onlineSubtitleToken_; // memory-only, never persisted or logged
    std::vector<std::shared_ptr<iima::SubtitleStaging>> onlineSubtitleFiles_;
    struct DownloadedSubtitle {
        std::filesystem::path path;
        std::shared_ptr<iima::SubtitleStaging> owner;
    };
    std::vector<DownloadedSubtitle> downloadedSubtitles_;
    std::size_t onlineSubtitleBytes_{};

    void WireAudioControls();
    void WireEqualizerEditor();
    std::optional<iima::EQProfile> ReadEqualizerDraft();
    void SetEqualizerDraft(iima::EQProfile const& profile);
    void RefreshUserEqualizers();
    void EditUserEqualizer(int operation);
    std::array<Microsoft::UI::Xaml::Controls::NumberBox, iima::EQProfile::BandCount> equalizerBands_;
    std::vector<std::string> equalizerNames_;
    void WireApplicationCommands();
    void WireOSD();
    void WireAdvancedPlayback();
    bool KeyboardPlaybackAllowed() const;
    void UpdateOSD(iima::PlaybackSnapshot const& state);
    void UpdatePresentationLayout();
    void ExecuteApplicationCommand(iima::ApplicationCommand command);
    void ApplyCollections(iima::PlaybackSnapshot const& state);
    void PlaySelected();
    std::vector<std::int64_t> SelectedPlaylistIds() const;
    std::vector<std::size_t> SelectedPlaylistRows();
    void UpdatePlaylistSelection();
    void WirePlaylistDrag();
    bool PlaylistDragCurrent(Microsoft::UI::Xaml::DragEventArgs const& args) const;
    std::optional<std::size_t> PlaylistDropRow(Microsoft::UI::Xaml::DragEventArgs const& args) const;
    std::optional<winrt::guid> playlistDragToken_;
    std::vector<std::int64_t> playlistDragIds_;
    std::vector<std::size_t> playlistDragRows_;
    std::uint64_t playlistDragRevision_{};
    void RemoveSelectedPlaylistItems();
    void QueueSelectedPlaylistNext();
    void MoveSelected(int delta);
    void Apply(iima::PlaybackSnapshot const& state);
    void ToggleFullscreen();
    void BeginClose();
    void WireFileActions();
    void WireSettingsControls();
    void PopulateAdvancedSettings();
    void ApplyAdvancedSetting(std::string const& key, iima::settings::PreferenceValue value);
    void ApplyWindowPreferences();
    void WireWindowPlaybackPolicy();
    std::unique_ptr<iima::WindowLifecycle> windowLifecycle_;
    std::unique_ptr<iima::PowerLifecycle> powerLifecycle_;
    iima::settings::PreferenceStore preferences_;
    void ApplySettingsSnapshot(iima::PlaybackSnapshot const& state);
    void WireAssociationControls();
    void FlushActivation();
    void InitializeDiagnostics();
    void UpdateDiagnostics(iima::PlaybackSnapshot const& state);
    void CloseDiagnostics();
    void OpenPendingFiles();
    winrt::fire_and_forget DropFiles(Microsoft::UI::Xaml::DragEventArgs args);
    winrt::fire_and_forget SaveScreenshot(bool clipboardOnly = false);
    winrt::fire_and_forget FinishScreenshot(std::shared_ptr<iima::ScreenshotStaging> staging,
        std::optional<std::filesystem::path> destination, bool copyToClipboard);
    winrt::fire_and_forget SavePlaylist();
    void LoadUserData();
    void WireUserDataControls();
    void ApplyUserData(iima::PlaybackSnapshot const& state);
    void RecordLastMedia();
    void RefreshHistory();
    void WireHistoryControls();
    iima::HistoryView historyProjection_;
    std::optional<iima::HistoryRemoval> historyUndo_;
    void MarkUserDataDirty();
    void PumpSave();
    void FinishClose();
    winrt::fire_and_forget ConfirmUnsavedClose();
    void ShowError(winrt::hstring const& message);
    std::shared_ptr<iima::PlayerSession> session_;
    iima::ApplicationCommandRouter commandRouter_;
    Microsoft::UI::Dispatching::DispatcherQueueTimer osdTimer_{nullptr};
    bool osdHaveState_{}, osdPaused_{}, osdMuted_{};
    double osdVolume_{}, osdSpeed_{};
    std::unique_ptr<iima::VideoPresenter> presenter_;
    std::optional<uint64_t> playlistRevision_, tracksRevision_, chaptersRevision_;
    std::vector<iima::PlaylistItem> playlist_;
    std::vector<iima::MediaTrack> tracks_;
    std::vector<iima::Chapter> chapters_;
    std::shared_ptr<void> swapChain_;
    std::unique_ptr<iima::MediaIntegration> mediaIntegration_;
    std::vector<std::string> pendingFiles_;
    bool pendingAppend_{}, inputResolving_{}, inputAssigned_{};
    OpenInputRouter openInputRouter_;
    std::stop_source inputStop_;
    std::stop_source subtitleMatchStop_;
    std::uint64_t subtitleMatchGeneration_{};
    uint64_t inputGeneration_{};
    Microsoft::UI::Dispatching::DispatcherQueueTimer activationTimer_{nullptr};
    std::vector<std::wstring> activationInputs_;
    bool activationDelayElapsed_{};
    std::function<void()> activationClosing_;
    std::function<void()> newWindow_, nextWindow_;
    std::unique_ptr<iima::DiagnosticLog> diagnostics_;
    void WireDiagnosticViewer();
    void RefreshDiagnosticViewer(bool force);
    void RenderDiagnosticViewer();
    void CopyDiagnosticSelection();
    winrt::fire_and_forget SaveDiagnosticSnapshot(bool all);
    Microsoft::UI::Dispatching::DispatcherQueueTimer diagnosticTimer_{nullptr};
    iima::DiagnosticLogSnapshot diagnosticSnapshot_;
    std::vector<iima::DiagnosticLogRecord> diagnosticVisible_;
    std::string lastDiagnostics_;
    bool diagnosticsClosed_{};
    iima::UserData userData_;
    iima::UserData savedUserData_; // Last snapshot submitted successfully, not another window's view.
    struct HistorySample { std::int64_t entryId; std::string path; std::optional<double> position, duration; };
    std::optional<HistorySample> lastMedia_;
    std::optional<int64_t> resumedEntry_;
    std::optional<int64_t> fullscreenPreferenceEntry_;
    bool discardUserData_{}, saveDialogOpen_{}, userDataLoaded_{}, settingsApplied_{}, saving_{}, saveDirty_{}, saveFailed_{}, coreClosed_{};
    bool screenshotPostProcessing_{};
    bool updating_{}, closing_{}, closeComplete_{}, pickerOpen_{}, fullscreen_{};
};
}
namespace winrt::IIMA::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}

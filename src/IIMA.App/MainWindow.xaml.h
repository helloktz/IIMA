#pragma once
#include <chrono>
#include "../IIMA.Core/Settings/SettingsSearch.h"
#include "MainWindow.g.h"
#include "VideoSurface.h"
#include "../IIMA.Core/Playback/PlayerSession.h"
#include "../IIMA.Core/Commands/ApplicationCommand.h"
#include "../IIMA.Core/Input/BindingConfiguration.h"
#include "../IIMA.Core/OSD/PlaybackOSD.h"
#include "../IIMA.Core/Filters/FilterPresets.h"
#include "../IIMA.Core/Geometry/CropSelection.h"
#include "Presentation/VideoPresenter.h"
#include "../IIMA.Core/Windows/MusicModePolicy.h"
#include <winrt/Windows.Graphics.h>
#include "Platform/MediaIntegration.h"
#include "Platform/WindowLifecycle.h"
#include "Platform/PowerLifecycle.h"
#include "Platform/WindowPlacement.h"
#include "Platform/LocalInputResolver.h"
#include "Platform/ScreenshotStaging.h"
#include "Platform/ScreenshotImage.h"
#include "Platform/ThumbnailExtractor.h"
#include "Platform/AssrtClient.h"
#include "Platform/OpenSubClient.h"
#include "Platform/ShooterClient.h"
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
    std::uint64_t lastVolumeMemoryRevision_{};
    void ApplyInitialWindowGeometry();
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
    bool InvokePluginMenuShortcut(std::string const& key, bool repeated);
    std::map<std::string, std::pair<std::string, std::string>, std::less<>> pluginMenuShortcuts_;
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
    void WireBindingLibrary();
    void WireBindingEditor();
    void WireKeyRecorder();
    bool recordingBindingKey_{};
    void LoadBindingRows();
    void UpdateBindingRowFields();
    void RefreshBindingRows(std::optional<std::size_t> selected = {});
    std::optional<std::size_t> SelectedBindingRow() const;
    void EditBindingRow(int action);
    void WriteBindingRows();
    bool projectingBindingRows_{};
    std::unique_ptr<iima::BindingConfiguration> bindingRowDraft_;
    std::string bindingRowSource_;
    std::optional<std::string> SelectedBindingSnapshot() const;
    void RefreshBindingLibrary(std::string selected = {});
    void SaveBindingSnapshot(bool replace);
    void RenameBindingSnapshot();
    void DeleteBindingSnapshot();
    void UndoBindingSnapshot();
    void LoadBindingSnapshot();
    bool projectingBindingLibrary_{};
    std::optional<std::pair<std::string, std::string>> bindingSnapshotUndo_;
    std::optional<std::string> bindingSnapshotUndoExpected_;
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
    void PrepareCropAspect();
    void ResetManagedCrop();
    std::uint64_t cropDraftEpoch_{};
    std::optional<std::int64_t> cropDraftEntry_;
    std::string cropDraftPath_;
    int cropDraftWidth_{}, cropDraftHeight_{};
    void WireVideoControls();
    void WireVideoSize();
    void ResizeToVideo(std::optional<double> scale);
    void WireVideoAdjustments();
    void UpdateVideoAdjustments(iima::PlaybackSnapshot const& state);
    bool projectingVideoAdjustments_{};
    void WireFilterEditor();
    void WireFilterPresets();
    void ResetFilterPresetCatalog();
    void ShowFilterPresetParameters();
    void GenerateFilterPreset();
    winrt::fire_and_forget PickFilterLutFile();
    std::uint64_t filterPresetEpoch_{};
    bool projectingFilterPresets_{};
    std::string lastGeneratedFilter_;
    std::vector<iima::FilterPreset> filterPresets_;
    std::map<std::string, Microsoft::UI::Xaml::FrameworkElement, std::less<>> filterPresetInputs_;
    void WireSavedFilters();
    void LoadSavedFilters();
    void RefreshSavedFilters();
    void EditSavedFilter(int action);
    void ApplySavedFilter(bool audio, std::size_t index);
    void OrganizeSavedFilter(int action);
    void CommitSavedFilterEdit(iima::SavedFilterLibrary before, bool audio, int selection);
    struct SavedFilterUndo {
        iima::SavedFilterLibrary library;
        std::uint64_t expectedRevision{};
        bool audio{};
        int selection{-1};
    };
    std::optional<SavedFilterUndo> savedFilterUndo_;
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
    void WireSubtitleEncoding();
    void UpdateSubtitleEncoding(iima::PlaybackSnapshot const& state);
    void ApplySubtitleColorDraft(unsigned kind);
    winrt::fire_and_forget ChooseSubtitleFont();
    std::stop_source fontCatalogStop_;
    Microsoft::UI::Xaml::Controls::ContentDialog subtitleFontDialog_{nullptr};
    winrt::fire_and_forget ChooseSubtitleColor(unsigned kind);
    Microsoft::UI::Xaml::Controls::ContentDialog subtitleColorDialog_{nullptr};
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
    std::vector<iima::shooter::SearchResult> onlineShooterResults_;
    void WireOpenSubAccount();
    void BeginOpenSubAccount(bool logout);
    void CancelOpenSubAccountPending();
    void ExpireOpenSubAccount();
    bool openSubAuthBusy_{};
    std::uint64_t openSubAuthGeneration_{};
    std::stop_source openSubAuthStop_;
    std::string openSubSessionKey_, openSubSessionToken_;
    std::chrono::steady_clock::time_point openSubSessionExpiry_{};
    bool onlineSubtitleSessionCredentials_{};
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
    winrt::fire_and_forget LoadExternalAudio();
    void WireEqualizerEditor();
    std::optional<iima::EQProfile> ReadEqualizerDraft();
    void SetEqualizerDraft(iima::EQProfile const& profile);
    void RefreshUserEqualizers();
    void EditUserEqualizer(int operation);
    std::array<Microsoft::UI::Xaml::Controls::NumberBox, iima::EQProfile::BandCount> equalizerBands_;
    std::vector<std::string> equalizerNames_;
    void WireApplicationCommands();
    void UpdateBufferingIndicator(iima::PlaybackSnapshot const& state);
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
    void WireMouseWheel();
    void WireInteractiveCrop();
    void StartInteractiveCrop();
    void FinishInteractiveCrop(bool commit, bool restorePlayback = true);
    void UpdateInteractiveCrop(iima::PlaybackSnapshot const& state);
    void PaintInteractiveCrop();
    std::optional<iima::CropMapping> InteractiveCropMapping() const;
    iima::CropSelection interactiveCrop_;
    std::optional<iima::CropMapping> cropMapping_;
    enum class CropTransactionPhase { None, Removing, Preparing, Editing, Writing };
    CropTransactionPhase cropTransactionPhase_{CropTransactionPhase::None};
    std::optional<iima::FilterChainSnapshot> cropOriginal_;
    iima::FilterNode cropExpectedEmpty_;
    std::uint64_t cropTransactionGeneration_{};
    bool cropCancelRequested_{};
    bool interactiveDelogo_{}; // Shared region UI; transaction owns the selected mode.
    void BeginManagedCropTransaction();
    void FinishManagedCropTransaction(bool commit, bool removeDelogo = false);
    void CompleteManagedCropTransaction(bool restorePlayback);
    void AbandonManagedCropTransaction(winrt::hstring const& message);
    std::optional<std::uint32_t> cropPointer_;
    std::uint64_t interactiveCropEpoch_{}, interactiveCropFilterRevision_{};
    std::int64_t interactiveCropEntry_{-1};
    bool interactiveCropWasPlaying_{};
    std::vector<Microsoft::UI::Xaml::Controls::Border> cropHandles_;
    void WireTimeDisplay();
    void UpdateTimeDisplay(iima::PlaybackSnapshot const& state);
    void WireMouseClicks();
    bool MouseClickAllowed();
    void CancelMouseClick();
    void PerformMouseClick(std::int64_t action);
    Microsoft::UI::Dispatching::DispatcherQueueTimer singleClickTimer_{nullptr};
    std::optional<std::int64_t> pendingClickAction_;
    std::uint64_t pendingClickEpoch_{};
    void WireFullscreenInformation();
    void UpdateFullscreenInformation(bool refreshTelemetry = false);
    void CloseFullscreenInformation();
    Microsoft::UI::Dispatching::DispatcherQueueTimer fullscreenInfoTimer_{nullptr};
    std::optional<std::int64_t> fullscreenInfoVisibilityToken_;
    void WireVideoCursor();
    void UpdateVideoCursor();
    void CloseVideoCursor();
    bool videoMouseInside_{};
    std::optional<std::int64_t> videoCursorVisibilityToken_;
    void WireControlBarVisibility();
    void ControlBarActivity();
    void UpdateControlBarPolicy();
    bool CanHideControlBar();
    Microsoft::UI::Dispatching::DispatcherQueueTimer controlBarTimer_{nullptr};
    bool controlBarHovered_{}, controlBarInactive_{};
    void WirePlaylistClipboard();
    void CopyPlaylistPaths(bool networkOnly);
    winrt::fire_and_forget PastePlaylistPaths();
    bool playlistPasteBusy_{};
    std::stop_source playlistPasteStop_;
    std::uint64_t playlistPasteGeneration_{};
    void ResolvePlaylistPaste(std::vector<std::wstring> inputs,
        std::uint64_t operation, std::uint64_t epoch, std::uint64_t revision,
        std::size_t destination);
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
    void WireSettingsSearch();
    void FilterAdvancedSettings();
    std::vector<iima::settings::SettingsSearchEntry> advancedSettingsIndex_;
    std::vector<Microsoft::UI::Xaml::FrameworkElement> advancedSettingsRows_;
    void ApplyAdvancedSetting(std::string const& key, iima::settings::PreferenceValue value);
    void ApplyWindowPreferences();
    void UpdateMediaIntegration();
    bool AnimationsAllowed() const;
    void RefreshAccessibility();
    bool systemAnimationsEnabled_{};
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
    std::vector<std::size_t> SelectedHistoryRows() const;
    void UpdateHistorySelection();
    void OpenSelectedHistory();
    bool projectingHistory_{};
    winrt::fire_and_forget RevealSelection(bool history);
    void WirePlaylistDeletion();
    winrt::fire_and_forget DeleteSelectedPlaylistFiles();
    bool playlistDeletionBusy_{};
    std::stop_source playlistDeletionStop_;
    Microsoft::UI::Xaml::Controls::ContentDialog playlistDeletionDialog_{nullptr};
    bool revealSelectionBusy_{};
    std::stop_source revealSelectionStop_;
    iima::HistoryView historyProjection_;
    void WireHistoryAvailability();
    void RequestHistoryAvailability(bool force = false);
    winrt::fire_and_forget ScanHistoryAvailabilityAsync(std::vector<std::string> paths);
    std::map<std::string, iima::HistoryAvailability, std::less<>> historyAvailability_;
    std::vector<std::string> historyAvailabilityPaths_;
    std::stop_source historyAvailabilityStop_;
    bool historyAvailabilityBusy_{}, historyAvailabilityPending_{}, historyAvailabilityRequested_{};
    void WireHistoryDeletion();
    winrt::fire_and_forget DeleteSelectedHistoryFiles();
    bool historyDeletionBusy_{};
    std::stop_source historyDeletionStop_;
    Microsoft::UI::Xaml::Controls::ContentDialog historyDeletionDialog_{nullptr};
    std::optional<iima::HistoryRemoval> historyUndo_;
    void MarkUserDataDirty();
    void PumpSave();
    void FinishClose();
    winrt::fire_and_forget ConfirmUnsavedClose();
    void ShowError(winrt::hstring const& message);
    std::shared_ptr<iima::PlayerSession> session_;
    iima::ApplicationCommandRouter commandRouter_;
    Microsoft::UI::Dispatching::DispatcherQueueTimer osdTimer_{nullptr};
    iima::PlaybackOSD playbackOSD_;
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
    void ShowScreenshotPreview(iima::ScreenshotPixels const& preview,
        std::optional<std::filesystem::path> destination);
    void WireScreenshotPreview();
    winrt::fire_and_forget OpenScreenshotPreviewFile(bool reveal);
    std::optional<std::filesystem::path> screenshotPreviewPath_;
    std::uint64_t screenshotPreviewGeneration_{};
    bool screenshotPreviewHovered_{}, screenshotPreviewFocused_{};
    void HideScreenshotPreview();
    Microsoft::UI::Dispatching::DispatcherQueueTimer screenshotPreviewTimer_{nullptr};
    bool updating_{}, closing_{}, closeComplete_{}, pickerOpen_{}, fullscreen_{};
};
}
namespace winrt::IIMA::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}

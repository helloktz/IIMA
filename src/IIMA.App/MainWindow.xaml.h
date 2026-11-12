#pragma once
#include <chrono>
#include "../IIMA.Core/Matching/SubtitleMatcher.h"
#include "../IIMA.Core/Playlist/PlaylistSubtitleAssociations.h"
#include "../IIMA.Core/Settings/SettingsSearch.h"
#include "../IIMA.Core/Media/TrackInspection.h"
#include "../IIMA.Core/Settings/ControlBarToolbar.h"
#include "../IIMA.Core/Settings/ControlBarLayout.h"
#include "MainWindow.g.h"
#include "VideoSurface.h"
#include "WheelSlider.h"
#include "../IIMA.Core/Playback/PlayerSession.h"
#include "../IIMA.Core/Commands/ApplicationCommand.h"
#include "../IIMA.Core/Input/BindingConfiguration.h"
#include "../IIMA.Core/OSD/PlaybackOSD.h"
#include "../IIMA.Core/Filters/FilterPresets.h"
#include "../IIMA.Core/Geometry/CropSelection.h"
#include "../IIMA.Core/Geometry/WindowGeometry.h"
#include "Presentation/VideoPresenter.h"
#include "../IIMA.Core/Windows/MusicModePolicy.h"
#include <winrt/Windows.Graphics.h>
#include "Platform/MediaIntegration.h"
#include "Platform/WindowLifecycle.h"
#include "Platform/PowerLifecycle.h"
#include "Platform/WindowPlacement.h"
#include "Platform/LocalInputResolver.h"
#include "Platform/ScreenshotStaging.h"
#include "../IIMA.Core/Screenshots/ScreenshotOptions.h"
#include "Platform/ScreenshotImage.h"
#include "Platform/ThumbnailExtractor.h"
#include "Platform/PlaylistDurationProbe.h"
#include "Platform/AssrtClient.h"
#include "Platform/OpenSubClient.h"
#include "Platform/ShooterClient.h"
#include "Platform/SubtitleMediaHash.h"
#include "Platform/SubtitleStaging.h"
#include "../IIMA.Core/OnlineSubtitles/SearchTransaction.h"
#include "../IIMA.Core/Filters/SavedFilterLibrary.h"
#include <deque>
#include <set>
#include "Plugins/PluginRuntime.h"
#include "Plugins/PluginPointerState.h"
#include "Plugins/PluginPlaybackProjection.h"
#include "../IIMA.Core/History/HistoryBrowser.h"
#include "../IIMA.Core/Diagnostics/DiagnosticLog.h"
#include "../IIMA.Core/Diagnostics/MpvLogBuffer.h"
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
    using OpenInputRouter = std::function<void(std::vector<std::string>, bool, bool, bool, bool)>;
    void SetOpenInputRouter(OpenInputRouter router) { openInputRouter_ = std::move(router); }
    void ReceiveResolvedInput(std::vector<std::string> paths, bool singleExplicitMedia = false);
    void SetSelectedInputNewWindowRouter(std::function<void(std::vector<std::string>)> router) { selectedInputNewWindowRouter_ = std::move(router); }
    bool CanReuseForInput() const;
    bool HasOpenInput(std::string const& path) const;
    bool IsClosing() const noexcept { return closing_; }
    void SetActivationClosing(std::function<void()> callback) { activationClosing_ = std::move(callback); }
private:
    winrt::fire_and_forget OpenFile(bool append = false, bool invertWindowPolicy = false);
    winrt::fire_and_forget OpenFolder(bool append = false, bool invertWindowPolicy = false);
    winrt::fire_and_forget OpenSubtitle();
    winrt::fire_and_forget OpenNetworkUrl(bool invertWindowPolicy = false, bool editFailedAttempt = false);
    Microsoft::UI::Xaml::Controls::ContentDialog openUrlDialog_{nullptr};
    void WireWindowPlacement();
    void CapturePlacement();
    std::uint64_t lastVolumeMemoryRevision_{};
    void ApplyInitialWindowGeometry();
    void RestorePlacement(std::optional<iima::WindowPlacementSnapshot> saved);
    void SaveWindowPlacement();
    std::optional<iima::WindowPlacementSnapshot> windowPlacement_;
    std::optional<Windows::Graphics::RectInt32> placementProgrammaticBounds_;
    bool placementTouched_{}, restoringPlacement_{}, placementSavePending_{}, placementWritable_{true}, musicTransition_{};
    void WirePluginInspection();
    winrt::fire_and_forget StartLocalPlugin();
    void StopLocalPlugin();
    winrt::fire_and_forget RetirePlugin(std::unique_ptr<iima::plugins::PluginRuntime> runtime);
    void ApplyPluginMenu(std::string const& json);
    void ClearPluginMenu();
    void WireNativePluginMenu(Microsoft::UI::Xaml::Controls::MenuBarItem const&);
    bool PluginMenuVisible() const noexcept;
    void QueuePluginMenuRefresh();
    void FlushPluginMenuRefresh();
    Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem nativePluginMenu_{nullptr};
    std::size_t pluginMenuVisibleCount_{};
    bool pluginMenuRefreshQueued_{}, pluginMenuClearPending_{};
    std::optional<std::string> pendingPluginMenuJson_;
    std::uint64_t pendingPluginMenuGeneration_{};
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
    void WireMusicVolume();
    void UpdateMusicVolume();
    void CloseMusicVolume();
    bool MusicVolumeAvailable();
    bool MusicVolumeContextValid();
    bool musicVolumeOpen_{}, musicVolumeUpdating_{};
    std::uint64_t musicVolumeGeneration_{}, musicVolumeIntent_{}, musicVolumeInput_{};
    std::optional<iima::MusicModePolicy::Transition> UpdateMusicContext();
    void UpdateMusicMode();
    void ApplyMusicTransition(iima::MusicModePolicy::Transition const& transition);
    void ApplyMusicLayout(bool compact);
    iima::MusicModePolicy musicMode_;
    std::uint64_t musicGeneration_{};
    iima::MediaState lastMusicMediaState_{iima::MediaState::Empty};
    std::optional<Windows::Graphics::RectInt32> beforeMusicBounds_;
    bool beforeMusicMaximized_{};
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
    void WirePlayerMenus();
    void RefreshPlayerMenus(bool shortcuts = false);
    void FillTrackMenu(Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem const&, std::string type, bool secondary = false);
    void FillChapterMenu(Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem const&);
    void FillRecentMenu(Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem const&);
    void FillAudioDeviceMenu(Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem const&);
    void FillSavedFilterMenu(Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem const&, bool audio);
    struct PlayerMenuAction {
        Microsoft::UI::Xaml::Controls::MenuFlyoutItem item{nullptr};
        std::function<winrt::hstring()> title;
        std::function<void()> execute;
        std::function<bool()> enabled, checked;
        std::vector<std::string> action;
        bool application{}, mediaBound{};
        std::uint64_t menuGeneration{}, menuIntent{};
        std::optional<iima::KeyMapping> binding;
    };
    std::vector<PlayerMenuAction> playerMenuActions_;
    Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem recentMenu_{nullptr}, videoTrackMenu_{nullptr},
        audioTrackMenu_{nullptr}, subtitleTrackMenu_{nullptr}, secondaryTrackMenu_{nullptr}, chapterMenu_{nullptr},
        audioDeviceMenu_{nullptr}, savedVideoFilterMenu_{nullptr}, savedAudioFilterMenu_{nullptr};
    bool playerMenusReady_{}, playerMenuRefreshQueued_{}, playerRecentMenuDirty_{true}, playerMenuShortcutsDirty_{};
    std::set<std::size_t> playerMenuVisibleItems_;
    std::string playerMenuDynamicKey_, playerRecentMenuStateKey_;
    void WireVideoControls();
    void WireVideoSize();
    void UpdateWindowAspectConstraint(bool layoutChanged = false);
    std::optional<iima::WindowAspectConstraint> windowAspectProjection_;
    void ResizeToVideo(std::optional<double> scale);
    enum class VideoResizeResult { Applied, Retry, Unsupported };
    VideoResizeResult TryResizeToVideo(std::optional<double> scale, bool automatic);
    void UpdateAutomaticVideoSize(bool layoutChanged = false);
    void AcceptExternalWindowScale(iima::PlaybackSnapshot const& state);
    void QueueWindowScaleFeedback();
    void PublishWindowScale();
    std::uint64_t externalWindowScaleRevision_{};
    bool automaticResizeExternal_{}, windowScaleFeedbackQueued_{};
    double automaticResizeExternalScale_{1};
    std::optional<double> windowScaleExpectedPanelWidth_;
    void CancelAutomaticVideoSize(bool discardInitial = true);
    static std::optional<iima::GeometrySize> VideoDisplaySize(iima::MediaProperties const& output);
    std::optional<iima::GeometrySize> automaticResizeVideo_;
    bool initialVideoSizePending_{true}, automaticResizeInitial_{}, automaticResizeStart_{true};
    bool automaticResizeKeepArea_{}, automaticResizeAwaitingStart_{}, automaticReconfigArmed_{};
    std::string initialVideoGeometry_;
    std::uint64_t automaticResizeGeneration_{}, automaticResizeIntent_{}, automaticResizeInformation_{};
    bool automaticResizeConsumed_{true}, automaticResizeAttempting_{}, automaticResizeLoaded_{};
    bool automaticResizePhysical_{};
    int automaticResizeTiming_{}, automaticResizeOption_{};
    unsigned automaticResizeAttempts_{};
    std::chrono::steady_clock::time_point automaticResizeDeadline_{};
    void WireVideoAdjustments();
    void UpdateVideoAdjustments(iima::PlaybackSnapshot const& state);
    bool projectingVideoAdjustments_{};
    void WireControlBarLayout();
    void UpdateControlBarLayout();
    void UpdateControlBarOverlays();
    void CancelControlBarDrag();
    void FinishControlBarDrag(bool cancel);
    bool oscDragging_{}, oscLayoutUpdating_{};
    iima::GeometryRect oscRect_{};
    iima::GeometrySize oscViewport_{};
    double oscTopInset_{}, oscDragScale_{};
    std::int64_t oscDragMode_{};
    bool oscDragFullscreen_{}, oscDragMusic_{};
    iima::settings::FloatingControlBarPosition oscDragPosition_{}, oscDragBaseline_{};
    void WireToolbarSettings();
    void UpdateControlBarToolbar();
    void RenderToolbarDraft(int selection = -1);
    void UpdateToolbarDraftControls();
    void EditToolbarDraft(int operation);
    void ApplyToolbarDraft();
    void CancelToolbarDraft();
    bool ToolbarActionAvailable(iima::settings::ToolbarAction action);
    void ExecuteToolbarAction(iima::settings::ToolbarAction action);
    std::vector<iima::settings::ToolbarAction> toolbarDraft_;
    std::vector<std::pair<iima::settings::ToolbarAction, Microsoft::UI::Xaml::Controls::Button>> toolbarButtons_;
    std::string toolbarBaseline_, toolbarApplied_;
    bool toolbarDraftReady_{}, toolbarDraftDirty_{}, toolbarProjecting_{}, toolbarBuilt_{};
    void WireScrubbing();
    void BeginScrubbing();
    void UpdateScrubbing();
    void FinishScrubbing(bool cancelled);
    void CancelScrubbing();
    void PositionSliderChanged(double value);
    std::uint64_t scrubToken_{}, scrubGeneration_{}, scrubIntent_{}, scrubInput_{}, scrubSuppressEpoch_{};
    double scrubDuration_{};
    std::int64_t scrubPresenter_{};
    bool scrubStarting_{}, scrubSuppress_{}, scrubFullscreen_{}, scrubMusic_{};
    void WireSliderWheels();
    void WireTrackInspector();
    std::optional<iima::MediaTrack> SelectedRemovableTrack();
    bool CanRemoveInspectedTrack();
    winrt::fire_and_forget RemoveInspectedTrack();
    void UpdateExternalTrackRemoval();
    void InvalidateExternalTrackRemoval();
    Microsoft::UI::Xaml::Controls::ContentDialog externalRemovalDialog_{nullptr};
    bool externalRemovalUpdating_{}, externalRemovalStarting_{};
    winrt::hstring externalRemovalLabel_;
    std::uint64_t trackInspectionSelectionRevision_{}, externalRemovalSelection_{}, externalRemovalTracks_{}, externalRemovalTrackIntent_{}, externalRemovalRevision_{},
        externalRemovalGeneration_{}, externalRemovalIntent_{}, externalRemovalInput_{}, externalRemovalObservedInput_{};
    void UpdateTrackInspector(bool force = false);
    void ClearTrackInspector();
    bool TrackInspectorCurrent() const;
    void ShowInspectedTrack();
    void CopyTrackInspection();
    std::vector<iima::TrackInspection> inspectedTracks_;
    std::optional<std::uint64_t> trackInspectorRevision_;
    std::uint64_t trackInspectorGeneration_{}, trackInspectorIntent_{}, trackInspectorInput_{};
    bool trackInspectorProjecting_{};
    void WireInitialWindow();
    bool InitialWindowEligible() const;
    void UpdateInitialWindow(bool invalidate = false);
    void OpenInitialHistory(iima::HistoryEntry entry, std::uint64_t epoch);
    bool initialWindowVisible_{}, initialWindowResume_{};
    std::uint64_t initialWindowEpoch_{};
    std::vector<iima::HistoryEntry> initialWindowEntries_;
    void WireFilterEditor();
    void WireQuickLut();
    void UpdateQuickLut();
    bool QuickLutAvailable() const;
    winrt::fire_and_forget ImportQuickLut(std::wstring path = {});
    void SubmitQuickLut(std::optional<std::string> path);
    bool quickLutBusy_{};
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
    void WireABLoopMarkers();
    void UpdateABLoopMarkers();
    bool ABMarkerAvailable();
    void BeginABMarker(bool pointB);
    void CancelABMarker();
    void FinishABMarker(bool cancelled);
    void ABMarkerKey(bool pointB, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
    bool abMarkerDragging_{}, abMarkerB_{}, abMarkerUpdating_{};
    std::optional<double> abMarkerA_,abMarkerBValue_;
    double abMarkerDraft_{},abMarkerDuration_{},abMarkerStart_{},abMarkerTravel_{},abMarkerScale_{};
    std::uint64_t abMarkerGeneration_{},abMarkerIntent_{},abMarkerInput_{},abMarkerEditIntent_{},abMarkerTemplate_{};
    std::uint64_t abDraftIntent_{};
    Microsoft::UI::Windowing::AppWindowPresenterKind abMarkerPresenter_{};
    std::int64_t abMarkerOscPosition_{};
    bool abMarkerMusic_{};
    Windows::Foundation::Point abMarkerOrigin_{};
    Windows::Graphics::SizeInt32 abMarkerWindowSize_{};
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
    bool CanStartSubtitleReload();
    void StartSubtitleReload(std::optional<std::string> encoding = {});
    void UpdateSubtitleReload();
    void InvalidateSubtitleReload();
    bool subtitleReloadUpdating_{}, subtitleReloadStarting_{};
    std::uint64_t subtitleReloadRevision_{}, subtitleReloadGeneration_{}, subtitleReloadIntent_{}, subtitleReloadInput_{}, subtitleReloadObservedInput_{};
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
    void PrepareAutomaticFiles(std::vector<std::string> const& paths, bool eligible);
    void UpdateAutomaticFiles();
    void CancelAutomaticFiles();
    void ClearAutomaticSubtitleCache();
    void UpdateAutomaticSubtitleCache();
    void RetainAutomaticSubtitleMatches(iima::SubtitleMatchResult const& matches);
    std::vector<iima::MediaSubtitleMatch> automaticSubtitleMatches_;
    std::vector<iima::PlaylistItem> automaticSubtitleQueue_;
    bool automaticSubtitlePending_{};
    std::uint64_t automaticSubtitleExpansion_{}, automaticSubtitleExpansionGeneration_{}, automaticSubtitleIntent_{}, automaticSubtitleInput_{}, automaticSubtitleEditIntent_{};
    std::uint64_t automaticSubtitleSeenGeneration_{}, automaticSubtitleWaitingGeneration_{}, automaticSubtitleUserIntent_{};
    std::chrono::steady_clock::time_point automaticSubtitleDeadline_{};
    bool pendingAutoFileMatch_{}, automaticFilesStarted_{};
    std::string automaticFilesPath_;
    std::uint64_t automaticFilesGeneration_{}, automaticFilesIntent_{}, automaticFilesInputRevision_{}, automaticFilesEditIntent_{}, automaticFilesMediaGeneration_{}, automaticFilesSubtitleIntent_{};
    std::stop_source automaticFilesStop_;
    std::chrono::steady_clock::time_point automaticFilesDeadline_{};
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
    void WireCompactOverlayControls();
    void UpdateCompactOverlayControls();
    bool CompactTransportAvailable();
    bool compactTransition_{}, compactLayoutPending_{};
    void UpdateCompactLayout();
    bool EnterCompactOverlay();
    bool ExitCompactOverlay(bool restoreGeometry = true);
    struct CompactRestoreState {
        Microsoft::UI::Windowing::AppWindowPresenter presenter{nullptr};
        std::optional<iima::WindowPlacementSnapshot> geometry;
        bool maximized{}, music{};
    };
    std::optional<CompactRestoreState> compactRestore_;
    void ExecuteApplicationCommand(iima::ApplicationCommand command);
    void ApplyCollections(iima::PlaybackSnapshot const& state);
    void PlaySelected();
    std::vector<std::int64_t> SelectedPlaylistIds() const;
    std::vector<std::size_t> SelectedPlaylistRows();
    void UpdatePlaylistSelection();
    void UpdatePlaylistDurations(bool refresh = false);
    void CancelPlaylistDurations(bool clearCache = true);
    struct PlaylistDurationCacheEntry { std::optional<double> seconds; bool complete{}; std::optional<iima::PlaylistDurationFingerprint> fingerprint; std::int64_t entryId{-1}; };
    std::map<std::string, PlaylistDurationCacheEntry, std::less<>> playlistDurationCache_;
    std::size_t playlistDurationBytes_{};
    std::vector<std::pair<std::int64_t,std::string>> playlistDurationWindow_;
    std::stop_source playlistDurationStop_;
    std::uint64_t playlistDurationEpoch_{}, playlistDurationRevision_{}, playlistDurationMedia_{}, playlistDurationIntent_{}, playlistDurationInput_{};
    bool playlistDurationBusy_{}, playlistDurationProjecting_{}, playlistDurationEnabled_{};
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
    void WirePlaylistSubtitleControls();
    void UpdatePlaylistSubtitleControls();
    winrt::fire_and_forget AddPlaylistSubtitleAssociation();
    void ClearPlaylistSubtitleAssociation();
    std::uint64_t playlistSubtitleSelectionRevision_{};
    std::vector<std::string> playlistSubtitleDisplayPaths_;
    std::vector<std::string> PlaylistSubtitlePaths(std::int64_t id, std::string const& path) const;
    bool SetPlaylistSubtitlePaths(std::int64_t id, std::string const& path, std::vector<std::string> paths);
    void UpdatePlaylistSubtitleAssociations();
    bool HasPlaylistSubtitleOverride(std::int64_t id, std::string const& path) const;
    iima::PlaylistSubtitleAssociations playlistSubtitleAssociations_;
    std::uint64_t playlistSubtitleInput_{}, playlistSubtitleSeenGeneration_{}, playlistSubtitleWaitingGeneration_{}, playlistSubtitleUserIntent_{};
    std::chrono::steady_clock::time_point playlistSubtitleDeadline_{};
    void WirePlaylistClipboard();
    void CopyPlaylistPaths(bool networkOnly, bool cut = false);
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
    void PlaySelectedPlaylistInNewWindow();
    void UpdatePlaylistBrowserControls();
    winrt::fire_and_forget OpenSelectedPlaylistInBrowser();
    bool playlistBrowserBusy_{};
    void MoveSelected(int delta);
    void Apply(iima::PlaybackSnapshot const& state);
    void ToggleFullscreen();
    void BeginClose();
    void WireFileActions();
    void WireNetworkOpen();
    void BeginNetworkOpen(std::vector<std::string> const& paths);
    void BindNetworkOpenAttempt(std::vector<std::string> const& paths, bool append);
    void UpdateNetworkOpen(iima::PlaybackSnapshot const& state);
    void CancelNetworkOpen();
    void RetryNetworkOpen();
    std::string networkOpenUrl_;
    std::uint64_t networkOpenInputRevision_{}, networkOpenIntent_{}, networkOpenBaselineIntent_{};
    bool networkOpenPending_{}, networkOpenFailed_{};
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
    void DragOverFiles(Microsoft::UI::Xaml::DragEventArgs const& args);
    std::uint64_t dropReadGeneration_{};
    std::stop_source dropResolveStop_;
    winrt::fire_and_forget DropFiles(Microsoft::UI::Xaml::DragEventArgs args, std::optional<std::size_t> playlistRow = {});
    struct ScreenshotOutputRequest {
        bool save{}, copy{}, preview{}, forceSaveAs{};
        iima::ScreenshotFormat format{iima::ScreenshotFormat::Png};
        std::filesystem::path directory;
        std::string filename;
    };
    winrt::fire_and_forget SaveScreenshot(bool clipboardOnly = false, bool forceSaveAs = false);
    winrt::fire_and_forget FinishScreenshot(std::shared_ptr<iima::ScreenshotStaging> staging, ScreenshotOutputRequest request);
    void WireScreenshotSettings();
    void UpdateScreenshotSettings();
    winrt::fire_and_forget ChooseScreenshotDirectory();
    std::uint64_t screenshotSequence_{};
    Windows::Foundation::IAsyncInfo screenshotPickerOperation_{nullptr};
    winrt::fire_and_forget SavePlaylist();
    void LoadUserData();
    void WireUserDataControls();
    void ApplyUserData(iima::PlaybackSnapshot const& state);
    void RecordLastMedia();
    void RefreshHistory();
    void WireHistoryControls();
    std::vector<std::size_t> SelectedHistoryRows() const;
    void UpdateHistorySelection();
    void OpenSelectedHistory(bool newWindow = false);
    std::uint64_t historySelectionRevision_{};
    std::function<void(std::vector<std::string>)> selectedInputNewWindowRouter_;
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
    void WireMpvDiagnosticViewer();
    void RefreshMpvDiagnosticViewer();
    void RenderMpvDiagnosticViewer();
    void ResetMpvDiagnosticView();
    void CloseMpvDiagnosticViewer();
    void CopyMpvDiagnosticSelection();
    winrt::fire_and_forget ExportMpvDiagnosticSnapshot();
    Microsoft::UI::Dispatching::DispatcherQueueTimer mpvLogTimer_{nullptr};
    iima::MpvLogSnapshot mpvLogSnapshot_;
    std::vector<iima::MpvLogRecord> mpvLogVisible_;
    std::uint64_t mpvLogEpoch_{};
    bool mpvLogExportBusy_{};
    std::stop_source mpvLogExportStop_;
    Windows::Foundation::IAsyncInfo mpvLogPicker_{nullptr};
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
    struct FullscreenRestoreState {
        Microsoft::UI::Windowing::AppWindowPresenter presenter{nullptr};
        std::optional<iima::WindowPlacementSnapshot> geometry;
        iima::GeometrySize chrome;
        bool maximized{}, videoSizingEligible{};
    };
    std::optional<FullscreenRestoreState> fullscreenRestore_;
    bool fullscreenTransition_{};
    bool ExitFullscreen(bool restoreGeometry = true);
    bool updating_{}, closing_{}, closeComplete_{}, pickerOpen_{}, fullscreen_{};
};
}
namespace winrt::IIMA::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}

#pragma once
#include "PlaybackState.h"
#include "PlaybackActions.h"
#include "../Subtitles/SubtitleStyle.h"
#include "../Audio/Equalizations.h"
#include "../Persistence/UserData.h"
#include "../Playlist/PlaylistPlan.h"
#include "../Settings/PreferenceSchema.h"
#include "../Settings/WindowPlaybackPolicy.h"

namespace iima {
enum class AutoPlaylistExpansionPhase { None, Running, Completed, Cancelled };
struct AutoPlaylistExpansionState {
    std::uint64_t revision{}, openIntent{}, mediaGeneration{}, editIntent{};
    std::int64_t sourceEntry{-1};
    AutoPlaylistExpansionPhase phase{AutoPlaylistExpansionPhase::None};
    // Populated only from the final successful explicit playlist readback.
    // IDs of newly appended entries are never inferred from paths or counters.
    std::vector<PlaylistItem> playlist;
};
// Command acknowledgements, not proof that new subtitle text has been rendered.
// A cancelled in-flight command cannot be recalled and may still change tracks.
struct SubtitleReloadStatus {
    enum class Phase { Idle, Reading, Encoding, Reloading, Restoring, Completed, Cancelled, Failed };
    Phase phase{Phase::Idle};
    std::uint64_t revision{};
    std::size_t total{}, completed{}, failed{}, skipped{}, remaining{};
    bool inFlight{};
    std::string error;
};
class PlayerSession final : public std::enable_shared_from_this<PlayerSession> {
public:
    using Dispatch = std::function<void(std::function<void()>)>;
    using Changed = std::function<void(PlaybackSnapshot const&)>;
    static std::shared_ptr<PlayerSession> Create(Dispatch dispatch, Changed changed);
    void Start(std::wstring dllPath);
    bool StartMpvLogCapture(std::string level) { return client_->StartMpvLogCapture(std::move(level)); }
    void StopMpvLogCapture() { client_->StopMpvLogCapture(); }
    void ClearMpvLogCapture() { client_->ClearMpvLogCapture(); }
    MpvLogSnapshot SnapshotMpvLogs() const { return client_->SnapshotMpvLogs(); }
    // Only a presenter with a verified owned swapchain lifetime may grant this.
    void SetPresentationReady(bool ready);
    bool PresentationReady() const noexcept { return presentationReady_; }
    void Open(std::string utf8Path);
    bool CancelNetworkOpen(std::uint64_t expectedIntent);
    void OpenMultiple(std::vector<std::string> paths, bool append = false);
    void PlaylistNext();
    void PlaylistPrevious();
    void PlaylistPlay(std::size_t index);
    void PlaylistRemove(std::size_t index);
    void PlaylistMove(std::size_t from, std::size_t finalIndex);
    void SortPlaylist(PlaylistSort order);
    void ClearPlaylist();
    // shuffled=true asks mpv to undo its last shuffle, false asks to shuffle.
    void ShufflePlaylist(bool shuffled);
    void InsertPlaylist(std::vector<std::string> paths, std::optional<std::size_t> at = {});
    // False means not started. True is accepted/no-op, not completed; playlist
    // events and the existing edit cursor remain the source of truth.
    bool ExpandAutoPlaylist(std::vector<std::string> before, std::vector<std::string> after,
                            std::int64_t expectedEntry, std::uint64_t expectedRevision,
                            std::uint64_t expectedIntent, std::uint64_t expectedGeneration);
    void RemovePlaylistItems(std::vector<std::size_t> indices);
    void DropPlaylistItems(std::vector<std::size_t> indices, std::size_t beforeRow);
    void QueuePlaylistItemsNext(std::vector<std::size_t> indices);
    // Cancels only unsent work, never claims rollback of a submitted command.
    void CancelPlaylistEdits();
    AutoPlaylistExpansionState const& AutoPlaylistExpansion() const noexcept { return autoPlaylistExpansion_; }
    std::uint64_t PlaylistEditIntentRevision() const noexcept { return playlistEditIntentRevision_; }
    bool PlaylistEditBusy() const noexcept { return static_cast<bool>(playlistEdit_); }
    bool PlaylistShuffled() const noexcept { return playlistShuffled_; }
    void SelectTrack(std::string type, std::int64_t id);
    void CycleTrack(std::string type);
    void SelectChapter(std::size_t index);
    void StepChapter(int direction);
    void AddSubtitle(std::string path, bool select = true);
    std::uint64_t SubtitleEditIntentRevision() const noexcept { return subtitleEditIntentRevision_; }
    // True means queued, not decoded. Actual tracks/errors arrive asynchronously.
    bool LoadExternalAudio(std::string path);
    void ApplySubtitleStyle(SubtitleStyle const& style, std::vector<std::string> fields = {});
    void SetSubtitleScale(double scale, bool primary = true);
    void SetSubtitlePosition(int percent, bool primary = true);
    void SetSecondarySubtitleDelay(double seconds);
    void SelectSecondarySubtitle(std::int64_t id);
    void SetSecondarySubtitlesVisible(bool visible);
    void ReloadSubtitles();
    // Queue the encoding change; reload matching external tracks only after success.
    bool SetSubtitleEncoding(std::string encoding);
    bool SubtitleEncodingBusy() const noexcept { return SubtitleReloadBusy(); }
    SubtitleReloadStatus const& SubtitleReload() const noexcept { return subtitleReloadStatus_; }
    bool SubtitleReloadBusy() const noexcept {
        return subtitleReloadStatus_.inFlight || subtitleReloadStatus_.phase == SubtitleReloadStatus::Phase::Reading ||
            subtitleReloadStatus_.phase == SubtitleReloadStatus::Phase::Restoring ||
            subtitleReloadStatus_.phase == SubtitleReloadStatus::Phase::Encoding ||
            subtitleReloadStatus_.phase == SubtitleReloadStatus::Phase::Reloading;
    }
    void CancelSubtitleReload();
    void SeekSubtitle(int direction, bool primary = true);
    void StepSubtitleTiming(int direction, bool primary = true);
    void AdjustSubtitleScale(int direction);
    // Completion runs on the UI dispatcher, including immediate rejection.
    // Close cancellation is delivered only after mpv has released its file writes.
    using ScreenshotCompleted = std::function<void(bool, std::string)>;
    void ScreenshotToFile(std::string path, ScreenshotCompleted completion = {});
    using WatchedPropertiesCompleted = std::function<void(std::vector<WatchedPropertyValue>, std::string)>;
    // UI thread only. One request at a time; callback on the UI dispatcher.
    // Immediate rejection also invokes completion. False means not enqueued.
    bool ReadWatchedProperties(std::vector<std::string> names, WatchedPropertiesCompleted completion);
    // Suppresses a stale UI request without cancelling unrelated mpv commands.
    void CancelWatchedProperties();
    void Pause(bool paused);
    void TogglePause();
    // UI-thread notification; owned dialogs are filtered by the Windows adapter.
    void WindowLifecycleChanged(bool inactive, bool minimized);
    // UI-thread suspend notification: never wakes/pauses an idle or closing core.
    // Caller applies pauseWhenGoesToSleep. No automatic resume on wake.
    void SystemWillSuspend();
    // UI-thread scrub transaction. Only normal End may restore prior playback.
    std::uint64_t BeginScrub();
    bool UpdateScrub(std::uint64_t token,double seconds);
    void EndScrub(std::uint64_t token);
    void CancelScrub(); // Does not undo submitted seek/pause; never resumes.
    std::uint64_t ActiveScrubToken() const noexcept { return scrubToken_; }
    void Seek(double seconds);
    void SeekRelative(double seconds, bool exact = true);
    void SeekRelativeWithMode(double seconds, RelativeSeekMode mode);
    void StepFrame(bool backwards = false);
    // Explicit typed loop intents, including rejected/no-op attempts. Drag
    // drafts compare this before submission, not only delayed observed A/B.
    std::uint64_t ABLoopEditIntentRevision() const noexcept { return abLoopEditIntentRevision_; }
    void CycleABLoop();
    void ClearABLoop();
    // UI-thread draft guard; returns submission acceptance, not mpv success.
    bool EditABLoopPoint(bool pointB, double seconds,
        std::optional<double> expectedA, std::optional<double> expectedB);
    void Stop();
    void SetVolume(double value);
    void AdjustVolumeByWheel(double delta);
    void SetSpeed(double value);
    void SetVideoAspect(std::string aspect);
    void SetVideoRotation(int degrees);
    void ToggleVideoOrientation(bool mirror);
    void SetDeinterlace(bool enabled);
    void SetHardwareDecoding(std::string mode);
    void SetVideoEqualizer(std::string property, int value);
    void AddVideoFilter(std::string filter);
    void RemoveVideoFilter(std::string label);
    void AddFilter(bool audio, std::string expression);
    void EditFilters(bool audio, FilterEditPlan plan);
    // Accepted requests receive one UI-thread reply plus owned readback; set
    // success alone does not prove the expected chain or current media matches.
    using FilterEditCompleted = std::function<void(bool, FilterChainSnapshot, std::string)>;
    bool EditFiltersWithCompletion(bool audio, FilterEditPlan plan, FilterEditCompleted completion);
    void SetCompositionSize(unsigned width, unsigned height);
    // UI supplies actual video viewport scale, not desired outer-frame scale.
    void SetWindowScaleFromWindow(double scale);
    // Local preferences never overwrite observed playback fields. Call on UI thread.
    void ApplySettings(UserSettings const& settings);
    // Immediate values are submitted now (or at Ready). NextMedia values are
    // submitted before the next explicit Open/Playlist switch, not at START_FILE.
    // This method does not claim that asynchronous commands succeeded.
    void ApplyAdvancedPreferences(settings::PreferenceStore const& preferences,
        bool explicitAudioDevice = false);
    void SetPauseWhenOpen(bool value);
    void SetAutoPlayNext(bool value);
    void SetMuted(bool value);
    void ToggleMute();
    void SetSubtitlesVisible(bool value);
    void SetAudioDelay(double seconds);
    void SetAudioEqualizer(EQProfile const& profile);
    void RemoveAudioEqualizer();
    void SetSubtitleDelay(double seconds);
    void SetRepeatMode(RepeatMode mode);
    void SetMaxVolume(double value);
    void SetScreenshotMode(ScreenshotMode mode);
    // UI must detach presenter and subscriptions BEFORE calling this.
    void CloseAsync(std::function<void()> completion = {});
    PlaybackSnapshot const& Snapshot() const noexcept { return snapshot_; }
private:
    PlayerSession(Dispatch dispatch, Changed changed);
    void OnEvent(MpvEvent event);
    bool BeginSubtitleReload(std::optional<std::string> encoding);
    bool HandleSubtitleReloadReply(MpvEvent const& event);
    bool SubtitleReloadContextValid() const;
    enum class SubtitleReloadStep { Baseline, Encoding, Reload, Refresh, PrimarySet, SecondaryRead, SecondarySet, Verify };
    void ReadSubtitleReloadState(SubtitleReloadStep step);
    void SubmitSubtitleReloadCommand(std::vector<std::string> command, SubtitleReloadStep step);
    void FailSubtitleReload(std::string error);
    void AdvanceSubtitleReload(MpvEvent const& fresh);
    void RestoreSubtitleReloadSlot(MpvEvent const& fresh, bool primary);
    std::optional<std::int64_t> ResolveReloadSelection(MpvEvent const& fresh, bool primary) const;
    struct ReloadSelection { std::int64_t id{}; bool external{}; std::string path; };
    ReloadSelection subtitleReloadPrimary_, subtitleReloadSecondary_;
    SubtitleReloadStep subtitleReloadStep_{SubtitleReloadStep::Baseline};
    std::optional<std::string> subtitleReloadEncoding_;
    std::int64_t subtitleReloadEntry_{-1};
    std::string subtitleReloadMediaPath_;
    bool subtitleReloadRestoreFailed_{};
    SubtitleReloadStatus subtitleReloadStatus_;
    std::uint64_t subtitleReloadRequest_{}, subtitleReloadGeneration_{}, subtitleReloadOpenIntent_{}, subtitleReloadEditIntent_{};
    std::size_t subtitleReloadIndex_{};
    std::vector<std::pair<std::int64_t, std::string>> subtitleReloadTracks_;
    void Notify();
    void SubmitAudioDevicePreference(std::string device, bool protectSelection = true);
    void PumpAudioDevicePreference();
    void UpdateAudioDeviceRecovery(MpvEvent const& event);
    std::optional<std::string> pendingAudioDevice_;
    bool pendingAudioDeviceProtection_{}, audioDeviceWriteProtection_{}, startupAudioDeviceProtection_{};
    std::uint64_t audioDeviceIntent_{}, audioDeviceWrite_{}, audioDeviceRecovery_{};
    std::uint64_t audioDeviceRecoveryIntent_{}, audioDeviceRecoveryListRevision_{};
    void PumpWindowScale();
    void BlockWindowScaleRetry();
    bool HandleWindowScaleEvent(MpvEvent const& event);
    void PublishExternalWindowScale(double scale);
    std::optional<double> pendingWindowScale_, confirmedWindowScale_, windowScaleEcho_, blockedWindowScale_;
    bool windowScaleWriteSucceeded_{};
    std::uint64_t windowScaleWrite_{}, windowScaleRead_{}, windowScaleGeneration_{}, windowScaleIntent_{};
    void HandleNetworkOpenEvent(MpvEvent const& event);
    void EvaluateNetworkOpen();
    std::uint64_t networkPauseRequest_{}, networkLoadRequest_{};
    std::optional<std::int64_t> networkEntry_;
    std::uint64_t networkBaselineGeneration_{};
    std::optional<std::uint64_t> networkOwnedGeneration_;
    // Small lifecycle journal handles FILE_LOADED/END_FILE arriving before
    // the loadfile reply identifies the entry. Never stores paths or metadata.
    struct NetworkLifecycle { MpvEvent::Kind kind; std::int64_t entry; int error, reason; std::int64_t insertedEntry; std::uint64_t generation; };
    std::vector<NetworkLifecycle> networkLifecycle_;
    bool networkJournalOverflow_{};
    void BeginMediaOriginIntent(bool explicitOpen);
    void ObserveMediaOrigin(MpvEvent const& event);
    void ResolveMediaOrigin();
    std::uint64_t videoGeometryRequest_{}, videoGeometryGeneration_{}, videoGeometryIntent_{};
    unsigned videoGeometryAttempts_{};
    struct OpenStart { std::int64_t entry; std::uint64_t generation; };
    // Bounded observations of START_FILE since the replace command submission.
    // More than the bound is conservatively unknown, never guessed explicit.
    std::vector<OpenStart> openStarts_;
    std::uint64_t openOriginRequest_{};
    std::optional<std::int64_t> openOriginEntry_;
    bool CanOpen();
    void MaybeResume();
    void ApplyAdvancedPreferencesOnReady();
    void PrependNextMediaPreferences();
    void RefreshWindowPausePolicy();
    void CancelWindowPausePolicy();
    void SubmitWindowPauseDirective(std::optional<settings::WindowPauseDirective> directive);
    void HandleWindowPauseReply(MpvEvent const& event);
    void HandleFilterEditReply(MpvEvent const& event);
    void CancelFilterEditCallbacks();
    std::map<std::uint64_t, FilterEditCompleted> filterEditCallbacks_;
    enum class ScrubStage { Baseline, Pause, PauseCheck, Seek, ResumeCheck, Resume };
    void HandleScrubEvent(MpvEvent const& event);
    void PumpScrub();
    void FailScrub(std::string error);
    bool ScrubMediaCurrent() const;
    std::uint64_t scrubSerial_{},scrubToken_{},scrubGeneration_{},scrubIntent_{},scrubRequest_{};
    ScrubStage scrubStage_{ScrubStage::Baseline};
    bool scrubEnding_{},scrubPausedReady_{},scrubResumeWanted_{};
    std::optional<double> scrubLatest_,scrubSubmitted_;
    void PausedCommand(std::vector<std::string> command);
    AutoPlaylistExpansionState autoPlaylistExpansion_;
    void InvalidateAutoPlaylistExpansion();
    std::uint64_t playlistEditIntentRevision_{};
    std::uint64_t subtitleEditIntentRevision_{};
    std::uint64_t abLoopEditIntentRevision_{};
    void BeginPlaylistEdit(PlaylistEditPlan plan, std::optional<bool> shuffledAfter = {}, bool automatic = false);
    void AdvancePlaylistEdit(std::span<const PlaylistItem> refreshed);
    bool HandlePlaylistEditEvent(MpvEvent const& event);
    void FailPlaylistEdit(std::string error);
    Dispatch dispatch_;
    Changed changed_;
    std::unique_ptr<MpvClient> client_;
    PlaybackSnapshot snapshot_;
    void SubmitVolumeCommand(std::vector<std::string> arguments);
    bool HandleVolumeMemoryEvent(MpvEvent const& event);
    void PumpVolumeMemoryRead();
    std::vector<std::uint64_t> volumeCommands_;
    std::uint64_t volumeRevision_{}, volumeReadRevision_{}, volumeReadRequest_{};
    bool volumeMemoryDirty_{}, applyingSettingsVolume_{};
    UserSettings settings_;
    bool applySettingsOnReady_{};
    settings::PreferenceStore advancedPreferences_;
    bool hasAdvancedPreferences_{}, applyAdvancedOnReady_{};
    settings::WindowPlaybackPolicy windowPausePolicy_;
    bool windowInactive_{}, windowMinimized_{};
    bool sleepPauseHeld_{}; // explicit Play/new-media intent releases sleep pause
    std::uint64_t windowMediaGeneration_{}, windowPauseRequest_{};
    std::optional<settings::WindowPauseDirective> windowPauseDirective_;
    AutomaticSeekPolicy automaticSeekPolicy_;
    std::uint64_t seekMediaGeneration_{}, seekMeasurementRequest_{};
    bool presentationReady_{};
    std::uint64_t pauseRequest_{}, loadRequest_{};
    std::int64_t expectedEntry_{-1};
    bool resumePending_{};
    bool transitionIssued_{}, waitingForStart_{}, loadReturnsEntry_{};
    std::vector<std::vector<std::string>> pendingCommands_;
    std::unique_ptr<PlaylistPlanCursor> playlistEdit_;
    std::uint64_t playlistEditCommand_{}, playlistEditQuery_{}, playlistEditRevision_{};
    std::optional<bool> playlistShuffleAfter_;
    bool playlistShuffled_{};
    std::uint64_t screenshotRequest_{};
    bool screenshotPreparing_{};
    std::uint64_t screenshotMediaGeneration_{};
    std::string screenshotPath_, screenshotMode_;
    ScreenshotCompleted screenshotCompleted_;
    std::uint64_t watchedPropertiesRequest_{};
    WatchedPropertiesCompleted watchedPropertiesCompleted_;
};
}

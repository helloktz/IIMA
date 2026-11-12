#pragma once
#include "PlaybackState.h"
#include "../Subtitles/SubtitleStyle.h"
#include "../Audio/Equalizations.h"
#include "../Persistence/UserData.h"
#include "../Playlist/PlaylistPlan.h"
#include "../Settings/PreferenceSchema.h"
#include "../Settings/WindowPlaybackPolicy.h"

namespace iima {
class PlayerSession final : public std::enable_shared_from_this<PlayerSession> {
public:
    using Dispatch = std::function<void(std::function<void()>)>;
    using Changed = std::function<void(PlaybackSnapshot const&)>;
    static std::shared_ptr<PlayerSession> Create(Dispatch dispatch, Changed changed);
    void Start(std::wstring dllPath);
    // Only a presenter with a verified owned swapchain lifetime may grant this.
    void SetPresentationReady(bool ready);
    void Open(std::string utf8Path);
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
    void RemovePlaylistItems(std::vector<std::size_t> indices);
    void DropPlaylistItems(std::vector<std::size_t> indices, std::size_t beforeRow);
    void QueuePlaylistItemsNext(std::vector<std::size_t> indices);
    // Cancels only unsent work, never claims rollback of a submitted command.
    void CancelPlaylistEdits();
    bool PlaylistEditBusy() const noexcept { return static_cast<bool>(playlistEdit_); }
    bool PlaylistShuffled() const noexcept { return playlistShuffled_; }
    void SelectTrack(std::string type, std::int64_t id);
    void CycleTrack(std::string type);
    void SelectChapter(std::size_t index);
    void AddSubtitle(std::string path, bool select = true);
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
    bool SubtitleEncodingBusy() const noexcept { return subtitleEncodingRequest_ != 0; }
    void SeekSubtitle(int direction);
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
    void Seek(double seconds);
    void SeekRelative(double seconds, bool exact = true);
    void StepFrame(bool backwards = false);
    void CycleABLoop();
    void ClearABLoop();
    // UI-thread draft guard; returns submission acceptance, not mpv success.
    bool EditABLoopPoint(bool pointB, double seconds,
        std::optional<double> expectedA, std::optional<double> expectedB);
    void Stop();
    void SetVolume(double value);
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
    void SetCompositionSize(unsigned width, unsigned height);
    // Local preferences never overwrite observed playback fields. Call on UI thread.
    void ApplySettings(UserSettings const& settings);
    // Immediate values are submitted now (or at Ready). NextMedia values are
    // submitted before the next explicit Open/Playlist switch, not at START_FILE.
    // This method does not claim that asynchronous commands succeeded.
    void ApplyAdvancedPreferences(settings::PreferenceStore const& preferences);
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
    void HandleSubtitleEncodingReply(MpvEvent const& event);
    std::uint64_t subtitleEncodingRequest_{}, subtitleEncodingGeneration_{};
    std::vector<std::pair<std::int64_t, std::string>> subtitleEncodingTracks_;
    void Notify();
    bool CanOpen();
    void MaybeResume();
    void ApplyAdvancedPreferencesOnReady();
    void PrependNextMediaPreferences();
    void RefreshWindowPausePolicy();
    void CancelWindowPausePolicy();
    void SubmitWindowPauseDirective(std::optional<settings::WindowPauseDirective> directive);
    void HandleWindowPauseReply(MpvEvent const& event);
    void PausedCommand(std::vector<std::string> command);
    void BeginPlaylistEdit(PlaylistEditPlan plan, std::optional<bool> shuffledAfter = {});
    void AdvancePlaylistEdit(std::span<const PlaylistItem> refreshed);
    bool HandlePlaylistEditEvent(MpvEvent const& event);
    void FailPlaylistEdit(std::string error);
    Dispatch dispatch_;
    Changed changed_;
    std::unique_ptr<MpvClient> client_;
    PlaybackSnapshot snapshot_;
    UserSettings settings_;
    bool applySettingsOnReady_{};
    settings::PreferenceStore advancedPreferences_;
    bool hasAdvancedPreferences_{}, applyAdvancedOnReady_{};
    settings::WindowPlaybackPolicy windowPausePolicy_;
    bool windowInactive_{}, windowMinimized_{};
    bool sleepPauseHeld_{}; // explicit Play/new-media intent releases sleep pause
    std::uint64_t windowMediaGeneration_{}, windowPauseRequest_{};
    std::optional<settings::WindowPauseDirective> windowPauseDirective_;
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
    ScreenshotCompleted screenshotCompleted_;
    std::uint64_t watchedPropertiesRequest_{};
    WatchedPropertiesCompleted watchedPropertiesCompleted_;
};
}

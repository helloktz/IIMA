#pragma once
#include "PlaybackState.h"
#include "../Subtitles/SubtitleStyle.h"
#include "../Audio/Equalizations.h"
#include "../Persistence/UserData.h"
#include "../Playlist/PlaylistPlan.h"

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
    void SelectChapter(std::size_t index);
    void AddSubtitle(std::string path);
    void ApplySubtitleStyle(SubtitleStyle const& style);
    void SetSubtitleScale(double scale, bool primary = true);
    void SetSubtitlePosition(int percent, bool primary = true);
    void SetSecondarySubtitleDelay(double seconds);
    void SelectSecondarySubtitle(std::int64_t id);
    void SetSecondarySubtitlesVisible(bool visible);
    void ReloadSubtitles();
    void SeekSubtitle(int direction);
    void ScreenshotToFile(std::string path);
    void Pause(bool paused);
    void TogglePause();
    void Seek(double seconds);
    void SeekRelative(double seconds, bool exact = true);
    void StepFrame(bool backwards = false);
    void CycleABLoop();
    void ClearABLoop();
    void Stop();
    void SetVolume(double value);
    void SetSpeed(double value);
    void SetVideoAspect(std::string aspect);
    void SetVideoRotation(int degrees);
    void SetDeinterlace(bool enabled);
    void SetVideoEqualizer(std::string property, int value);
    void AddVideoFilter(std::string filter);
    void RemoveVideoFilter(std::string label);
    void SetCompositionSize(unsigned width, unsigned height);
    // Local preferences never overwrite observed playback fields. Call on UI thread.
    void ApplySettings(UserSettings const& settings);
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
    void Notify();
    bool CanOpen();
    void MaybeResume();
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
};
}

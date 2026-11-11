#pragma once
#include "PlaybackState.h"

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
    void SelectTrack(std::string type, std::int64_t id);
    void SelectChapter(std::size_t index);
    void AddSubtitle(std::string path);
    void ScreenshotToFile(std::string path);
    void Pause(bool paused);
    void TogglePause();
    void Seek(double seconds);
    void Stop();
    void SetVolume(double value);
    void SetSpeed(double value);
    void SetCompositionSize(unsigned width, unsigned height);
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
    Dispatch dispatch_;
    Changed changed_;
    std::unique_ptr<MpvClient> client_;
    PlaybackSnapshot snapshot_;
    bool presentationReady_{};
    std::uint64_t pauseRequest_{}, loadRequest_{};
    std::int64_t expectedEntry_{-1};
    bool resumePending_{};
    std::vector<std::vector<std::string>> pendingCommands_;
};
}

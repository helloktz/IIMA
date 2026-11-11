#pragma once
#include "../../IIMA.Core/Playback/PlaybackState.h"
#include <Windows.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <functional>
#include <memory>

namespace iima {
enum class SystemMediaCommand { Play, Pause, Previous, Next };
// Windows adaptation of IINA NowPlayingInfoManager.updateInfo,
// AppDelegate.swift RemoteCommandController, and SleepPreventer.updateSleepPrevention.
// Upstream: references/iina-source.txt; upstream copyright/license remain applicable.
class MediaIntegration final {
public:
    // Construct, update and close on the owning UI thread. Native callbacks are
    // dispatched there; they never invoke a session from the SMTC worker thread.
    MediaIntegration(HWND window, winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
                     std::function<void(SystemMediaCommand)> command);
    ~MediaIntegration();
    MediaIntegration(MediaIntegration const&) = delete;
    MediaIntegration& operator=(MediaIntegration const&) = delete;
    void Update(PlaybackSnapshot const& state) noexcept;
    void Close() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}

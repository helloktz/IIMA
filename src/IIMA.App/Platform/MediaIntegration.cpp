// Windows adaptation of IINA NowPlayingInfoManager and RemoteCommandController.
// Copyright (c) 2016-2025 lhc and IINA contributors. GPL-3.0, references/iina/LICENSE.
#include "pch.h"
#include "MediaIntegration.h"
#include "../../IIMA.Core/Media/SystemMediaInfo.h"
#include "../../IIMA.Core/Windows/MusicModePolicy.h"
#include "../../IIMA.Core/Network/MediaUrl.h"
#include <chrono>
#include <SystemMediaTransportControlsInterop.h>
#include <winrt/Windows.Media.h>
#include <atomic>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdlib>

namespace iima {
namespace {
using namespace winrt::Windows::Media;
void Report(wchar_t const* message) noexcept { OutputDebugStringW(message); }

}
struct MediaIntegration::State {
    SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttonToken{}, positionToken{};
    bool subscribed{}, positionSubscribed{};
    std::atomic<bool> alive{true}, transportReady{false};
    std::function<void(SystemMediaCommand)> command;
    std::function<void(double)> seek;
    // UI state is accessed only on the dispatcher. Epoch is the sole field
    // sampled by an SMTC worker callback and compared again after dispatch.
    std::atomic<std::uint64_t> epoch{1};
    bool active{}, canSeek{};
    std::int64_t entryId{-1};
    std::string path;
    double duration{};
    std::optional<SystemMediaInfo> previousInfo;
    HANDLE power{INVALID_HANDLE_VALUE};
    bool systemHeld{}, displayHeld{};
    winrt::hstring title, album, artist;
    MediaPlaybackType type{MediaPlaybackType::Unknown};
    bool metadataPublished{};
    std::optional<std::uint64_t> playlistRevision;
    bool previousAvailable{}, nextAvailable{};

    bool desiredSystem{}, desiredDisplay{};
    std::chrono::steady_clock::time_point nextPowerAttempt{};
    void ReleasePower() noexcept {
        if (power != INVALID_HANDLE_VALUE) {
            if (displayHeld) PowerClearRequest(power, PowerRequestDisplayRequired);
            if (systemHeld) PowerClearRequest(power, PowerRequestSystemRequired);
            // Handle closure releases even a failed explicit clear. In particular,
            // changing to audio-only mode must never leave displayHeld behind.
            CloseHandle(power);
            power = INVALID_HANDLE_VALUE;
        }
        displayHeld = systemHeld = false;
    }
    void SetPower(bool system, bool display) noexcept {
        display = system && display;
        if (system != desiredSystem || display != desiredDisplay) {
            ReleasePower();
            desiredSystem = system; desiredDisplay = display;
            nextPowerAttempt = {};
        }
        if (!system) { ReleasePower(); return; }
        if (systemHeld && (!display || displayHeld)) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < nextPowerAttempt) return;
        nextPowerAttempt = now + std::chrono::seconds(1);
        if (power == INVALID_HANDLE_VALUE) {
            REASON_CONTEXT reason{};
            reason.Version = POWER_REQUEST_CONTEXT_VERSION;
            reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
            wchar_t description[] = L"IIMA playback is in progress";
            reason.Reason.SimpleReasonString = description;
            power = PowerCreateRequest(&reason);
            if (power == INVALID_HANDLE_VALUE) { Report(L"IIMA: PowerCreateRequest failed.\n"); return; }
        }
        // Request reference counts are incremented only for an unheld type.
        // Retry partial failure at most once/second as playback updates arrive.
        if (!systemHeld) systemHeld = PowerSetRequest(power, PowerRequestSystemRequired) != FALSE;
        if (display && !displayHeld) displayHeld = PowerSetRequest(power, PowerRequestDisplayRequired) != FALSE;
        if (!systemHeld || (display && !displayHeld))
            Report(L"IIMA: playback sleep-prevention request was not fully accepted.\n");
    }
    // SMTC and sleep prevention are independent services in IINA. A partially
    // registered transport must be retired without marking the power owner dead.
    void ReleaseTransport() noexcept {
        transportReady = false;
        ++epoch;
        active = canSeek = false;
        if (controls) {
            // Revoke independently: failure of one cleanup must not leave the
            // other handler retaining a dispatcher or receiving new events.
            if (subscribed) {
                try { controls.ButtonPressed(buttonToken); } catch (...) {}
                subscribed = false;
            }
            if (positionSubscribed) {
                try { controls.PlaybackPositionChangeRequested(positionToken); } catch (...) {}
                positionSubscribed = false;
            }
            // A failed setter must not suppress the remaining best-effort cleanup.
            try { controls.IsEnabled(false); } catch (...) {}
            try { controls.PlaybackStatus(MediaPlaybackStatus::Closed); } catch (...) {}
            try { controls.UpdateTimelineProperties(SystemMediaTransportControlsTimelineProperties{}); } catch (...) {}
            try { controls.DisplayUpdater().ClearAll(); controls.DisplayUpdater().Update(); } catch (...) {}
        }
        controls = nullptr;
    }
    void Close() noexcept {
        if (!alive.exchange(false)) return;
        SetPower(false, false);
        ReleaseTransport();
        command = {}; seek = {};
    }
    ~State() { Close(); }
};

MediaIntegration::MediaIntegration(HWND window,
    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
    std::function<void(SystemMediaCommand)> command,
    std::function<void(double)> seek) : state_(std::make_shared<State>()) {
    if (!window || !dispatcher || !dispatcher.HasThreadAccess())
        throw winrt::hresult_invalid_argument(L"SMTC requires its owning window UI thread");
    state_->command = std::move(command);
    state_->seek = std::move(seek);
    try {
    auto interop = winrt::get_activation_factory<SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
    winrt::check_hresult(interop->GetForWindow(window, winrt::guid_of<SystemMediaTransportControls>(),
                                              winrt::put_abi(state_->controls)));
    state_->controls.IsEnabled(false);
    state_->controls.IsPlayEnabled(false);
    state_->controls.IsPauseEnabled(false);
    state_->controls.IsPreviousEnabled(false);
    state_->controls.IsNextEnabled(false);
    std::weak_ptr<State> weak = state_;
    state_->buttonToken = state_->controls.ButtonPressed([weak, dispatcher](auto const&, SystemMediaTransportControlsButtonPressedEventArgs const& args) {
        try {
            auto state = weak.lock();
            if (!state || !state->alive.load() || !state->transportReady.load()) return;
            std::optional<SystemMediaCommand> command;
            switch (args.Button()) {
            case SystemMediaTransportControlsButton::Play: command = SystemMediaCommand::Play; break;
            case SystemMediaTransportControlsButton::Pause: command = SystemMediaCommand::Pause; break;
            case SystemMediaTransportControlsButton::Previous: command = SystemMediaCommand::Previous; break;
            case SystemMediaTransportControlsButton::Next: command = SystemMediaCommand::Next; break;
            default: return;
            }
            // A failed enqueue drops the command. Never block shutdown or retain the window.
            const auto epoch = state->epoch.load();
            dispatcher.TryEnqueue([weak, requested = *command, epoch] {
                auto current = weak.lock();
                try {
                    if (current && current->alive.load() && current->transportReady.load() && current->active &&
                        current->epoch.load() == epoch && current->command) current->command(requested);
                } catch (...) { Report(L"IIMA: SMTC command failed.\n"); }
            });
        } catch (...) { Report(L"IIMA: SMTC command delivery failed.\n"); }
    });
    state_->subscribed = true;
    state_->positionToken = state_->controls.PlaybackPositionChangeRequested(
        [weak, dispatcher](auto const&, PlaybackPositionChangeRequestedEventArgs const& args) {
            try {
                auto state = weak.lock();
                if (!state || !state->alive.load() || !state->transportReady.load()) return;
                const auto ticks = args.RequestedPlaybackPosition().count();
                if (ticks < 0) return;
                const double seconds = static_cast<double>(ticks) / 10000000.0;
                const auto epoch = state->epoch.load();
                dispatcher.TryEnqueue([weak, seconds, epoch] {
                    auto current = weak.lock();
                    if (!current || !current->alive.load() || !current->transportReady.load() || !current->active ||
                        !current->canSeek || current->epoch.load() != epoch || !current->seek) return;
                    try { current->seek(std::clamp(seconds, 0.0, current->duration)); }
                    catch (...) { Report(L"IIMA: SMTC seek failed.\n"); }
                });
            } catch (...) { Report(L"IIMA: SMTC seek delivery failed.\n"); }
        });
    state_->positionSubscribed = true;
    state_->transportReady = true;
    } catch (...) {
        // Invalidate queued callbacks before revoking subscriptions. They may
        // already have reached the UI queue even when initialization failed.
        state_->ReleaseTransport();
        Report(L"IIMA: system media controls unavailable; power policy remains active.\n");
    }
}

MediaIntegration::~MediaIntegration() { Close(); }
void MediaIntegration::Close() noexcept { if (state_) state_->Close(); }
bool MediaIntegration::TransportAvailable() const noexcept {
    return state_ && state_->alive.load() && state_->transportReady.load();
}

void MediaIntegration::Update(PlaybackSnapshot const& snapshot, bool preventSleep,
    bool allowDisplaySleepForAudio, bool musicMode) noexcept {
    if (!state_ || !state_->alive.load()) return;
    const bool active = snapshot.session == SessionState::Ready && snapshot.media == MediaState::Loaded;
    const bool playing = active && !snapshot.paused;
    // Set even if Windows metadata publication fails: queued commands must not
    // act against a stale item while a different item is opening or closing.
    if (active != state_->active || snapshot.entryId != state_->entryId || snapshot.path != state_->path) {
        ++state_->epoch;
        state_->active = active;
        state_->entryId = snapshot.entryId;
        try { state_->path = snapshot.path; } catch (...) { state_->path.clear(); }
        state_->previousInfo.reset();
    }
    state_->canSeek = false;
    // Match PlaybackInfo.isAudio: unknown tracks and network resources are not
    // guessed to be audio. Explicit mini/music mode is the other upstream case.
    const auto kind = ClassifyMusicTracks(snapshot.tracks, !snapshot.tracks.empty(),
        IsNetworkMediaPath(snapshot.path)).kind;
    const bool allowDisplay = allowDisplaySleepForAudio && (musicMode || kind == MusicMediaKind::Audio);
    state_->SetPower(playing && preventSleep, playing && preventSleep && !allowDisplay);
    // Never dereference a null projected WinRT object after an activation or
    // subscription failure. Power policy above remains evaluated on every update.
    if (!state_->controls) return;
    try {
        auto info = MakeSystemMediaInfo(snapshot);
        auto controls = state_->controls;
        controls.IsEnabled(active);
        controls.IsPlayEnabled(active && snapshot.paused);
        controls.IsPauseEnabled(playing);
        if (state_->playlistRevision != snapshot.playlistRevision) {
            state_->playlistRevision = snapshot.playlistRevision;
            auto current = std::find_if(snapshot.playlist.begin(), snapshot.playlist.end(),
                [](auto const& item) { return item.current; });
            state_->previousAvailable = current != snapshot.playlist.end() && current != snapshot.playlist.begin();
            state_->nextAvailable = current != snapshot.playlist.end() && std::next(current) != snapshot.playlist.end();
        }
        controls.IsPreviousEnabled(active && state_->previousAvailable);
        controls.IsNextEnabled(active && state_->nextAvailable);
        controls.PlaybackStatus(!active ? MediaPlaybackStatus::Closed :
            snapshot.paused ? MediaPlaybackStatus::Paused : MediaPlaybackStatus::Playing);
        // Windows represents pause via PlaybackStatus, not a zero PlaybackRate.
        controls.PlaybackRate(info.rate);
        state_->duration = static_cast<double>(info.durationTicks) / 10000000.0;
        state_->canSeek = info.canSeek && static_cast<bool>(state_->seek);
        const auto& previous = state_->previousInfo;
        bool updateTimeline = !previous || previous->active != info.active ||
            previous->durationTicks != info.durationTicks || previous->canSeek != info.canSeek ||
            previous->paused != info.paused || previous->rate != info.rate;
        // Coalesce high frequency position notifications, but publish seeks and
        // every paused position exactly. Never advance the UI by wall clock.
        if (previous && (info.paused || std::abs(info.positionTicks - previous->positionTicks) >= 2500000))
            updateTimeline = updateTimeline || info.positionTicks != previous->positionTicks;
        if (updateTimeline) {
            SystemMediaTransportControlsTimelineProperties timeline;
            using winrt::Windows::Foundation::TimeSpan;
            timeline.StartTime(TimeSpan{0});
            timeline.EndTime(TimeSpan{info.durationTicks});
            timeline.Position(TimeSpan{info.positionTicks});
            timeline.MinSeekTime(TimeSpan{state_->canSeek ? 0 : info.positionTicks});
            timeline.MaxSeekTime(TimeSpan{state_->canSeek ? info.durationTicks : info.positionTicks});
            controls.UpdateTimelineProperties(timeline);
            state_->previousInfo = info;
        }
        auto updater = controls.DisplayUpdater();
        if (!active) {
            if (state_->metadataPublished) { updater.ClearAll(); updater.Update(); }
            state_->metadataPublished = false;
            return;
        }
        const auto title = winrt::to_hstring(info.title);
        const auto album = winrt::to_hstring(info.album);
        const auto artist = winrt::to_hstring(info.artist);
        const auto type = info.video ? MediaPlaybackType::Video : MediaPlaybackType::Music;
        if (!state_->metadataPublished || title != state_->title || album != state_->album ||
            artist != state_->artist || type != state_->type) {
            updater.ClearAll();
            updater.Type(type);
            if (info.video) updater.VideoProperties().Title(title);
            else {
                updater.MusicProperties().Title(title);
                updater.MusicProperties().AlbumTitle(album);
                updater.MusicProperties().Artist(artist);
            }
            updater.Update();
            state_->title = title; state_->album = album; state_->artist = artist;
            state_->type = type;
            state_->metadataPublished = true;
        }
    } catch (...) { Report(L"IIMA: SMTC state update failed.\n"); }
}
}

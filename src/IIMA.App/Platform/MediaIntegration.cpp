#include "pch.h"
#include "MediaIntegration.h"
#include <SystemMediaTransportControlsInterop.h>
#include <winrt/Windows.Media.h>
#include <atomic>
#include <algorithm>
#include <iterator>

namespace iima {
namespace {
using namespace winrt::Windows::Media;
void Report(wchar_t const* message) noexcept { OutputDebugStringW(message); }
winrt::hstring SafeTitle(PlaybackSnapshot const& snapshot) {
    // Local playback only. Publish a filename, never a full filesystem path or URL.
    auto title = snapshot.path;
    const bool isUrl = title.find("://") != std::string::npos;
    auto slash = title.find_last_of("/\\");
    if (slash != std::string::npos) title.erase(0, slash + 1);
    if (isUrl) {
        if (auto query = title.find_first_of("?#"); query != std::string::npos) title.resize(query);
    }
    return title.empty() ? winrt::hstring{L"IIMA"} : winrt::to_hstring(title);
}
}
struct MediaIntegration::State {
    SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttonToken{};
    bool subscribed{};
    std::atomic<bool> alive{true};
    std::function<void(SystemMediaCommand)> command;
    HANDLE power{INVALID_HANDLE_VALUE};
    bool systemHeld{}, displayHeld{};
    winrt::hstring title;
    MediaPlaybackType type{MediaPlaybackType::Unknown};
    bool metadataPublished{};
    std::optional<std::uint64_t> playlistRevision;
    bool previousAvailable{}, nextAvailable{};

    void SetPower(bool playing) noexcept {
        if (!playing) {
            if (power != INVALID_HANDLE_VALUE) {
                if (displayHeld) PowerClearRequest(power, PowerRequestDisplayRequired);
                if (systemHeld) PowerClearRequest(power, PowerRequestSystemRequired);
                // Closing the request handle releases all outstanding requests,
                // even if an explicit clear failed. No request survives a pause.
                CloseHandle(power);
                power = INVALID_HANDLE_VALUE;
            }
            displayHeld = systemHeld = false;
            return;
        }
        if (power == INVALID_HANDLE_VALUE) {
            REASON_CONTEXT reason{};
            reason.Version = POWER_REQUEST_CONTEXT_VERSION;
            reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
            wchar_t description[] = L"IIMA playback is in progress";
            reason.Reason.SimpleReasonString = description;
            power = PowerCreateRequest(&reason);
            if (power == INVALID_HANDLE_VALUE) { Report(L"IIMA: PowerCreateRequest failed.\n"); return; }
        }
        // Power request APIs are handle-scoped, unlike SetThreadExecutionState.
        // Each request type is set at most once and cleared exactly once.
        if (!systemHeld) systemHeld = PowerSetRequest(power, PowerRequestSystemRequired) != FALSE;
        if (!displayHeld) displayHeld = PowerSetRequest(power, PowerRequestDisplayRequired) != FALSE;
        if (!systemHeld || !displayHeld) Report(L"IIMA: playback sleep-prevention request was not fully accepted.\n");
    }
    void Close() noexcept {
        if (!alive.exchange(false)) return;
        SetPower(false);
        try {
            if (controls) {
                if (subscribed) { controls.ButtonPressed(buttonToken); subscribed = false; }
                controls.IsEnabled(false);
                controls.PlaybackStatus(MediaPlaybackStatus::Closed);
                controls.DisplayUpdater().ClearAll();
                controls.DisplayUpdater().Update();
            }
        } catch (...) { Report(L"IIMA: SMTC cleanup failed.\n"); }
        controls = nullptr;
    }
    ~State() { Close(); }
};

MediaIntegration::MediaIntegration(HWND window,
    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
    std::function<void(SystemMediaCommand)> command) : state_(std::make_shared<State>()) {
    if (!window || !dispatcher || !dispatcher.HasThreadAccess())
        throw winrt::hresult_invalid_argument(L"SMTC requires its owning window UI thread");
    state_->command = std::move(command);
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
            if (!state || !state->alive.load()) return;
            std::optional<SystemMediaCommand> command;
            switch (args.Button()) {
            case SystemMediaTransportControlsButton::Play: command = SystemMediaCommand::Play; break;
            case SystemMediaTransportControlsButton::Pause: command = SystemMediaCommand::Pause; break;
            case SystemMediaTransportControlsButton::Previous: command = SystemMediaCommand::Previous; break;
            case SystemMediaTransportControlsButton::Next: command = SystemMediaCommand::Next; break;
            default: return;
            }
            // A failed enqueue drops the command. Never block shutdown or retain the window.
            dispatcher.TryEnqueue([weak, requested = *command] {
                auto current = weak.lock();
                try {
                    if (current && current->alive.load() && current->command) current->command(requested);
                } catch (...) { Report(L"IIMA: SMTC command failed.\n"); }
            });
        } catch (...) { Report(L"IIMA: SMTC command delivery failed.\n"); }
    });
    state_->subscribed = true;
}

MediaIntegration::~MediaIntegration() { Close(); }
void MediaIntegration::Close() noexcept { if (state_) state_->Close(); }

void MediaIntegration::Update(PlaybackSnapshot const& snapshot) noexcept {
    if (!state_ || !state_->alive.load()) return;
    const bool active = snapshot.session == SessionState::Ready && snapshot.media == MediaState::Loaded;
    const bool playing = active && !snapshot.paused;
    // IINA defaults preventScreenSaver=true, allowScreenSaverForAudio=false:
    // both audio and video prevent system/display idle while actively playing.
    state_->SetPower(playing);
    try {
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
        auto updater = controls.DisplayUpdater();
        if (!active) {
            if (state_->metadataPublished) { updater.ClearAll(); updater.Update(); }
            state_->metadataPublished = false;
            return;
        }
        const auto title = SafeTitle(snapshot);
        const bool hasVideo = std::any_of(snapshot.tracks.begin(), snapshot.tracks.end(),
            [](auto const& track) { return track.type == "video" && track.selected; });
        const auto type = hasVideo ? MediaPlaybackType::Video : MediaPlaybackType::Music;
        if (!state_->metadataPublished || title != state_->title || type != state_->type) {
            updater.ClearAll();
            updater.Type(type);
            if (hasVideo) updater.VideoProperties().Title(title);
            else updater.MusicProperties().Title(title);
            updater.Update();
            state_->title = title;
            state_->type = type;
            state_->metadataPublished = true;
        }
    } catch (...) { Report(L"IIMA: SMTC state update failed.\n"); }
}
}

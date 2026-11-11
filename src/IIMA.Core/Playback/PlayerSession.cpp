// Adapted from IINA PlayerCore.swift / MPVController.swift / PlaybackInfo.swift.
// Copyright © 2016 lhc. All rights reserved. IINA's GPL-3.0 license applies
// to the adapted logic; see references/iina/LICENSE and docs/PORTING.md.
#include "PlayerSession.h"
#include <algorithm>
#include <cmath>

namespace iima {
PlayerSession::PlayerSession(Dispatch dispatch, Changed changed)
    : dispatch_(std::move(dispatch)), changed_(std::move(changed)) {}
std::shared_ptr<PlayerSession> PlayerSession::Create(Dispatch dispatch, Changed changed) {
    auto session=std::shared_ptr<PlayerSession>(new PlayerSession(std::move(dispatch),std::move(changed)));
    std::weak_ptr<PlayerSession> weak=session;
    session->client_=std::make_unique<MpvClient>([weak, dispatch=session->dispatch_](MpvEvent event) {
        dispatch([weak,event=std::move(event)]() mutable { if(auto self=weak.lock()) self->OnEvent(std::move(event)); });
    });
    return session;
}
void PlayerSession::Start(std::wstring path) { client_->Start(std::move(path)); }
void PlayerSession::Notify() { if(changed_) changed_(snapshot_); }
void PlayerSession::Open(std::string path) {
    if(snapshot_.session!=SessionState::Ready) return;
    if(path.empty() || path.find('\0')!=std::string::npos) {
        snapshot_.error="The media path is empty or contains a NUL character"; Notify(); return;
    }
    if(!presentationReady_) {
        snapshot_.error="Video output is unavailable: the pinned libmpv display-swapchain property exposes a borrowed pointer without a safe acquisition contract. Playback is blocked until the presenter can bind an owned swapchain.";
        Notify(); return;
    }
    // IINA openMainWindow pauses before loadfile. Wait for the asynchronous pause
    // reply before loading, rather than relying on unrelated async command order.
    pendingPath_=std::move(path);
    pauseRequest_=client_->Command({"set","pause","yes"});
    if(!pauseRequest_) { pendingPath_.clear(); snapshot_.error="Player is closing"; Notify(); }
}
void PlayerSession::Pause(bool value) {
    if(snapshot_.session==SessionState::Ready) client_->Command({"set","pause",value ? "yes" : "no"});
}
void PlayerSession::TogglePause() { Pause(!snapshot_.paused); }
void PlayerSession::Seek(double seconds) {
    if(snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
       !snapshot_.duration || *snapshot_.duration<=0 || !std::isfinite(seconds)) return;
    client_->Command({"seek",std::to_string(std::clamp(seconds,0.0,*snapshot_.duration)),"absolute+exact"});
}
void PlayerSession::Stop() {
    pendingPath_.clear(); pauseRequest_=0;
    if(snapshot_.session==SessionState::Ready) client_->Command({"stop"});
}
void PlayerSession::SetVolume(double value) {
    if(snapshot_.session==SessionState::Ready && std::isfinite(value))
        client_->Command({"set","volume",std::to_string(std::clamp(value,0.0,100.0))});
}
void PlayerSession::SetSpeed(double value) {
    if(snapshot_.session==SessionState::Ready && std::isfinite(value) && value>=0.01 && value<=100)
        client_->Command({"set","speed",std::to_string(value)});
}
void PlayerSession::SetCompositionSize(unsigned width,unsigned height) {
    if(snapshot_.session==SessionState::Ready && width && height && width<=16384 && height<=16384)
        client_->Command({"set","d3d11-composition-size",std::to_string(width)+"x"+std::to_string(height)});
}
void PlayerSession::OnEvent(MpvEvent e) {
    if(snapshot_.session==SessionState::Closing || snapshot_.session==SessionState::Closed) return;
    if(e.kind==MpvEvent::Kind::CommandReply && e.requestId==pauseRequest_) {
        pauseRequest_=0;
        if(e.error>=0 && !pendingPath_.empty()) {
            client_->Command({"loadfile",std::move(pendingPath_),"replace"});
        }
        pendingPath_.clear();
    }
    ApplyPlaybackEvent(snapshot_,e);
    if(e.kind==MpvEvent::Kind::Loaded && presentationReady_ && !pauseRequest_)
        Pause(false);
    Notify();
    if(e.kind==MpvEvent::Kind::Shutdown) CloseAsync();
}
void PlayerSession::CloseAsync(std::function<void()> completion) {
    if(snapshot_.session!=SessionState::Closed) snapshot_.session=SessionState::Closing;
    pendingPath_.clear(); pauseRequest_=0; presentationReady_=false; Notify();
    auto weak=weak_from_this();
    client_->CloseAsync([weak, dispatch=dispatch_, completion=std::move(completion)] {
        dispatch([weak,completion] {
            if(auto self=weak.lock()) { self->snapshot_.session=SessionState::Closed; self->Notify(); }
            if(completion) completion();
        });
    });
}
}

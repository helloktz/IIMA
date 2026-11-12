// IINA PlayerCore.audioDeviceListChanged/setAudioDevice and MPVController.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "PlayerSession.h"
#include <algorithm>

namespace iima {
void PlayerSession::SubmitAudioDevicePreference(std::string device, bool protectSelection) {
    // An explicit same-value selection is still intent, even if the persisted
    // preference did not change after an automatic, non-persistent fallback.
    ++audioDeviceIntent_;
    if (protectSelection) audioDeviceRecoveryListRevision_=snapshot_.audioDevicesRevision;
    pendingAudioDeviceProtection_=protectSelection;
    pendingAudioDevice_=std::move(device);
    PumpAudioDevicePreference();
}
void PlayerSession::PumpAudioDevicePreference() {
    if (audioDeviceWrite_ || !pendingAudioDevice_ || snapshot_.session!=SessionState::Ready) return;
    auto device=std::move(*pendingAudioDevice_);
    pendingAudioDevice_.reset();
    audioDeviceWriteProtection_=pendingAudioDeviceProtection_;
    audioDeviceWrite_=client_->Command({"set","audio-device",std::move(device)});
    if (!audioDeviceWrite_) {
        snapshot_.error="Audio device selection could not be queued";
        Notify();
    }
}
void PlayerSession::UpdateAudioDeviceRecovery(MpvEvent const& event) {
    if (event.kind==MpvEvent::Kind::Failure || event.kind==MpvEvent::Kind::Shutdown) {
        pendingAudioDevice_.reset(); audioDeviceWrite_=0; audioDeviceRecovery_=0;
        return;
    }
    if (event.kind==MpvEvent::Kind::CommandReply && audioDeviceWrite_ && event.requestId==audioDeviceWrite_) {
        audioDeviceWrite_=0;
        // List changes delivered during a user selection are not a license to
        // override that selection. Wait for a later list-content/validity change.
        // Startup restoration is not a concurrent user choice: a persisted
        // device already absent at startup can recover from the initial list.
        if (audioDeviceWriteProtection_) audioDeviceRecoveryListRevision_=snapshot_.audioDevicesRevision;
        PumpAudioDevicePreference();
        return;
    }
    if (event.kind==MpvEvent::Kind::CommandReply && audioDeviceRecovery_ && event.requestId==audioDeviceRecovery_) {
        audioDeviceRecovery_=0;
        // Success/skip only means the worker completed its operation. The
        // observed audio-device property remains the sole selection truth.
        // Consume changes during recovery too; failure cannot create a loop.
        audioDeviceRecoveryListRevision_=snapshot_.audioDevicesRevision;
        return;
    }
    if (snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded ||
        audioDeviceWrite_ || pendingAudioDevice_ || audioDeviceRecovery_ ||
        !snapshot_.audioDevicesObserved || snapshot_.audioDevices.empty() ||
        !snapshot_.audioDeviceObserved || snapshot_.audioDevice.empty() || snapshot_.audioDevice=="auto" ||
        snapshot_.audioDevicesRevision==audioDeviceRecoveryListRevision_) return;
    audioDeviceRecoveryListRevision_=snapshot_.audioDevicesRevision;
    if (std::any_of(snapshot_.audioDevices.begin(),snapshot_.audioDevices.end(),[&](auto const& device) {
        return device.name==snapshot_.audioDevice;
    })) return;
    audioDeviceRecoveryIntent_=audioDeviceIntent_;
    audioDeviceRecovery_=client_->RecoverAudioDeviceAsync(snapshot_.audioDevice);
    if (!audioDeviceRecovery_) {
        snapshot_.error="The selected audio device disappeared, but automatic fallback could not be queued";
        // Caller publishes this snapshot once. No timer or per-frame retry.
    }
}
}

// IINA PlayerCore.setAudioEq and removeAudioEq; Copyright © 2016 lhc.
// GPL v3; references/iina/LICENSE.
#include "PlayerSession.h"
namespace iima {
void PlayerSession::SetAudioEqualizer(EQProfile const& profile) {
    if (snapshot_.session != SessionState::Ready) return;
    auto command = Equalizations::AddFilterCommand(profile);
    if (!command) {
        snapshot_.error = "Invalid audio equalizer gains";
        Notify();
        return;
    }
    // Same label replaces previous EQ; other audio filters remain intact.
    client_->Command(std::move(*command));
}
void PlayerSession::RemoveAudioEqualizer() {
    if (snapshot_.session == SessionState::Ready)
        client_->Command({"af", "remove", "@" + std::string(Equalizations::FilterLabel)});
}
}

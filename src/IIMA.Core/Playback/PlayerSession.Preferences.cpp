// Preference initialization and setUserOption mapping adapted from IINA
// MPVController.swift and Preference.swift. Copyright © lhc and contributors.
// GPL-3.0; see references/iina/LICENSE.
#include "PlayerSession.h"
#include <algorithm>
#include <iterator>

namespace iima {
void PlayerSession::ApplyAdvancedPreferences(settings::PreferenceStore const& preferences, bool explicitAudioDevice) {
    // The store can only contain validated values. Capture desired preferences,
    // not a claim about what libmpv or the current decoder is actually using.
    const bool first=!hasAdvancedPreferences_;
    auto changes=first ? settings::MpvPreferenceOptions(preferences)
        : settings::ChangedMpvPreferences(advancedPreferences_,preferences);
    advancedPreferences_=preferences;
    hasAdvancedPreferences_=true;
    if(snapshot_.session!=SessionState::Ready) {
        startupAudioDeviceProtection_=startupAudioDeviceProtection_ || explicitAudioDevice;
        applyAdvancedOnReady_=snapshot_.session==SessionState::Initializing;
        return;
    }
    if (explicitAudioDevice) SubmitAudioDevicePreference(preferences.String("audioDevice"));
    for(auto const& change:changes) {
        if(change.timing!=settings::ApplyTiming::Immediate) continue;
        if (change.property=="audio-device") {
            if (!explicitAudioDevice) SubmitAudioDevicePreference(change.value, !first);
            continue;
        }
        if(!client_->Command({"set",change.property,change.value})) {
            snapshot_.error="Player is closing; some advanced preferences were not submitted";
            Notify();
            break;
        }
    }
    RefreshWindowPausePolicy();
    // NextMedia values remain in advancedPreferences_. Do not set them here:
    // e.g. changing audio language must not switch the still-playing old media.
}
void PlayerSession::ApplyAdvancedPreferencesOnReady() {
    if(!applyAdvancedOnReady_ || !hasAdvancedPreferences_) return;
    applyAdvancedOnReady_=false;
    const bool protectAudioDevice=startupAudioDeviceProtection_;
    startupAudioDeviceProtection_=false;
    for(auto const& option:settings::MpvPreferenceOptions(advancedPreferences_)) {
        if(option.timing==settings::ApplyTiming::Immediate) {
            if (option.property=="audio-device") SubmitAudioDevicePreference(option.value, protectAudioDevice);
            else client_->Command({"set",option.property,option.value});
        }
    }
}
void PlayerSession::PrependNextMediaPreferences() {
    if(!hasAdvancedPreferences_) return;
    std::vector<std::vector<std::string>> commands;
    for(auto const& option:settings::MpvPreferenceOptions(advancedPreferences_)) {
        if(option.timing==settings::ApplyTiming::NextMedia)
            commands.push_back({"set",option.property,option.value});
    }
    // Snapshot desired settings at the explicit open request, not at START_FILE.
    // These setters are submitted after its pause reply and before load/switch
    // in the worker's serialized lane. Automatic EOF advance keeps the last
    // submitted settings; pending edits wait for an explicit open/switch.
    commands.insert(commands.end(),std::make_move_iterator(pendingCommands_.begin()),
                    std::make_move_iterator(pendingCommands_.end()));
    pendingCommands_=std::move(commands);
}
}

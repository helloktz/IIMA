// FilterWindowController and MPVController filter editing, adapted for async UI.
// Copyright (c) 2016 lhc. SPDX-License-Identifier: GPL-3.0-or-later
#include "PlayerSession.h"
namespace iima {
void PlayerSession::AddFilter(bool audio, std::string expression) {
    if (snapshot_.session != SessionState::Ready) return;
    auto parsed = MPVFilter::Parse(expression);
    if (!parsed || expression.size() > 65536 || expression.find('\0') != std::string::npos) {
        snapshot_.error = "Invalid filter expression"; Notify(); return;
    }
    client_->Command({audio ? "af" : "vf", "add", parsed->StringFormat()});
}
void PlayerSession::EditFilters(bool audio, FilterEditPlan plan) {
    if (snapshot_.session != SessionState::Ready) return;
    if (!plan.valid) { snapshot_.error = plan.error; Notify(); return; }
    auto const& current = audio ? snapshot_.audioFilters : snapshot_.videoFilters;
    if (!plan.FreshMatches(current)) {
        snapshot_.error = "Filter list changed; select the filter again"; Notify(); return;
    }
    if (!client_->EditFilterChainAsync(audio, std::move(plan))) {
        snapshot_.error = "Filter edit could not be queued"; Notify();
    }
    // Neither optimistic row mutation nor playback restart. Observed vf/af is
    // authoritative. A competing script can still change the chain between
    // worker get/set; mpv has no compare-and-swap property API.
}
}

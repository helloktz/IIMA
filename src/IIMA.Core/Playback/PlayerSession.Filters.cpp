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
bool PlayerSession::EditFiltersWithCompletion(bool audio, FilterEditPlan plan, FilterEditCompleted completion) {
    if (snapshot_.session != SessionState::Ready || !completion || filterEditCallbacks_.size() >= 8 ||
        !plan.valid || !plan.FreshMatches(audio ? snapshot_.audioFilters : snapshot_.videoFilters)) return false;
    auto id = client_->EditFilterChainAsync(audio,std::move(plan),snapshot_.entryId,snapshot_.path);
    if (!id) return false;
    filterEditCallbacks_.emplace(id,std::move(completion));
    return true;
}
void PlayerSession::HandleFilterEditReply(MpvEvent const& event) {
    if (event.kind == MpvEvent::Kind::Failure || event.kind == MpvEvent::Kind::Shutdown) {
        CancelFilterEditCallbacks(); return;
    }
    if (event.kind != MpvEvent::Kind::CommandReply) return;
    auto found = filterEditCallbacks_.find(event.requestId);
    if (found == filterEditCallbacks_.end()) return;
    auto callback = std::move(found->second); filterEditCallbacks_.erase(found);
    auto path = event.properties.find("path");
    if (event.entryId != snapshot_.entryId || path == event.properties.end() || path->second != snapshot_.path) {
        callback(false,{},"Media changed or could not be identified during filter readback"); return;
    }
    // Readback is copied on the sole mpv worker immediately after the set reply.
    // A successful set without a complete readback is not restoration proof.
    if (event.filters.complete && (event.name == "vf" || event.name == "af")) {
        auto& target = event.name == "vf" ? snapshot_.videoFilters : snapshot_.audioFilters;
        auto revision = target.revision + 1;
        target = event.filters; target.revision = revision;
    }
    callback(event.error >= 0, event.filters, event.text);
}
void PlayerSession::CancelFilterEditCallbacks() {
    auto callbacks = std::move(filterEditCallbacks_); filterEditCallbacks_.clear();
    for (auto& [id, callback] : callbacks) if (callback)
        callback(false,{},"Player stopped before filter edit readback; outcome unconfirmed");
}

}

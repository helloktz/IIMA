// IINA OpenURLWindowController loading/failure/stop lifecycle, adapted for
// independently routed Windows players. Copyright (c) 2018 lhc and contributors.
// GPL-3.0; references/iina/LICENSE.
#include "PlayerSession.h"
namespace iima {
namespace {
bool Pending(OpenAttemptPhase phase) {
    return phase==OpenAttemptPhase::Pending || phase==OpenAttemptPhase::Loading ||
        phase==OpenAttemptPhase::RedirectWaiting;
}
}
bool PlayerSession::CancelNetworkOpen(std::uint64_t expectedIntent) {
    auto& attempt=snapshot_.networkOpenAttempt;
    if (expectedIntent!=snapshot_.mediaOpenIntentRevision || expectedIntent!=attempt.intent ||
        !Pending(attempt.phase)) return false;
    // Also cover external mpv starts that do not increment the App intent.
    // Until a start is positively correlated, a changed generation cannot be
    // stopped on this request's behalf (including unknown redirects/overflow).
    if (snapshot_.mediaGeneration!=networkOwnedGeneration_.value_or(networkBaselineGeneration_)) {
        attempt.phase=OpenAttemptPhase::Superseded;
        Notify();
        return false;
    }
    attempt.phase=OpenAttemptPhase::Cancelled;
    Stop(); // Changes intent synchronously before any queued events can arrive.
    return true;
}
void PlayerSession::HandleNetworkOpenEvent(MpvEvent const& event) {
    auto& attempt=snapshot_.networkOpenAttempt;
    if (attempt.intent!=snapshot_.mediaOpenIntentRevision) return;
    // An external mpv binding/automatic playlist advance can start another
    // entry without an application intent. Do not retain a retry over it.
    if (attempt.phase==OpenAttemptPhase::Failed && event.kind==MpvEvent::Kind::StartFile) {
        attempt.phase=OpenAttemptPhase::Superseded; return;
    }
    if (!Pending(attempt.phase)) return;
    if (event.kind==MpvEvent::Kind::Failure || event.kind==MpvEvent::Kind::Shutdown) {
        attempt.phase=OpenAttemptPhase::Failed;
        attempt.error="Playback service unavailable";
        return;
    }
    if (event.kind==MpvEvent::Kind::CommandReply) {
        if (networkPauseRequest_ && event.requestId==networkPauseRequest_) {
            networkPauseRequest_=0;
            if (event.error<0) { attempt.phase=OpenAttemptPhase::Failed; attempt.error="Unable to prepare media loading"; }
        }
        if (networkLoadRequest_ && event.requestId==networkLoadRequest_) {
            networkLoadRequest_=0;
            if (event.error<0 || event.entryId<0) {
                attempt.phase=OpenAttemptPhase::Failed; attempt.error="Unable to submit media URL"; return;
            }
            networkEntry_=event.entryId;
            EvaluateNetworkOpen();
        }
        return;
    }
    if (!networkLoadRequest_ && !networkEntry_) return;
    if (event.kind!=MpvEvent::Kind::StartFile && event.kind!=MpvEvent::Kind::Loaded &&
        event.kind!=MpvEvent::Kind::EndFile) return;
    if (networkLifecycle_.size()==64) {
        networkJournalOverflow_=true; attempt.phase=OpenAttemptPhase::RedirectWaiting;
        networkLifecycle_.clear(); return;
    }
    if (networkJournalOverflow_) return; // Unknown never guesses success/failure; cancel still checks ownership.
    networkLifecycle_.push_back({event.kind,
        event.kind==MpvEvent::Kind::Loaded ? snapshot_.entryId : event.entryId,
        event.error,event.reason,event.insertedEntryId,
        snapshot_.mediaGeneration+(event.kind==MpvEvent::Kind::StartFile ? 1 : 0)});
    EvaluateNetworkOpen();
}
void PlayerSession::EvaluateNetworkOpen() {
    if (!networkEntry_ || networkJournalOverflow_) return;
    auto& attempt=snapshot_.networkOpenAttempt;
    auto entry=*networkEntry_;
    bool started=false, everStarted=false;
    for (auto const& event:networkLifecycle_) {
        if (event.kind==MpvEvent::Kind::StartFile && everStarted && (started || event.entry!=entry)) {
            attempt.phase=OpenAttemptPhase::Superseded; return;
        }
        if (event.entry!=entry) continue;
        if (event.kind==MpvEvent::Kind::StartFile) {
            started=true; everStarted=true; networkOwnedGeneration_=event.generation;
            attempt.phase=OpenAttemptPhase::Loading;
        }
        else if (event.kind==MpvEvent::Kind::Loaded && started) {
            attempt.phase=OpenAttemptPhase::Loaded; return;
        } else if (event.kind==MpvEvent::Kind::EndFile && started) {
            // MPV_END_FILE_REASON_REDIRECT=5. Follow only the positively
            // identified first inserted entry; never attach to an unrelated start.
            if (event.reason==5) {
                attempt.phase=OpenAttemptPhase::RedirectWaiting;
                if (event.insertedEntry<0) return;
                entry=event.insertedEntry; started=false;
            } else {
                attempt.phase=OpenAttemptPhase::Failed;
                attempt.error=event.error<0 ? "Unable to open network media" : "Media ended before loading completed";
                return;
            }
        }
    }
}
}

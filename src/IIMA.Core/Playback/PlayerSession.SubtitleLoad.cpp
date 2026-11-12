// IINA PlayerCore.loadExternalSubFile, Copyright © 2016 lhc. GPL-3.0.
// references/iina-source.txt; references/iina/LICENSE.
// Windows adaptation: bounded batches, fresh worker identities and cancellation.
#include "PlayerSession.h"
#include <set>
#include <utility>
namespace iima {
bool PlayerSession::AddSubtitle(std::string path, bool select) {
    std::vector<SubtitleLoadItem> items;
    items.push_back({std::move(path),select});
    return AddSubtitles(std::move(items));
}
void PlayerSession::CancelSubtitleLoad() {
    if(subtitleLoadCancelled_) subtitleLoadCancelled_->store(true);
    if(subtitleLoadStatus_.phase!=SubtitleLoadStatus::Phase::Loading) return;
    subtitleLoadStatus_.phase=SubtitleLoadStatus::Phase::Cancelled;
    // The outstanding item drains. No later item is submitted, no rollback.
    Notify();
}
bool PlayerSession::SubtitleLoadContextValid() const {
    return snapshot_.session==SessionState::Ready && snapshot_.media==MediaState::Loaded &&
        !waitingForStart_ && !snapshot_.mediaOriginPending &&
        snapshot_.mediaGeneration==subtitleLoadGeneration_ &&
        snapshot_.mediaOpenIntentRevision==subtitleLoadOpenIntent_ &&
        trackEditIntentRevision_==subtitleLoadEditIntent_ &&
        snapshot_.entryId==subtitleLoadEntry_ && snapshot_.path==subtitleLoadMediaPath_;
}
bool PlayerSession::AddSubtitles(std::vector<SubtitleLoadItem> items) {
    BeginTrackEditIntent();
    CancelSubtitleReload();
    ++subtitleEditIntentRevision_;
    if(SubtitleLoadBusy() || SubtitleReloadBusy() || ExternalAVLoadBusy() || ExternalTrackRemovalBusy() || ExternalTrackReloadBusy() ||
        snapshot_.session!=SessionState::Ready || snapshot_.media!=MediaState::Loaded || waitingForStart_ ||
        snapshot_.mediaOriginPending || pauseRequest_ || loadRequest_ || !pendingCommands_.empty() || playlistEdit_ ||
        snapshot_.entryId<0 || snapshot_.path.empty() || snapshot_.path.size()>131072 || items.empty() || items.size()>64) return false;
    std::set<std::string> paths;
    std::size_t bytes{};
    for(auto const& item:items) {
        if(!LocalTrackPath(item.path) || item.path.size()>1024*1024-bytes || !paths.insert(item.path).second) return false;
        bytes+=item.path.size();
    }
    const auto revision=subtitleLoadStatus_.revision+1;
    subtitleLoadStatus_={};subtitleLoadStatus_.revision=revision;
    subtitleLoadStatus_.phase=SubtitleLoadStatus::Phase::Loading;
    subtitleLoadStatus_.total=items.size();subtitleLoadItems_=std::move(items);
    subtitleLoadGeneration_=snapshot_.mediaGeneration;subtitleLoadOpenIntent_=snapshot_.mediaOpenIntentRevision;
    subtitleLoadEditIntent_=trackEditIntentRevision_;subtitleLoadEntry_=snapshot_.entryId;subtitleLoadMediaPath_=snapshot_.path;
    try {subtitleLoadCancelled_=std::make_shared<std::atomic_bool>(false);}
    catch(...) {
        subtitleLoadStatus_.phase=SubtitleLoadStatus::Phase::Failed;
        subtitleLoadStatus_.error="Could not allocate subtitle loading request";
        subtitleLoadItems_.clear();Notify();return false;
    }
    const bool accepted=SubmitSubtitleLoad();Notify();return accepted;
}
bool PlayerSession::SubmitSubtitleLoad() {
    auto const& item=subtitleLoadItems_[subtitleLoadStatus_.processed];
    try {subtitleLoadRequest_=client_->LoadSubtitleAsync(item.path,item.select,subtitleLoadEntry_,subtitleLoadMediaPath_,subtitleLoadCancelled_);}
    catch(...) {subtitleLoadRequest_=0;}
    subtitleLoadStatus_.inFlight=subtitleLoadRequest_!=0;
    if(!subtitleLoadRequest_) {
        subtitleLoadStatus_.phase=SubtitleLoadStatus::Phase::Failed;
        subtitleLoadStatus_.error="Subtitle loading request was not queued; remaining items were not submitted";
        subtitleLoadItems_.clear();subtitleLoadCancelled_.reset();
    }
    return subtitleLoadRequest_!=0;
}
bool PlayerSession::HandleSubtitleLoadReply(MpvEvent const& event) {
    if(event.kind!=MpvEvent::Kind::SubtitleLoadReply) return false;
    if(!subtitleLoadRequest_ || event.requestId!=subtitleLoadRequest_) return true;
    subtitleLoadRequest_=0;subtitleLoadStatus_.inFlight=false;
    ++subtitleLoadStatus_.processed;
    subtitleLoadStatus_.submitted+=event.subtitleLoadSubmitted;
    subtitleLoadStatus_.confirmed+=event.subtitleLoadConfirmed;
    subtitleLoadStatus_.skipped+=event.subtitleLoadSkipped;
    const bool failed=event.error<0 || !event.subtitleLoadConfirmed;
    subtitleLoadStatus_.failed+=failed;
    const bool cancelled=subtitleLoadStatus_.phase==SubtitleLoadStatus::Phase::Cancelled ||
        (subtitleLoadCancelled_ && subtitleLoadCancelled_->load()) || !SubtitleLoadContextValid();
    if(cancelled) subtitleLoadStatus_.phase=SubtitleLoadStatus::Phase::Cancelled;
    else if(failed) {
        subtitleLoadStatus_.phase=SubtitleLoadStatus::Phase::Failed;
        subtitleLoadStatus_.error=event.subtitleLoadConfirmed ?
            "Subtitle presence/selection was confirmed, but mpv reported an error; remaining items were not submitted" :
            event.subtitleLoadCommandSucceeded ? "Subtitle command was acknowledged, but fresh track state was not confirmed; remaining items were not submitted" :
            "Subtitle loading failed or could not be confirmed; tracks may have changed, remaining items were not submitted";
    } else if(subtitleLoadStatus_.processed==subtitleLoadStatus_.total) subtitleLoadStatus_.phase=SubtitleLoadStatus::Phase::Completed;
    else {
        // No Notify between context validation and advancing: user intent cannot
        // reenter here and accidentally revive a cancelled batch.
        SubmitSubtitleLoad();Notify();return true;
    }
    subtitleLoadItems_.clear();subtitleLoadCancelled_.reset();Notify();return true;
}
}

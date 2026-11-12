// Adapted from IINA PlayerCore playlist operations and MPVController event flow.
// Copyright (c) 2016-2025 lhc and contributors. GPL-3.0.
// Source/version/license: references/iina-source.txt, references/iina/LICENSE.
#include "PlayerSession.h"
#include <algorithm>
#include <utility>

namespace iima {
void PlayerSession::CancelPlaylistEdits() {
    if(playlistEdit_) playlistEdit_->Cancel();
    playlistEdit_.reset();
    playlistEditCommand_=0;
    playlistEditQuery_=0;
    playlistShuffleAfter_.reset();
}

void PlayerSession::FailPlaylistEdit(std::string error) {
    CancelPlaylistEdits();
    snapshot_.error=std::move(error);
    Notify();
}

void PlayerSession::BeginPlaylistEdit(PlaylistEditPlan plan, std::optional<bool> shuffledAfter) {
    if(snapshot_.session!=SessionState::Ready) return;
    // Do not replace an executing edit with one based on its not-yet-observed
    // intermediate state. Explicit Open/Stop/Close may cancel it instead.
    if(playlistEdit_) {
        snapshot_.error="A playlist edit is still in progress";
        Notify();
        return;
    }
    if(pauseRequest_ || loadRequest_ || !pendingCommands_.empty()) {
        snapshot_.error="Wait for the pending media switch before editing the playlist";
        Notify();
        return;
    }
    if(!plan) { snapshot_.error=plan.error; Notify(); return; }
    if(plan.steps.empty()) return;
    playlistShuffleAfter_=shuffledAfter;
    if(plan.clearShuffleState) playlistShuffleAfter_=false;
    playlistEditRevision_=plan.sourceRevision;
    playlistEdit_=std::make_unique<PlaylistPlanCursor>(std::move(plan));
    // Even the first command gets an explicit fresh read. The UI's observation
    // stream may lag a queued loadfile or an external script mutation.
    playlistEditQuery_=client_->GetPlaylistAsync();
    if(!playlistEditQuery_) { FailPlaylistEdit("Player is closing; playlist edit was not submitted"); return; }
    Notify();
}

void PlayerSession::AdvancePlaylistEdit(std::span<const PlaylistItem> refreshed) {
    if(!playlistEdit_) return;
    if(playlistEdit_->Finished()) {
        const auto error=playlistEdit_->Error();
        if(!error.empty()) { FailPlaylistEdit(error); return; }
        const auto shuffled=playlistShuffleAfter_;
        CancelPlaylistEdits();
        if(shuffled) playlistShuffled_=*shuffled;
        Notify();
        return;
    }
    const auto* step=playlistEdit_->Next(refreshed,playlistEditRevision_);
    if(!step) {
        const auto error=playlistEdit_->Error();
        FailPlaylistEdit(error.empty() ? "Playlist edit could not advance" : error);
        return;
    }
    const auto request=client_->Command(step->arguments);
    if(!request) { FailPlaylistEdit("Player is closing; remaining playlist edits were cancelled"); return; }
    if(!playlistEdit_->Begin(request)) {
        // The command has already been submitted. Do not represent cancelling
        // the remaining plan as undoing this command.
        FailPlaylistEdit("Playlist command was submitted but its transaction could not be tracked");
        return;
    }
    playlistEditCommand_=request;
}

bool PlayerSession::HandlePlaylistEditEvent(MpvEvent const& event) {
    if(event.kind==MpvEvent::Kind::PlaylistReply) {
        // A query from a cancelled edit is not allowed to revive that edit or
        // overwrite the current UI projection with a now-obsolete snapshot.
        if(!playlistEdit_ || !playlistEditQuery_ || event.requestId!=playlistEditQuery_) return true;
        playlistEditQuery_=0;
        if(event.error<0) {
            FailPlaylistEdit("Could not refresh the playlist after an edit: " + event.text);
            return true;
        }
        snapshot_.playlist=event.playlist;
        ++snapshot_.playlistRevision;
        if(playlistEditCommand_) {
            const auto command=playlistEditCommand_;
            playlistEditCommand_=0;
            if(!playlistEdit_->Complete(command,0,event.playlist)) {
                const auto error=playlistEdit_->Error();
                FailPlaylistEdit(error.empty() ? "Playlist edit completion was not accepted" : error);
                return true;
            }
        }
        AdvancePlaylistEdit(event.playlist);
        Notify();
        return true;
    }
    if(event.kind!=MpvEvent::Kind::CommandReply || !playlistEdit_ || !playlistEditCommand_ ||
       event.requestId!=playlistEditCommand_) return false;
    if(event.error<0) {
        playlistEdit_->Complete(event.requestId,event.error,{});
        FailPlaylistEdit("Playlist edit failed; remaining edits were cancelled: " + event.text);
        return true;
    }
    // A successful mutating step has already invalidated the former shuffle
    // ordering, even if a later step or fresh query fails.
    if(playlistShuffleAfter_ && !*playlistShuffleAfter_) playlistShuffled_=false;
    // Command success is not a playlist snapshot. Queue a GET in the worker's
    // same serialized lane and wait for its own GET_PROPERTY_REPLY. Property
    // observations can update UI in between but never advance this cursor.
    playlistEditQuery_=client_->GetPlaylistAsync();
    if(!playlistEditQuery_) FailPlaylistEdit("Player is closing; playlist refresh was not submitted");
    return true;
}

void PlayerSession::SortPlaylist(PlaylistSort order) {
    BeginPlaylistEdit(PlanPlaylistSort(snapshot_.playlist,order,snapshot_.playlistRevision));
}
void PlayerSession::ClearPlaylist() {
    BeginPlaylistEdit(PlanPlaylistClear(snapshot_.playlist,snapshot_.playlistRevision));
}
void PlayerSession::ShufflePlaylist(bool shuffled) {
    BeginPlaylistEdit(PlanPlaylistShuffle(snapshot_.playlist,shuffled,snapshot_.playlistRevision),!shuffled);
}
void PlayerSession::InsertPlaylist(std::vector<std::string> paths,std::optional<std::size_t> at) {
    BeginPlaylistEdit(PlanPlaylistInsert(snapshot_.playlist,paths,at,snapshot_.playlistRevision));
}
void PlayerSession::RemovePlaylistItems(std::vector<std::size_t> indices) {
    BeginPlaylistEdit(PlanPlaylistRemove(snapshot_.playlist,indices,snapshot_.playlistRevision));
}
void PlayerSession::DropPlaylistItems(std::vector<std::size_t> indices,std::size_t beforeRow) {
    BeginPlaylistEdit(PlanPlaylistDrop(snapshot_.playlist,indices,beforeRow,snapshot_.playlistRevision));
}
void PlayerSession::QueuePlaylistItemsNext(std::vector<std::size_t> indices) {
    BeginPlaylistEdit(PlanPlaylistPlayNext(snapshot_.playlist,indices,snapshot_.playlistRevision));
}
void PlayerSession::PlaylistRemove(std::size_t index) {
    RemovePlaylistItems({index});
}
void PlayerSession::PlaylistMove(std::size_t from,std::size_t finalIndex) {
    if(from>=snapshot_.playlist.size() || finalIndex>=snapshot_.playlist.size() || from==finalIndex) return;
    std::vector<std::int64_t> desired;
    for(const auto& item:snapshot_.playlist) desired.push_back(item.id);
    const auto moving=desired[from];
    desired.erase(desired.begin()+static_cast<std::ptrdiff_t>(from));
    desired.insert(desired.begin()+static_cast<std::ptrdiff_t>(finalIndex),moving);
    BeginPlaylistEdit(PlanPlaylistReorder(snapshot_.playlist,desired,snapshot_.playlistRevision));
}
} // namespace iima

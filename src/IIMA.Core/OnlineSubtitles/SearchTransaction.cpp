// IINA OnlineSubtitle._search / SubChooseViewController selection and cancellation.
// Copyright (c) lhc and contributors. SPDX-License-Identifier: GPL-3.0-or-later
#include "SearchTransaction.h"
#include <utility>
namespace iima {
SubtitleSearchTransaction::Ticket SubtitleSearchTransaction::Search(Media media) {
    current_ = Ticket{++generation_, std::move(media)};
    phase_ = Phase::Searching;
    return *current_;
}
bool SubtitleSearchTransaction::Matches(Ticket const& ticket) const noexcept {
    return current_ && current_->generation == ticket.generation && current_->media == ticket.media;
}
bool SubtitleSearchTransaction::MatchesMedia(Media const& media) const noexcept {
    return current_ && current_->media == media;
}
bool SubtitleSearchTransaction::SearchFinished(Ticket const& ticket, bool results) {
    if (!Matches(ticket) || phase_ != Phase::Searching) return false;
    phase_ = results ? Phase::Choosing : Phase::Completed;
    return true;
}
bool SubtitleSearchTransaction::Download(Ticket const& ticket) {
    if (!Matches(ticket) || phase_ != Phase::Choosing) return false;
    phase_ = Phase::Downloading;
    return true;
}
bool SubtitleSearchTransaction::DownloadFinished(Ticket const& ticket) {
    if (!Matches(ticket) || phase_ != Phase::Downloading) return false;
    phase_ = Phase::Completed;
    return true;
}
bool SubtitleSearchTransaction::Fail(Ticket const& ticket) {
    if (!Matches(ticket)) return false;
    phase_ = Phase::Failed;
    return true;
}
void SubtitleSearchTransaction::Cancel() {
    ++generation_;
    current_.reset();
    phase_ = Phase::Cancelled;
}
}

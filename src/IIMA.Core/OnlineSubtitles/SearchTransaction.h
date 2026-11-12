// IINA OnlineSubtitle._search / Provider.fetchSubtitles lifecycle, Windows adaptation.
// Copyright (c) lhc and contributors. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace iima {
// UI-thread owned transaction. Cancellation invalidates callbacks but does not
// claim that an already transmitted HTTP request can be undone.
class SubtitleSearchTransaction {
public:
    enum class Phase { Idle, Searching, Choosing, Downloading, Completed, Failed, Cancelled };
    struct Media {
        std::int64_t entry{-1};
        std::string path;
        bool operator==(Media const&) const = default;
    };
    struct Ticket {
        std::uint64_t generation{};
        Media media;
    };
    Ticket Search(Media media);
    bool SearchFinished(Ticket const&, bool hasResults);
    bool Download(Ticket const&);
    bool DownloadFinished(Ticket const&);
    bool Fail(Ticket const&);
    void Cancel();
    // False means current media no longer owns this search or its results.
    bool Matches(Ticket const&) const noexcept;
    bool MatchesMedia(Media const&) const noexcept;
    Phase State() const noexcept { return phase_; }
    std::optional<Ticket> Current() const { return current_; }
private:
    std::uint64_t generation_{};
    Phase phase_{Phase::Idle};
    std::optional<Ticket> current_;
};
}

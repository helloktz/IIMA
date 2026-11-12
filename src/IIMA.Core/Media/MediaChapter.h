// Ported from IINA MPVChapter.swift and SidebarChaptersPane.swift.
// Copyright (c) 2016 lhc and IINA contributors.
// Source version: references/iina-source.txt. License: references/iina/LICENSE (GPL v3).
#pragma once

#include "VideoTime.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace iima {

// Presentation-facing chapter metadata. It does not own or advance the mpv
// chapter list. The mpv property remains the authoritative playback state.
class MediaChapter final {
public:
    MediaChapter(std::optional<std::string> title, double startSeconds, std::int64_t index);

    // The index is the source index, not implicitly shifted to one-based.
    // An explicitly supplied empty title remains empty, just as Swift ?? does.
    // The caller supplies a localized fallback word at the Windows UI boundary.
    [[nodiscard]] std::string Title(std::string_view chapterWord = "Chapter") const;
    // Sidebar rows additionally replace an explicitly empty title with their row index.
    [[nodiscard]] std::string RowTitle(std::size_t row, std::string_view chapterWord = "Chapter") const;
    [[nodiscard]] const std::optional<std::string>& ExplicitTitle() const noexcept;
    [[nodiscard]] VideoTime Time() const noexcept;
    [[nodiscard]] std::int64_t Index() const noexcept;

    // IINA displays the current and following chapter timestamps with an arrow.
    // The last row has the End sentinel rather than assuming media duration.
    [[nodiscard]] std::string IntervalLabel(VideoTime nextChapterTime) const;
    static VideoTime FollowingTime(std::span<const MediaChapter> chapters,
                                   std::size_t row) noexcept;

private:
    std::optional<std::string> title_;
    VideoTime time_;
    std::int64_t index_;
};

} // namespace iima

// Ported from IINA MPVChapter.swift and SidebarChaptersPane.swift.
// Copyright (c) 2016 lhc and IINA contributors.
// Source version: references/iina-source.txt. License: references/iina/LICENSE (GPL v3).
#include "MediaChapter.h"
#include <utility>

namespace iima {

MediaChapter::MediaChapter(std::optional<std::string> title, double startSeconds,
                           std::int64_t index)
    : title_(std::move(title)), time_(startSeconds), index_(index) {}

std::string MediaChapter::Title(std::string_view chapterWord) const {
    if (title_) return *title_;
    return std::string(chapterWord) + " " + std::to_string(index_);
}

std::string MediaChapter::RowTitle(std::size_t row, std::string_view chapterWord) const {
    auto title = Title(chapterWord);
    return title.empty() ? std::string(chapterWord) + " " + std::to_string(row) : title;
}

const std::optional<std::string>& MediaChapter::ExplicitTitle() const noexcept {
    return title_;
}

VideoTime MediaChapter::Time() const noexcept { return time_; }
std::int64_t MediaChapter::Index() const noexcept { return index_; }

std::string MediaChapter::IntervalLabel(VideoTime nextChapterTime) const {
    return time_.StringRepresentation() + " \xE2\x86\x92 " + nextChapterTime.StringRepresentation();
}

VideoTime MediaChapter::FollowingTime(std::span<const MediaChapter> chapters,
                                     std::size_t row) noexcept {
    // Avoid row + 1 overflow when an invalid caller supplies SIZE_MAX.
    if (row >= chapters.size() || chapters.size() - row <= 1) return VideoTime::Infinite();
    return chapters[row + 1].Time();
}

} // namespace iima

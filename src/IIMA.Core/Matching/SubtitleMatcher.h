// Derived from IINA AutoFileMatcher.swift, Copyright (c) 2017 lhc.
// GPL-3.0; source/version/license: references/iina-source.txt, references/iina/LICENSE.
#pragma once
#include "FileGroup.h"
#include <cstdint>
#include <span>

namespace iima {
enum class SubtitleAutoLoad { Disabled, FilenameContains, Iina };
struct SubtitleMatchOptions {
    SubtitleAutoLoad mode = SubtitleAutoLoad::Iina;
    bool forceMatchUnmatched = true; // upstream gates this with playlistAutoAdd
    std::vector<std::u32string> priorityStrings;
};
struct MediaSubtitleMatch {
    std::filesystem::path media;
    std::vector<std::filesystem::path> subtitles;
    // Same episode number in any subtitle series; for future manual selection.
    std::vector<std::filesystem::path> relatedSubtitles;
};
struct SubtitleMatchResult {
    std::uint64_t generation = 0;
    bool cancelled = false;
    bool forceMatchLimitReached = false;
    std::vector<MediaSubtitleMatch> media;
};

// Complete pure matching chain for already discovered local media/subtitles:
// natural order -> series groups -> mutual series match -> episode/substring
// match -> preference prioritization -> bounded mutual-nearest fallback.
// Paths remain filesystem::path (UTF-16 on Windows), never lossy ANSI strings.
// Inputs are exact paths, not URL strings. Filesystem enumeration/extension
// classification and asynchronous mpv playlist mutation belong to the caller.
// Cancelled runs return no partial matches; UI must also compare generation
// before applying a successful result to guard against a last-moment switch.
SubtitleMatchResult MatchSubtitles(std::span<const std::filesystem::path> media,
                                  std::span<const std::filesystem::path> subtitles,
                                  const SubtitleMatchOptions& options = {},
                                  std::uint64_t generation = 0,
                                  const MatchCancellation& cancelled = {});
} // namespace iima

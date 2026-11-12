// Derived from IINA AutoFileMatcher.swift, Copyright (c) 2017 lhc.
// GPL-3.0; source/version/license: references/iina-source.txt, references/iina/LICENSE.
#include "SubtitleMatcher.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace iima {
namespace {
constexpr auto NoDistance = std::numeric_limits<std::size_t>::max();

std::vector<MatchFile> Files(std::span<const std::filesystem::path> paths,
                           const MatchCancellation& cancelled) {
    std::set<std::filesystem::path> seen;
    std::vector<MatchFile> result;
    for (const auto& path : paths) {
        CheckMatchingCancelled(cancelled);
        // Do not canonicalize on disk, follow symlinks, or resolve network
        // shares during a pure matching operation. Exact lexical duplicates
        // are removed; case-sensitive directories retain distinct spellings.
        if (seen.insert(path.lexically_normal()).second) result.emplace_back(path);
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return NaturalFilenameLess(left.filename, right.filename);
    });
    return result;
}

std::optional<std::u32string_view> IntegerName(std::u32string_view name) {
    if (name.empty()) return std::nullopt;
    bool negative = false;
    if (name.front() == U'+' || name.front() == U'-') {
        negative = name.front() == U'-';
        name.remove_prefix(1);
    }
    if (name.empty() || std::any_of(name.begin(), name.end(), [](auto c) { return c < U'0' || c > U'9'; })) return std::nullopt;
    // Episode numbers should not wrap at machine Int limits. Normalize
    // arbitrary-length nonnegative numbers without arithmetic overflow.
    if (negative) return std::nullopt;
    while (name.size() > 1 && name.front() == U'0') name.remove_prefix(1);
    return name;
}

bool SameEpisode(const MatchFile& video, const MatchFile& sub) {
    if (!video.nameInSeries || !sub.nameInSeries) return false;
    const auto a = IntegerName(*video.nameInSeries), b = IntegerName(*sub.nameInSeries);
    return a && b ? *a == *b : video.nameInSeries == sub.nameInSeries;
}

std::size_t Occurrences(std::u32string_view name, std::span<const std::u32string> priorities) {
    std::size_t count = 0;
    for (const auto& priority : priorities) {
        if (priority.empty()) continue;
        for (std::size_t at = 0;;) {
            at = name.find(priority, at);
            if (at == std::u32string_view::npos) break;
            ++count;
            at += priority.size();
        }
    }
    return count;
}

using PrefixPairs = std::map<std::u32string, std::u32string>;
PrefixPairs PairSeries(const FileGroups& videos, const FileGroups& subs,
                       const MatchCancellation& cancelled) {
    // Ordered maps replace the nondeterministic Swift Dictionary tie-break.
    std::map<std::u32string, std::u32string> closestVideo;
    std::map<std::u32string, std::map<std::u32string, std::size_t>> distances;
    for (const auto& [sp, subIndices] : subs) {
        CheckMatchingCancelled(cancelled);
        std::size_t best = NoDistance;
        for (const auto& [vp, videoIndices] : videos) {
            if (videoIndices.size() <= 2) continue;
            const auto distance = FilenameDistance(vp, sp, cancelled);
            distances[sp][vp] = distance;
            if (distance < best) {
                best = distance;
                closestVideo[sp] = vp;
            }
        }
    }
    PrefixPairs matched;
    for (const auto& [vp, videoIndices] : videos) {
        if (videoIndices.size() <= 2) continue;
        CheckMatchingCancelled(cancelled);
        std::size_t best = NoDistance;
        std::optional<std::u32string> closestSub;
        for (const auto& [sp, subIndices] : subs) {
            const auto distance = distances.at(sp).at(vp);
            if (distance < best) {
                best = distance;
                closestSub = sp;
            }
        }
        if (!closestSub) continue;
        const auto threshold = static_cast<std::size_t>((vp.size() + closestSub->size()) * 0.6);
        const auto reverse = closestVideo.find(*closestSub);
        if (best < threshold && reverse != closestVideo.end() && reverse->second == vp) matched[vp] = *closestSub;
    }
    return matched;
}
} // namespace

SubtitleMatchResult MatchSubtitles(std::span<const std::filesystem::path> mediaPaths,
                                  std::span<const std::filesystem::path> subtitlePaths,
                                  const SubtitleMatchOptions& options,
                                  std::uint64_t generation,
                                  const MatchCancellation& cancelled) {
    SubtitleMatchResult result;
    result.generation = generation;
    try {
        CheckMatchingCancelled(cancelled);
        auto media = Files(mediaPaths, cancelled);
        auto subs = Files(subtitlePaths, cancelled);
        for (const auto& video : media) result.media.push_back({video.path, {}, {}});
        // Disabled really disables all loading. Upstream's force-match phase
        // happens to be skipped through an empty unmatched list; make explicit.
        if (options.mode == SubtitleAutoLoad::Disabled) return result;
        const auto videoGroups = GroupSeries(media, cancelled);
        const auto subGroups = GroupSeries(subs, cancelled);
        const auto prefixes = PairSeries(videoGroups, subGroups, cancelled);
        std::vector<bool> subMatched(subs.size());
        std::vector<std::size_t> unmatchedVideos;
        for (std::size_t vi = 0; vi < media.size(); ++vi) {
            CheckMatchingCancelled(cancelled);
            const auto& video = media[vi];
            std::vector<std::size_t> matches;
            if (options.mode == SubtitleAutoLoad::Iina && !video.prefix.empty()) {
                const auto prefix = prefixes.find(video.prefix);
                if (prefix != prefixes.end()) {
                    for (std::size_t si = 0; si < subs.size(); ++si) {
                        CheckMatchingCancelled(cancelled);
                        if (!SameEpisode(video, subs[si])) continue;
                        result.media[vi].relatedSubtitles.push_back(subs[si].path);
                        if (subs[si].prefix == prefix->second) {
                            matches.push_back(si);
                            subMatched[si] = true;
                        }
                    }
                }
            }
            for (std::size_t si = 0; si < subs.size(); ++si) {
                CheckMatchingCancelled(cancelled);
                if (!subMatched[si] && subs[si].filename.find(video.filename) != std::u32string::npos) {
                    matches.push_back(si);
                    subMatched[si] = true;
                }
            }
            if (matches.empty()) unmatchedVideos.push_back(vi);
            // Preserve the upstream rule of promoting only entries with more
            // occurrences than the minimum. Stable partition removes the
            // stale-index/Set-order ambiguity in the Swift remove/insert loop.
            if (!matches.empty() && !options.priorityStrings.empty()) {
                std::vector<std::size_t> counts(subs.size());
                auto minimum = NoDistance;
                for (const auto si : matches) {
                    counts[si] = Occurrences(subs[si].filename, options.priorityStrings);
                    minimum = std::min(minimum, counts[si]);
                }
                std::stable_partition(matches.begin(), matches.end(), [&](auto si) { return counts[si] > minimum; });
            }
            for (const auto si : matches) result.media[vi].subtitles.push_back(subs[si].path);
        }
        if (options.forceMatchUnmatched) {
            std::vector<std::size_t> unmatchedSubs;
            for (std::size_t si = 0; si < subs.size(); ++si) if (!subMatched[si]) unmatchedSubs.push_back(si);
            // Upstream strict bound: videoCount * subCount < 100 * 100.
            // Division avoids overflowing size_t before applying the bound.
            if (!unmatchedVideos.empty() && unmatchedSubs.size() > 9999 / unmatchedVideos.size()) {
                result.forceMatchLimitReached = true;
            } else if (!unmatchedVideos.empty() && !unmatchedSubs.empty()) {
                std::vector<std::vector<std::size_t>> distances(unmatchedVideos.size(),
                    std::vector<std::size_t>(unmatchedSubs.size(), NoDistance));
                std::vector<std::size_t> minVideo(unmatchedVideos.size(), NoDistance);
                std::vector<std::size_t> minSub(unmatchedSubs.size(), NoDistance);
                for (std::size_t v = 0; v < unmatchedVideos.size(); ++v) {
                    const auto& video = media[unmatchedVideos[v]];
                    for (std::size_t s = 0; s < unmatchedSubs.size(); ++s) {
                        CheckMatchingCancelled(cancelled);
                        const auto& sub = subs[unmatchedSubs[s]];
                        const auto raw = FilenameDistance(video.prefix, sub.prefix, cancelled) + FilenameDistance(video.suffix, sub.suffix, cancelled);
                        const auto threshold = static_cast<std::size_t>((video.filename.size() + sub.filename.size()) * 0.6);
                        if (raw >= threshold) continue;
                        distances[v][s] = raw;
                        minVideo[v] = std::min(minVideo[v], raw);
                        minSub[s] = std::min(minSub[s], raw);
                    }
                }
                for (std::size_t v = 0; v < unmatchedVideos.size(); ++v) {
                    CheckMatchingCancelled(cancelled);
                    if (minVideo[v] == NoDistance) continue;
                    for (std::size_t s = 0; s < unmatchedSubs.size(); ++s) {
                        if (distances[v][s] == minVideo[v] && distances[v][s] == minSub[s]) {
                            result.media[unmatchedVideos[v]].subtitles.push_back(subs[unmatchedSubs[s]].path);
                        }
                    }
                }
            }
        }
        CheckMatchingCancelled(cancelled);
    } catch (const MatchingCancelled&) {
        result.media.clear();
        result.cancelled = true;
        result.forceMatchLimitReached = false;
    }
    return result;
}
} // namespace iima

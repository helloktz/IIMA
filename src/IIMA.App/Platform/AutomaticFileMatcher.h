// IINA AutoFileMatcher.startMatching/getAllMediaFiles and PlayerCore auto-load.
// Copyright (c) 2017 lhc. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "../../IIMA.Core/Matching/SubtitleMatcher.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <stop_token>
#include <functional>

namespace iima {
struct AutomaticFileMatchOptions {
    bool expandPlaylist{true};
    SubtitleMatchOptions subtitles;
    std::wstring subtitleSearchPaths{L"./*"};
};
struct AutomaticFileMatchResult {
    std::filesystem::path current;
    std::uint64_t generation{};
    // Video group in natural order, then audio group in natural order. These
    // are candidates, NOT an instruction to expand if expandPlaylist is false.
    std::vector<std::filesystem::path> orderedMedia;
    SubtitleMatchResult matches;
    std::vector<std::wstring> warnings;
    bool cancelled{}, complete{};
};
class AutomaticFileMatcher final {
public:
    // Caller must prove a single explicit regular media source, and recheck
    // media/open-intent/playlist-edit generations before applying any result.
    // Media discovery is same-directory only; subtitle paths support one-level
    // child expansion. No recursive search or URL fetch (UNC paths may use network shares).
    // Cancellation is cooperative between filesystem calls, not an I/O timeout.
    // At most four outstanding scans process-wide; busy rejection is delivered
    // on the UI dispatcher. No window is retained by this discovery adapter.
    static std::stop_source Discover(std::filesystem::path current, AutomaticFileMatchOptions options,
        std::uint64_t generation, winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
        std::function<void(AutomaticFileMatchResult)> completion);
};
}

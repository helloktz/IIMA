#pragma once
// IINA HistoryWindowController fileExists / selectedPlayableEntries adaptation.
// Copyright (c) 2017 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "../../IIMA.Core/History/HistoryBrowser.h"
#include <map>
#include <stop_token>
#include <string>
#include <vector>
namespace iima {
struct HistoryAvailabilityScan {
    std::map<std::string, HistoryAvailability, std::less<>> entries;
    std::size_t attempted{};
    bool limited{};
};
// Background only. Fixed local drive, ordinary non-reparse ancestry; never
// deliberately probes a network/share, reads content or changes files. Unknown
// status is retained for offline/reparse paths; this is not an OS I/O sandbox.
// Limits are checked BETWEEN native calls, not a claim of OS I/O cancellation.
HistoryAvailabilityScan ScanHistoryAvailability(std::vector<std::string> const& paths,
    std::stop_token stop);
}

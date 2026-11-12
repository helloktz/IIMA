// Font family selection derived from IINA FontPickerWindowController.windowDidLoad.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <cstddef>
#include <stop_token>
#include <string>
#include <vector>

namespace iima {
struct FontFamilyEntry final {
    // Prefer the English family name for stable mpv input; fall back to the
    // collection's first name. This is not a PostScript/typeface identifier.
    std::wstring name;
    std::wstring displayName;
};
// Worker-only synchronous DirectWrite snapshot. Does not request font downloads
// or force a system collection refresh. Caller owns cancellation/lifetime and
// must marshal results to its UI thread. Cancellation throws hresult_canceled;
// factory/collection errors throw hresult_error. Bad individual families are
// skipped. More than 8192 families fails rather than returning a partial catalog.
// The snapshot is not a guarantee libass can resolve the family name.
class FontCatalog final {
public:
    static std::vector<FontFamilyEntry> Enumerate(std::stop_token stop = {});
};
}

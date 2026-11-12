// IINA JavascriptAPIPreferences / JavascriptPlugin.syncPreferences adaptation.
// Copyright (c) 2019 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <string>
#include <string_view>

namespace iima {
// Blocking private local storage; call from the plugin or background worker.
// No runtime file/network permission is granted by this API. Every identifier
// has a separate JSON object; defaults are not written until explicitly set.
class PluginPreferenceStore final {
public:
    // Validates UTF-8, depth, node count, keys and JSON-compatible values, then
    // emits canonical parser output. Throws rather than truncating a document.
    static std::string Normalize(std::string_view json);
    static std::string Load(std::string_view identifier);
    // Top-level three-way merge under a cross-process exclusive lock. Only
    // changed keys are applied; concurrent changes to one key are last-writer
    // wins. Nested objects/arrays are one preference value, not recursively merged.
    // On success caller advances its baseline to editedJson, NOT the returned
    // merged document unless it also refreshes its local preferences to that value.
    // Missing file means empty values. Corrupt/unsafe existing files cause failure
    // and are never silently overwritten. Save errors remain visible to caller.
    static std::string SaveMerged(std::string_view identifier,
        std::string_view baseJson, std::string_view editedJson);
};
}

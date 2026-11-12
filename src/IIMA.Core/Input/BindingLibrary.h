// Adapted from IINA SettingsPageKeyBindings.ConfigEditor named configuration
// creation/duplication/deletion. Copyright (c) lhc and contributors. GPL-3.0.
#pragma once
#include <map>
#include <string>
#include <string_view>
namespace iima {
using BindingLibrary = std::map<std::string, std::string, std::less<>>;
// JSON-backed named snapshots, not filenames. Exact names identify entries;
// ASCII case-insensitive collisions are rejected for Windows-facing consistency.
bool ValidBindingLibraryName(std::string_view name) noexcept;
bool ValidBindingLibrary(BindingLibrary const& library) noexcept;
std::string BindingLibraryError(BindingLibrary const& library, std::string_view name,
    std::string_view text, bool replace);
}

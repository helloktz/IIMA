// Derived from IINA JavascriptPlugin.swift / JavascriptPluginInstance.swift.
// Copyright (c) 2018 lhc. GNU GPL v3; references/iina/LICENSE.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iima {
enum class PluginPermission { NetworkRequest, ShowOSD, ShowAlert, VideoOverlay, FileSystem };
std::string_view PluginPermissionName(PluginPermission permission) noexcept;
bool PluginPermissionIsDangerous(PluginPermission permission) noexcept;
std::optional<PluginPermission> ParsePluginPermission(std::string_view value) noexcept;
struct PluginSubtitleProvider { std::string id; std::string name; };

// JSON adapters must reject wrong types instead of coercing numbers/bools into
// strings. Missing required fields remain nullopt. Localization is an adapter
// responsibility: choose the preferred language, then override display fields.
struct PluginManifestInput {
    std::optional<std::string> name, authorName, identifier, version, entry;
    std::optional<std::string> authorEmail, authorUrl, description;
    std::optional<std::string> globalEntry, preferencesPage, helpPage;
    std::optional<std::string> ghRepo, sidebarTabName;
    std::optional<std::int64_t> ghVersion;
    std::vector<std::string> permissions, allowedDomains;
    std::vector<PluginSubtitleProvider> subtitleProviders;
};
struct PluginManifest {
    std::string name, authorName, identifier, version, entry;
    std::optional<std::string> authorEmail, authorUrl, description;
    std::optional<std::string> globalEntry, preferencesPage, helpPage;
    std::optional<std::string> ghRepo, sidebarTabName;
    std::optional<std::int64_t> ghVersion;
    std::vector<PluginPermission> permissions; // dangerous first, then raw key
    std::vector<std::string> unknownPermissions;
    std::vector<std::string> allowedDomains; // canonical ASCII DNS patterns
    std::vector<PluginSubtitleProvider> subtitleProviders;
    bool HasPermission(PluginPermission permission) const noexcept;
    bool CanCheckForUpdates() const noexcept;
    // This is a declaration check, NOT an approval or network security boundary.
    // Caller must separately gate execution, schemes, redirects, address access,
    // consent and credential scope. Host must be parsed by the platform URL API.
    bool DeclaresNetworkHost(std::string_view host) const;
};
struct PluginManifestIssue {
    enum class Severity { Warning, Error };
    Severity severity{Severity::Error};
    std::string field;
    std::string message;
};
struct PluginManifestValidation {
    std::optional<PluginManifest> manifest;
    std::vector<PluginManifestIssue> issues;
};
// Does not read files or validate entry existence, symlinks/reparse points,
// manifest size, duplicate JSON keys or arbitrary preferenceDefault values.
PluginManifestValidation ValidatePluginManifest(PluginManifestInput const& input);

// Returns '/' separated relative path, or nullopt for Windows-unsafe input.
// Lexical only: filesystem adapter must verify every component stays beneath
// its opened package root and reject links/reparse points before reading.
std::optional<std::string> NormalizePluginRelativePath(std::string_view path);
bool IsPluginIdentifier(std::string_view identifier) noexcept;
std::optional<std::string> NormalizePluginDomain(std::string_view domain);
bool PluginDomainMatches(std::string_view pattern, std::string_view host);
}

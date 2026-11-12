// Static port of IINA plugin manifest parsing, permissions and host matching.
// Copyright (c) 2018 lhc. GNU GPL v3; references/iina/LICENSE.
#include "PluginManifest.h"
#include <algorithm>
#include <array>
#include <utility>

namespace iima {
namespace {
bool AlphaNumeric(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
bool IdentifierChar(char c) noexcept { return AlphaNumeric(c) || c == '_' || c == '-'; }
char Lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
std::string LowerAscii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), Lower);
    return result;
}
bool HasControl(std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool ReservedWindowsName(std::string_view part) {
    auto stem = LowerAscii(part.substr(0, part.find('.')));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
        stem == "conin$" || stem == "conout$") return true;
    if (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
        stem[3] >= '1' && stem[3] <= '9') return true;
    // Win32 also recognizes superscript 1, 2 and 3 in device names (UTF-8).
    return stem.size() == 5 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
        static_cast<unsigned char>(stem[3]) == 0xc2 &&
        (static_cast<unsigned char>(stem[4]) == 0xb9 ||
         static_cast<unsigned char>(stem[4]) == 0xb2 ||
         static_cast<unsigned char>(stem[4]) == 0xb3);
}
bool RepositoryName(std::string_view repo) {
    auto slash = repo.find('/');
    return slash != std::string_view::npos && slash > 0 && slash + 1 < repo.size() &&
        repo.find('/', slash + 1) == std::string_view::npos &&
        std::all_of(repo.begin(), repo.begin() + slash, IdentifierChar) &&
        std::all_of(repo.begin() + slash + 1, repo.end(), IdentifierChar);
}
bool RemoteHelp(std::string_view value) {
    // Only recognize a URL here. Full URL parsing and user-initiated navigation
    // are platform responsibilities. No URL is fetched by this module.
    auto colon = value.find("://");
    if (colon == std::string_view::npos) return false;
    auto scheme = LowerAscii(value.substr(0, colon));
    if (scheme != "https" && scheme != "http") return false;
    auto authority = value.substr(colon + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    return !authority.empty() && authority.find('@') == std::string_view::npos &&
        value.find('\\') == std::string_view::npos && !HasControl(value) &&
        value.find(' ') == std::string_view::npos;
}
}
std::string_view PluginPermissionName(PluginPermission permission) noexcept {
    switch (permission) {
    case PluginPermission::NetworkRequest: return "network-request";
    case PluginPermission::ShowOSD: return "show-osd";
    case PluginPermission::ShowAlert: return "show-alert";
    case PluginPermission::VideoOverlay: return "video-overlay";
    case PluginPermission::FileSystem: return "file-system";
    }
    return {};
}
bool PluginPermissionIsDangerous(PluginPermission permission) noexcept {
    return permission == PluginPermission::NetworkRequest || permission == PluginPermission::FileSystem;
}
std::optional<PluginPermission> ParsePluginPermission(std::string_view value) noexcept {
    constexpr std::array values{PluginPermission::NetworkRequest, PluginPermission::ShowOSD,
        PluginPermission::ShowAlert, PluginPermission::VideoOverlay, PluginPermission::FileSystem};
    for (auto permission : values) if (PluginPermissionName(permission) == value) return permission;
    return std::nullopt;
}
bool PluginManifest::HasPermission(PluginPermission permission) const noexcept {
    return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
}
bool PluginManifest::CanCheckForUpdates() const noexcept {
    // checkNewVersion requires BOTH values. The upstream supportAutoUpdate
    // computed property uses the inverse condition; don't perpetuate that bug.
    return ghRepo.has_value() && ghVersion.has_value();
}
bool IsPluginIdentifier(std::string_view identifier) noexcept {
    if (identifier.empty() || identifier.size() > 255) return false;
    bool dot = false;
    bool component = false;
    for (char c : identifier) {
        if (c == '.') {
            if (!component) return false;
            dot = true;
            component = false;
        } else {
            if (!IdentifierChar(c)) return false;
            component = true;
        }
    }
    return dot && component;
}
std::optional<std::string> NormalizePluginRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 4096 || HasControl(path)) return std::nullopt;
    if (path.front() == '/' || path.front() == '\\') return std::nullopt;
    std::string normalized;
    std::size_t start = 0;
    while (start < path.size()) {
        auto end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        auto part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.size() > 255 ||
            part.back() == '.' || part.back() == ' ' || ReservedWindowsName(part)) return std::nullopt;
        if (part.find_first_of(":*?\"<>|") != std::string_view::npos) return std::nullopt;
        // Percent escapes are not URL-decoded: these are filesystem paths.
        // Reject them rather than letting a later URI layer reinterpret traversal.
        if (part.find('%') != std::string_view::npos) return std::nullopt;
        if (!normalized.empty()) normalized += '/';
        normalized += part;
        if (end == path.size()) break;
        start = end + 1;
        if (start == path.size()) return std::nullopt;
    }
    return normalized;
}
std::optional<std::string> NormalizePluginDomain(std::string_view domain) {
    if (domain == "*") return std::string("*");
    bool wildcard = domain.starts_with("*.");
    if (wildcard) domain.remove_prefix(2);
    if (domain.empty() || domain.size() > 253) return std::nullopt;
    if (domain.back() == '.') domain.remove_suffix(1);
    if (domain.empty() || domain.back() == '.') return std::nullopt;
    std::size_t start = 0;
    while (start < domain.size()) {
        auto end = domain.find('.', start);
        if (end == std::string_view::npos) end = domain.size();
        auto label = domain.substr(start, end - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-' ||
            !std::all_of(label.begin(), label.end(), [](char c) { return AlphaNumeric(c) || c == '-'; }))
            return std::nullopt;
        start = end + 1;
    }
    return (wildcard ? "*." : "") + LowerAscii(domain);
}
bool PluginDomainMatches(std::string_view pattern, std::string_view host) {
    auto candidate = NormalizePluginDomain(host);
    auto allowed = NormalizePluginDomain(pattern);
    if (!candidate || !allowed || candidate->find('*') != std::string::npos) return false;
    if (*allowed == "*") return true;
    if (allowed->starts_with("*.")) {
        // IINA's hasSuffix(".example.org") excludes the apex and includes any
        // depth of subdomain, not just one label. Keep that boundary behavior.
        auto suffix = std::string_view(*allowed).substr(1);
        return candidate->size() > suffix.size() && candidate->ends_with(suffix);
    }
    return *candidate == *allowed;
}
bool PluginManifest::DeclaresNetworkHost(std::string_view host) const {
    if (!HasPermission(PluginPermission::NetworkRequest)) return false;
    return std::any_of(allowedDomains.begin(), allowedDomains.end(), [&](auto const& domain) {
        return PluginDomainMatches(domain, host);
    });
}
PluginManifestValidation ValidatePluginManifest(PluginManifestInput const& input) {
    PluginManifestValidation result;
    PluginManifest manifest;
    auto issue = [&](std::string field, std::string message, bool warning = false) {
        result.issues.push_back({warning ? PluginManifestIssue::Severity::Warning :
            PluginManifestIssue::Severity::Error, std::move(field), std::move(message)});
    };
    auto required = [&](std::optional<std::string> const& source, std::string& destination,
                        std::string const& field, std::size_t maximum) {
        if (!source || source->empty()) issue(field, "Required non-empty string is missing.");
        else if (source->size() > maximum || HasControl(*source)) issue(field, "String is too long or contains control characters.");
        else destination = *source;
    };
    required(input.name, manifest.name, "name", 512);
    required(input.authorName, manifest.authorName, "author.name", 512);
    required(input.identifier, manifest.identifier, "identifier", 255);
    required(input.version, manifest.version, "version", 128);
    if (input.identifier && !IsPluginIdentifier(*input.identifier))
        issue("identifier", "Expected ASCII reverse-domain identifier with at least two non-empty components.");
    auto localPath = [&](std::optional<std::string> const& source, std::string const& field, bool mandatory) {
        std::optional<std::string> output;
        if (!source) {
            if (mandatory) issue(field, "Required package-relative path is missing.");
        } else {
            output = NormalizePluginRelativePath(*source);
            if (!output) issue(field, "Expected safe package-relative Windows path without traversal, device names or alternate streams.");
        }
        return output;
    };
    auto entry = localPath(input.entry, "entry", true);
    if (entry) manifest.entry = *entry;
    manifest.globalEntry = localPath(input.globalEntry, "globalEntry", false);
    manifest.preferencesPage = localPath(input.preferencesPage, "preferencesPage", false);
    if (input.helpPage) {
        if (RemoteHelp(*input.helpPage) && input.helpPage->size() <= 4096) manifest.helpPage = input.helpPage;
        else manifest.helpPage = localPath(input.helpPage, "helpPage", false);
    }
    auto optionalText = [&](std::optional<std::string> const& source, std::optional<std::string>& destination,
                            std::string const& field, std::size_t maximum, bool multiline = false) {
        if (!source) return;
        bool invalid = std::any_of(source->begin(), source->end(), [multiline](unsigned char c) {
            return (c < 32 && !(multiline && (c == '\n' || c == '\r' || c == '\t'))) || c == 127;
        });
        if (source->size() > maximum || invalid) issue(field, "Optional string is too long or contains control characters.");
        else destination = source;
    };
    optionalText(input.authorEmail, manifest.authorEmail, "author.email", 512);
    optionalText(input.authorUrl, manifest.authorUrl, "author.url", 4096);
    optionalText(input.description, manifest.description, "description", 16384, true);
    optionalText(input.sidebarTabName, manifest.sidebarTabName, "sidebarTab.name", 512);
    if (input.ghRepo) {
        if (input.ghRepo->size() > 255 || !RepositoryName(*input.ghRepo))
            issue("ghRepo", "Expected owner/repository using ASCII letters, digits, underscore or hyphen.");
        else manifest.ghRepo = input.ghRepo;
    }
    manifest.ghVersion = input.ghVersion;
    if (input.ghVersion && *input.ghVersion < 0) issue("ghVersion", "Negative update sequence is not supported.");
    if (input.permissions.size() > 128) issue("permissions", "Too many permission declarations.");
    else for (auto const& value : input.permissions) {
        if (value.size() > 128 || HasControl(value)) { issue("permissions", "Invalid permission name."); continue; }
        if (auto parsed = ParsePluginPermission(value)) {
            if (!manifest.HasPermission(*parsed)) manifest.permissions.push_back(*parsed);
        } else if (std::find(manifest.unknownPermissions.begin(), manifest.unknownPermissions.end(), value) == manifest.unknownPermissions.end()) {
            manifest.unknownPermissions.push_back(value);
            issue("permissions", "Unknown permission is not granted: " + value, true);
        }
    }
    std::sort(manifest.permissions.begin(), manifest.permissions.end(), [](auto a, auto b) {
        auto ad = PluginPermissionIsDangerous(a), bd = PluginPermissionIsDangerous(b);
        return ad != bd ? ad : PluginPermissionName(a) < PluginPermissionName(b);
    });
    if (input.allowedDomains.size() > 256) issue("allowedDomains", "Too many domain declarations.");
    else for (auto const& value : input.allowedDomains) {
        auto normalized = NormalizePluginDomain(value);
        if (!normalized) { issue("allowedDomains", "Invalid ASCII host or wildcard-host declaration."); continue; }
        if (std::find(manifest.allowedDomains.begin(), manifest.allowedDomains.end(), *normalized) == manifest.allowedDomains.end())
            manifest.allowedDomains.push_back(std::move(*normalized));
    }
    if (!manifest.allowedDomains.empty() && !manifest.HasPermission(PluginPermission::NetworkRequest))
        issue("allowedDomains", "Domains do not grant network-request permission.", true);
    if (manifest.HasPermission(PluginPermission::NetworkRequest) && manifest.allowedDomains.empty())
        issue("allowedDomains", "No hosts are declared; plugin network requests would be denied.", true);
    if (input.subtitleProviders.size() > 128) issue("subtitleProviders", "Too many subtitle providers.");
    else for (auto const& provider : input.subtitleProviders) {
        if (provider.id.empty() || provider.id.size() > 255 || HasControl(provider.id) ||
            provider.name.empty() || provider.name.size() > 512 || HasControl(provider.name)) {
            issue("subtitleProviders", "Provider requires a non-empty bounded id and name.");
            continue;
        }
        if (std::any_of(manifest.subtitleProviders.begin(), manifest.subtitleProviders.end(), [&](auto const& previous) { return previous.id == provider.id; })) {
            issue("subtitleProviders", "Duplicate provider identifier.");
            continue;
        }
        manifest.subtitleProviders.push_back(provider);
    }
    auto invalid = std::any_of(result.issues.begin(), result.issues.end(), [](auto const& entry) {
        return entry.severity == PluginManifestIssue::Severity::Error;
    });
    if (!invalid) result.manifest = std::move(manifest);
    return result;
}
}

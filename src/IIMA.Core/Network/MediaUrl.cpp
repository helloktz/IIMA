// IINA OpenURLWindowController.getURL; Windows protocol policy adaptation.
// Copyright © 2018 lhc. GPL v3; references/iina/LICENSE.
#include "MediaUrl.h"
#include <algorithm>
namespace iima {
namespace {
bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : c; }
bool Protocol(std::string_view scheme) {
    return scheme == "http" || scheme == "https" || scheme == "rtsp" || scheme == "rtsps" ||
        scheme == "rtmp" || scheme == "rtmps" || scheme == "ftp" || scheme == "sftp";
}
}
std::optional<MediaUrlInput> PrepareMediaUrl(std::string_view input) {
    while (!input.empty() && Space(input.front())) input.remove_prefix(1);
    while (!input.empty() && Space(input.back())) input.remove_suffix(1);
    if (input.empty() || input.size() > 32768) return {};
    if (std::any_of(input.begin(), input.end(), [](unsigned char c) { return c < 32 || c == 127; })) return {};
    std::string value(input);
    auto colon = value.find("://");
    bool suppliedScheme = colon != std::string::npos;
    if (!suppliedScheme) {
        // A drive path or malformed explicit scheme must not become an HTTP
        // host. host:port remains valid when the suffix starts with a digit.
        auto singleColon = value.find(':');
        if (singleColon != std::string::npos && (singleColon + 1 == value.size() ||
            value[singleColon + 1] < '0' || value[singleColon + 1] > '9')) return {};
        value = "http://" + value;
        colon = 4;
    }
    auto scheme = value.substr(0, colon);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), Lower);
    if (!Protocol(scheme)) return {};
    value.replace(0, colon, scheme);
    auto authorityStart = colon + 3;
    auto authorityEnd = value.find_first_of("/?#", authorityStart);
    auto authority = std::string_view(value).substr(authorityStart,
        authorityEnd == std::string::npos ? value.size() - authorityStart : authorityEnd - authorityStart);
    if (authority.empty() || authority.find('\\') != std::string_view::npos ||
        authority.find(' ') != std::string_view::npos) return {};
    return MediaUrlInput{std::move(value), suppliedScheme};
}
bool IsNetworkMediaPath(std::string_view path) noexcept {
    return path.find("://") != std::string_view::npos;
}
bool CanPersistMediaPath(std::string_view path) noexcept {
    return !path.empty() && !IsNetworkMediaPath(path);
}
}

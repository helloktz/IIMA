// URL input rules adapted from IINA OpenURLWindowController.getURL.
// Copyright © 2018 lhc. GPL v3; references/iina/LICENSE.
#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace iima {
struct MediaUrlInput {
    std::string value;
    bool suppliedScheme{};
};
// Produces input for Windows.Foundation.Uri canonicalization. This is not a
// network request or a claim that a particular protocol exists in libmpv.
std::optional<MediaUrlInput> PrepareMediaUrl(std::string_view input);
bool IsNetworkMediaPath(std::string_view path) noexcept;
// Do not persist credentials, signed query strings or fragments in history.
// Returning false rejects network history entirely in this implementation.
bool CanPersistMediaPath(std::string_view path) noexcept;
}

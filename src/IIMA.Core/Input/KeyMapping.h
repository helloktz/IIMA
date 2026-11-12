// Static C++ port of IINA KeyMapping.swift / KeyCodeHelper.swift and
// PlayerCore.setKeyBindings/filterSectionBindings.
// Copyright (c) 2016 lhc. Derived from IINA, licensed under GPL-3.0.
// Source revision and license: references/iina-source.txt, references/iina/LICENSE.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace iima {

// Input strings are UTF-8. Normalization concerns mpv names, not OS key codes.
// Sequence support deliberately matches the pinned IINA implementation: strings
// containing more than one '-' pass through unchanged.
std::string NormalizeMpvKey(std::string_view key);
std::string EscapeReservedMpvKey(std::string_view key);
// Windows-friendly text; this does not assign an accelerator or interpret a VK.
std::string DisplayMpvKey(std::string_view normalizedKey);

class KeyMapping final {
public:
    // Like the Swift initializer, the constructor's isIinaCommand is explicit;
    // rawAction containing @iina is only interpreted by SetRawAction.
    KeyMapping(std::string rawKey, std::string rawAction,
               bool isIinaCommand = false,
               std::optional<std::string> comment = std::nullopt);

    const std::string& RawKey() const noexcept;
    const std::string& NormalizedMpvKey() const noexcept;
    const std::string& RawAction() const noexcept;
    const std::vector<std::string>& Action() const noexcept;
    bool IsIinaCommand() const noexcept;
    const std::optional<std::string>& Comment() const noexcept;

    void SetRawKey(std::string key);
    void SetRawAction(std::string action);
    void SetComment(std::optional<std::string> comment);
    std::optional<std::string> Section() const;
    std::string ReadableAction() const;
    std::string ConfFileFormat() const;

private:
    std::string rawKey_;
    std::string normalizedMpvKey_;
    std::string rawAction_;
    std::vector<std::string> action_;
    bool isIinaCommand_;
    std::optional<std::string> comment_;
};

struct InputConfIssue {
    std::size_t line = 0; // one-based physical line number
    std::string message;
};

struct InputConfResult {
    std::vector<KeyMapping> mappings;
    std::vector<InputConfIssue> issues;
    // File I/O error is separate from malformed individual mappings. Valid
    // mappings are retained when malformed lines are skipped.
    std::error_code error;
    explicit operator bool() const noexcept { return !error; }
};

// Inline '#' starts a comment even inside quotes, matching IINA's parser.
// Escaped spaces/quoted strings are not expanded here: this model preserves
// action spelling for downstream mpv command-string execution.
InputConfResult ParseInputConf(std::string_view text);
InputConfResult ReadInputConf(const std::filesystem::path& path);
std::string GenerateInputConf(std::span<const KeyMapping> mappings);

// Produces the application-active set using IINA's default-section filtering
// and last-binding-wins rule while keeping first-occurrence display ordering.
// This class does not execute arbitrary config commands or grant permissions.
class KeyBindingSet final {
public:
    explicit KeyBindingSet(std::span<const KeyMapping> mappings);
    const std::vector<KeyMapping>& Ordered() const noexcept;
    const KeyMapping* Find(std::string_view rawKey) const;
    std::size_t Size() const noexcept;

private:
    std::vector<KeyMapping> ordered_;
    std::unordered_map<std::string, std::size_t> index_;
};

} // namespace iima

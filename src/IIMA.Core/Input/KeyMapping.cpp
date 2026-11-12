// Static C++ port of IINA KeyMapping.swift / KeyCodeHelper.swift and
// PlayerCore.setKeyBindings/filterSectionBindings.
// Copyright (c) 2016 lhc. Derived from IINA, licensed under GPL-3.0.
// Source revision and license: references/iina-source.txt, references/iina/LICENSE.
#include "KeyMapping.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <utility>

namespace iima {
namespace {
constexpr std::string_view IinaPrefix = "@iina";
constexpr std::string_view FilePrefix = "#@iina";
constexpr std::array<std::string_view, 4> Modifiers = {
    "Ctrl", "Alt", "Shift", "Meta"
};

// Foundation CharacterSet.whitespaces includes horizontal Unicode separators.
// Preserve UTF-8 keys and parameters; do not locale-fold arbitrary bytes.
std::size_t WhitespaceLength(std::string_view text, std::size_t at) {
    if (at >= text.size()) return 0;
    const auto tail = text.substr(at);
    if (tail[0] == ' ' || tail[0] == '\t') return 1;
    constexpr std::array<std::string_view, 17> spaces = {
        "\xc2\xa0", "\xe1\x9a\x80", "\xe2\x80\x80", "\xe2\x80\x81",
        "\xe2\x80\x82", "\xe2\x80\x83", "\xe2\x80\x84", "\xe2\x80\x85",
        "\xe2\x80\x86", "\xe2\x80\x87", "\xe2\x80\x88", "\xe2\x80\x89",
        "\xe2\x80\x8a", "\xe2\x80\x8b", "\xe2\x80\xaf", "\xe2\x81\x9f",
        "\xe3\x80\x80"
    };
    for (const auto value : spaces) {
        if (tail.starts_with(value)) return value.size();
    }
    return 0;
}

std::string_view Trim(std::string_view text) {
    std::size_t start = 0;
    while (const auto length = WhitespaceLength(text, start)) start += length;
    std::size_t end = start;
    for (std::size_t at = start; at < text.size();) {
        if (const auto length = WhitespaceLength(text, at)) {
            at += length;
        } else {
            end = ++at;
        }
    }
    return text.substr(start, end - start);
}

std::vector<std::string> SplitAction(std::string_view text) {
    std::vector<std::string> result;
    std::size_t at = 0;
    while (at < text.size()) {
        if (const auto length = WhitespaceLength(text, at)) {
            at += length;
            continue;
        }
        const auto start = at;
        while (at < text.size() && !WhitespaceLength(text, at)) ++at;
        result.emplace_back(text.substr(start, at - start));
    }
    return result;
}

std::string Join(std::span<const std::string> parts, std::string_view separator) {
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty()) result += separator;
        result += part;
    }
    return result;
}

std::string UpperAscii(std::string_view text) {
    std::string result(text);
    for (auto& ch : result) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return result;
}

std::optional<std::string> ShiftedKey(std::string_view key) {
    if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') {
        return UpperAscii(key);
    }
    constexpr std::array<std::pair<std::string_view, std::string_view>, 21> pairs = {{
        {"1", "!"}, {"2", "@"}, {"3", "SHARP"}, {"4", "$"}, {"5", "%"},
        {"6", "^"}, {"7", "&"}, {"8", "*"}, {"9", "("}, {"0", ")"},
        {"=", "PLUS"}, {"-", "_"}, {"[", "{"}, {"]", "}"}, {"'", "\""},
        {";", ":"}, {"\\", "|"}, {",", "<"}, {"/", "?"}, {".", ">"}, {"`", "~"}
    }};
    for (const auto& [lower, upper] : pairs) {
        if (key == lower) return std::string(upper);
    }
    return std::nullopt;
}

bool AlreadyShifted(std::string_view key) {
    if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') return true;
    constexpr std::array<std::string_view, 21> shifted = {
        "!", "@", "SHARP", "$", "%", "^", "&", "*", "(", ")", "PLUS",
        "_", "{", "}", "\"", ":", "|", "<", "?", ">", "~"
    };
    return std::find(shifted.begin(), shifted.end(), key) != shifted.end();
}

std::vector<std::string> SplitPlus(std::string_view input) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto end = input.find('+', start);
        result.emplace_back(input.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

std::string NormalizeSingle(std::string_view input) {
    if (input == "+") return "PLUS";
    std::string expanded;
    // Swift replacingOccurrences uses non-overlapping matches.
    for (std::size_t at = 0; at < input.size();) {
        if (input.substr(at).starts_with("++")) {
            expanded += "+PLUS";
            at += 2;
        } else {
            expanded += input[at++];
        }
    }
    const auto parts = SplitPlus(expanded);
    auto key = parts.back();
    if (key == "#") key = "SHARP";
    else if (key == "+") key = "PLUS";
    else key = key.size() > 1 ? UpperAscii(key) : key;

    std::array<bool, 4> present{};
    for (std::size_t at = 0; at + 1 < parts.size(); ++at) {
        const auto modifier = UpperAscii(parts[at]);
        if (modifier == "SHIFT") {
            if (const auto shifted = ShiftedKey(key)) key = *shifted;
            else if (!AlreadyShifted(key)) present[2] = true;
        } else if (modifier == "CTRL") present[0] = true;
        else if (modifier == "ALT") present[1] = true;
        else if (modifier == "META") present[3] = true;
        // IINA collects unknown modifiers but drops them when projecting its
        // canonical four-modifier order. Preserve that behavior, not guesses.
    }
    std::string result;
    for (std::size_t at = 0; at < Modifiers.size(); ++at) {
        if (present[at]) {
            result += Modifiers[at];
            result += '+';
        }
    }
    result += key;
    return result;
}
} // namespace

std::string NormalizeMpvKey(std::string_view key) {
    if (key == "default-bindings" || std::count(key.begin(), key.end(), '-') > 1) {
        return std::string(key);
    }
    return NormalizeSingle(key);
}

std::string EscapeReservedMpvKey(std::string_view key) {
    // Intentional fix of the Swift function: checking for a literal space
    // after trimming it made that branch unreachable.
    if (key == " ") return "SPACE";
    key = Trim(key);
    if (key == "#") return "SHARP";
    return std::string(key);
}

std::string DisplayMpvKey(std::string_view key) {
    auto parts = SplitPlus(key);
    for (auto& part : parts) {
        if (part == "Meta") part = "Win";
        else if (part == "SHARP") part = "#";
        else if (part == "PLUS") part = "+";
        else if (part == "ENTER") part = "Enter";
        else if (part == "ESC") part = "Esc";
        else if (part == "SPACE") part = "Space";
        else if (part == "BS") part = "Backspace";
        else if (part == "DEL") part = "Delete";
        else if (part == "PGUP") part = "Page Up";
        else if (part == "PGDWN") part = "Page Down";
    }
    return Join(parts, "+");
}

KeyMapping::KeyMapping(std::string key, std::string action, bool isIinaCommand,
                       std::optional<std::string> comment)
    : rawKey_(std::move(key)), normalizedMpvKey_(NormalizeMpvKey(rawKey_)),
      rawAction_(std::move(action)), action_(SplitAction(rawAction_)),
      isIinaCommand_(isIinaCommand), comment_(std::move(comment)) {}

const std::string& KeyMapping::RawKey() const noexcept { return rawKey_; }
const std::string& KeyMapping::NormalizedMpvKey() const noexcept { return normalizedMpvKey_; }
const std::string& KeyMapping::RawAction() const noexcept { return rawAction_; }
const std::vector<std::string>& KeyMapping::Action() const noexcept { return action_; }
bool KeyMapping::IsIinaCommand() const noexcept { return isIinaCommand_; }
const std::optional<std::string>& KeyMapping::Comment() const noexcept { return comment_; }

void KeyMapping::SetRawKey(std::string key) {
    auto normalized = NormalizeMpvKey(key);
    rawKey_ = std::move(key);
    normalizedMpvKey_ = std::move(normalized);
}

void KeyMapping::SetRawAction(std::string action) {
    const bool isIinaCommand = action.starts_with(IinaPrefix);
    if (isIinaCommand) action = std::string(Trim(std::string_view(action).substr(IinaPrefix.size())));
    auto tokens = SplitAction(action);
    rawAction_ = std::move(action);
    action_ = std::move(tokens);
    isIinaCommand_ = isIinaCommand;
}

void KeyMapping::SetComment(std::optional<std::string> comment) {
    comment_ = std::move(comment);
}

std::optional<std::string> KeyMapping::Section() const {
    if (action_.size() <= 1 || !action_[0].starts_with('{')) return std::nullopt;
    const auto end = action_[0].find('}');
    if (end == std::string::npos) return std::nullopt;
    return std::string(Trim(std::string_view(action_[0]).substr(1, end - 1)));
}

std::string KeyMapping::ReadableAction() const {
    return (isIinaCommand_ ? std::string(IinaPrefix) + " " : "") + Join(action_, " ");
}

std::string KeyMapping::ConfFileFormat() const {
    std::string result;
    if (isIinaCommand_) result = std::string(FilePrefix) + " ";
    result += rawKey_ + " " + Join(action_, " ");
    if (comment_ && !comment_->empty()) result += "   #" + *comment_;
    return result;
}

InputConfResult ParseInputConf(std::string_view text) {
    InputConfResult result;
    // Accept the UTF-8 BOM and Windows CRLF instead of accidentally retaining
    // BOM as a key and CR as part of a command. This is an intentional adapter.
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        ++lineNumber;
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        if (end == std::string_view::npos) text = {};
        else text.remove_prefix(end + 1);
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (Trim(line).empty()) continue;
        bool isIinaCommand = false;
        if (line.starts_with('#')) {
            if (!line.starts_with(FilePrefix)) continue;
            isIinaCommand = true;
            line.remove_prefix(FilePrefix.size());
        }
        std::optional<std::string> comment;
        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            comment = std::string(line.substr(hash + 1));
            line = line.substr(0, hash);
        }
        // File key/action separation is ASCII space or tab, as in the Swift
        // split predicate (not the wider action tokenizer above).
        const auto start = line.find_first_not_of(" \t");
        const auto split = start == std::string_view::npos ? start : line.find_first_of(" \t", start);
        const auto actionAt = split == std::string_view::npos ? split : line.find_first_not_of(" \t", split);
        if (start == std::string_view::npos || split == std::string_view::npos || actionAt == std::string_view::npos) {
            // Pure indented comments are harmless and should not produce a
            // malformed-binding diagnostic.
            if (!Trim(line).empty()) result.issues.push_back({lineNumber, "Skipped input binding with no command"});
            continue;
        }
        auto key = Trim(line.substr(start, split - start));
        auto action = Trim(line.substr(actionAt));
        if (key.empty() || action.empty()) {
            result.issues.push_back({lineNumber, "Skipped empty key or command"});
            continue;
        }
        result.mappings.emplace_back(std::string(key), std::string(action), isIinaCommand, std::move(comment));
    }
    return result;
}

InputConfResult ReadInputConf(const std::filesystem::path& path) {
    errno = 0;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        InputConfResult result;
        result.error = errno ? std::error_code(errno, std::generic_category()) : std::make_error_code(std::errc::io_error);
        return result;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        InputConfResult result;
        result.error = std::make_error_code(std::errc::io_error);
        return result;
    }
    return ParseInputConf(text);
}

std::string GenerateInputConf(std::span<const KeyMapping> mappings) {
    std::string result = "# Generated by IIMA\n\n";
    for (const auto& mapping : mappings) result += mapping.ConfFileFormat() + '\n';
    return result;
}

KeyBindingSet::KeyBindingSet(std::span<const KeyMapping> mappings) {
    for (const auto& mapping : mappings) {
        if (mapping.RawKey() == "default-bindings" && mapping.Action().size() == 1 && mapping.Action()[0] == "start") continue;
        auto active = mapping;
        if (const auto section = active.Section()) {
            if (*section != "default") continue;
            active = KeyMapping(active.RawKey(), Join(std::span<const std::string>(active.Action()).subspan(1), " "),
                                active.IsIinaCommand(), active.Comment());
        }
        const auto key = active.NormalizedMpvKey();
        if (const auto found = index_.find(key); found != index_.end()) {
            ordered_[found->second] = std::move(active);
        } else {
            index_.emplace(key, ordered_.size());
            ordered_.push_back(std::move(active));
        }
    }
}

const std::vector<KeyMapping>& KeyBindingSet::Ordered() const noexcept { return ordered_; }
const KeyMapping* KeyBindingSet::Find(std::string_view rawKey) const {
    const auto found = index_.find(NormalizeMpvKey(rawKey));
    return found == index_.end() ? nullptr : &ordered_[found->second];
}
std::size_t KeyBindingSet::Size() const noexcept { return ordered_.size(); }

} // namespace iima

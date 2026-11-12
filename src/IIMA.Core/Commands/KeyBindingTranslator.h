// Adapted from IINA KeyBindingTranslator.swift / MPVCommandFormat.strings.
// Copyright (c) 2017 lhc. GNU GPL v3; see references/iina/LICENSE.
#pragma once
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iima {
using CommandTranslationDictionary = std::map<std::string, std::string, std::less<>>;
struct CommandCriterion {
    std::string name;
    std::string commandValue;
    bool placeholder{};
    bool applicationCommand{};
};
class KeyBindingTranslator final {
public:
    // Windows resource loader supplies localized strings using original keys
    // (read.seek, opt.volume, iina.open-file...). Missing resources fall back
    // to the original command. This class performs no command execution.
    explicit KeyBindingTranslator(CommandTranslationDictionary translations = {});
    static CommandTranslationDictionary DefaultFormats();
    std::string ReadableCommand(std::span<std::string const> action,
                                bool applicationCommand = false) const;
    // Returns no result for malformed criterion trees instead of Swift's
    // out-of-bounds/force-cast failure. Result is input.conf action spelling.
    static std::optional<std::string> FromCriteria(std::span<CommandCriterion const> criteria);
    CommandTranslationDictionary TranslationData(std::span<std::string const> action) const;
private:
    CommandTranslationDictionary translations_;
};
}

// Adapted from IINA KeyBindingTranslator.swift / MPVCommandFormat.strings.
// Copyright (c) 2017 lhc. GNU GPL v3; see references/iina/LICENSE.
#include "KeyBindingTranslator.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>

namespace iima {
namespace {
std::vector<std::string> Split(std::string_view text, char delimiter) {
    std::vector<std::string> result;
    for (;;) {
        auto end = text.find(delimiter);
        result.emplace_back(text.substr(0, end));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return result;
}
std::string Join(std::span<std::string const> values) {
    std::string result;
    bool first = true;
    for (auto const& value : values) {
        if (!first) result += ' ';
        first = false;
        result += value;
    }
    return result;
}
bool Prefix(std::string_view value) {
    return value == "no-osd" || value == "osd-auto" || value == "osd-bar" ||
        value == "osd-msg-bar" || value == "raw" || value == "repeatable" || value == "expand-properties";
}
std::vector<std::string> Commands(std::span<std::string const> values) {
    // Unlike Swift, strip only command-prefix position. An argument literally
    // named 'raw' is a valid value and must not shift following argument slots.
    std::size_t start = 0;
    while (start < values.size() && Prefix(values[start])) ++start;
    return {values.begin() + start, values.end()};
}
std::optional<double> Number(std::string_view text) {
    if (text.empty()) return std::nullopt;
    if (text.starts_with('+')) text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    double value{};
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value))
        return std::nullopt;
    return value;
}
std::string Pretty(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream.precision(3);
    auto scaled = value * 1000;
    stream << (std::isfinite(scaled) ? std::round(scaled) / 1000 : value);
    auto result = stream.str();
    while (!result.empty() && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result == "-0" ? "0" : result;
}
// Implements only the variable, positive and inverted section forms used by
// the pinned KeyBinding.strings resources. Unsupported/malformed templates
// fall back to command spelling. Output is plain UI text, never HTML.
std::optional<std::string> Render(std::string_view input,
    CommandTranslationDictionary const& data, std::size_t& position,
    std::string_view closing = {}, unsigned depth = 0) {
    if (depth > 32) return std::nullopt;
    std::string output;
    while (position < input.size()) {
        auto open = input.find("{{", position);
        if (open == std::string_view::npos) {
            output += input.substr(position);
            position = input.size();
            break;
        }
        output += input.substr(position, open - position);
        bool triple = input.substr(open).starts_with("{{{");
        auto end = input.find(triple ? "}}}" : "}}", open + (triple ? 3 : 2));
        if (end == std::string_view::npos) return std::nullopt;
        auto tag = input.substr(open + (triple ? 3 : 2), end - open - (triple ? 3 : 2));
        position = end + (triple ? 3 : 2);
        if (tag.empty()) return std::nullopt;
        if (!triple && tag.front() == '/') {
            if (tag.substr(1) != closing) return std::nullopt;
            return output;
        }
        if (!triple && (tag.front() == '#' || tag.front() == '^')) {
            auto key = tag.substr(1);
            auto content = Render(input, data, position, key, depth + 1);
            if (!content) return std::nullopt;
            auto found = data.find(key);
            bool present = found != data.end() && !found->second.empty();
            if ((tag.front() == '#' && present) || (tag.front() == '^' && !present)) output += *content;
        } else {
            if (!triple && (tag.front() == '>' || tag.front() == '=' || tag.front() == '!'))
                return std::nullopt;
            auto found = data.find(tag);
            if (found != data.end()) output += found->second;
        }
    }
    if (!closing.empty()) return std::nullopt;
    return output;
}
}
KeyBindingTranslator::KeyBindingTranslator(CommandTranslationDictionary translations)
    : translations_(std::move(translations)) {}
CommandTranslationDictionary KeyBindingTranslator::DefaultFormats() {
    return {{"seek", "command value opt:relative|absolute|absolute-percent|relative-percent|exact|keyframes"},
        {"set", "command property value"}, {"add", "command property value#+1"},
        {"cycle", "command property dir:up|down"}, {"cycle-values", "command property values..."},
        {"multiply", "command property factor"}, {"quit", "command value"}, {"sub-seek", "command skip:1|-1"}};
}
CommandTranslationDictionary KeyBindingTranslator::TranslationData(std::span<std::string const> action) const {
    auto commands = Commands(action);
    CommandTranslationDictionary data;
    if (commands.empty()) return data;
    auto formats = DefaultFormats();
    auto format = formats.find(commands.front());
    if (format == formats.end()) return data;
    auto parts = Split(format->second, ' ');
    for (std::size_t index = 0; index < parts.size(); ++index) {
        auto const& part = parts[index];
        auto fallback = part.find('#');
        auto choice = part.find(':');
        if (fallback != std::string::npos) {
            data[part.substr(0, fallback)] = index < commands.size() ? commands[index] : part.substr(fallback + 1);
        } else if (choice != std::string::npos) {
            auto key = part.substr(0, choice);
            auto options = Split(std::string_view(part).substr(choice + 1), '|');
            data[key + "_" + (index < commands.size() ? commands[index] : options.front())] = "true";
            if (index < commands.size()) data[key] = commands[index];
        } else if (part.ends_with("...")) {
            data[part.substr(0, part.size() - 3)] = index < commands.size() ?
                Join(std::span<std::string const>(commands).subspan(index)) : "";
        } else if (index < commands.size()) data[part] = commands[index];
    }
    if (auto property = data.find("property"); property != data.end()) {
        auto localized = translations_.find("opt." + property->second);
        if (localized != translations_.end()) property->second = localized->second;
    }
    if (auto value = data.find("value"); value != data.end()) {
        auto original = value->second;
        if (original == "yes" || original == "no") {
            auto translated = translations_.find(original);
            if (translated != translations_.end()) value->second = translated->second;
        } else if (commands.front() == "add" && !original.starts_with('-') && !original.starts_with('+')) {
            value->second.insert(value->second.begin(), '+');
        }
        if (commands.front() == "seek") {
            auto option = data.find("opt");
            auto flags = Split(option == data.end() ? "relative" : option->second, '+');
            bool absolute = std::any_of(flags.begin(), flags.end(), [](auto const& flag) { return flag.starts_with("absolute"); });
            if (absolute) data["abs"] = "true";
            else if (original.starts_with('-')) { data["bwd"] = "true"; value->second = original.substr(1); }
            else data["fwd"] = "true";
            bool percent = std::any_of(flags.begin(), flags.end(), [](auto const& flag) { return flag.ends_with("percent"); });
            data[percent ? "per" : "sec"] = "true";
            if (std::find(flags.begin(), flags.end(), "exact") != flags.end()) data["exact"] = "true";
            else if (std::find(flags.begin(), flags.end(), "keyframes") != flags.end()) data["kf"] = "true";
        }
    }
    return data;
}
std::string KeyBindingTranslator::ReadableCommand(std::span<std::string const> action, bool applicationCommand) const {
    auto commands = Commands(action);
    if (commands.empty()) return {};
    if (applicationCommand) {
        auto translation = translations_.find("iina." + commands.front());
        return translation == translations_.end() ? commands.front() : translation->second;
    }
    auto data = TranslationData(commands);
    auto translation = translations_.find("read." + commands.front());
    if (!data.empty() && translation != translations_.end()) {
        std::size_t position = 0;
        if (auto text = Render(translation->second, data, position)) return *text;
    }
    auto name = translations_.find("cmd." + commands.front());
    if (name != translations_.end()) commands.front() = name->second;
    return Join(commands);
}
std::optional<std::string> KeyBindingTranslator::FromCriteria(std::span<CommandCriterion const> criteria) {
    if (criteria.empty() || criteria.front().placeholder) return std::nullopt;
    std::vector<std::string> parts;
    for (auto const& criterion : criteria) if (!criterion.placeholder) parts.push_back(criterion.commandValue);
    if (parts.empty()) return std::nullopt;
    auto const& first = criteria.front();
    if (first.name == "add") {
        if (parts.size() != 4 || (parts[2] != "add" && parts[2] != "minus")) return std::nullopt;
        auto value = Number(parts[3]);
        if (!value) return std::nullopt;
        parts[2] = Pretty(parts[2] == "minus" ? -*value : *value);
        parts.resize(3);
    } else if (first.name == "seek") {
        if (parts.size() != 4 || (parts[1] != "forward" && parts[1] != "backward" && parts[1] != "seek-to"))
            return std::nullopt;
        auto value = Number(parts[2]);
        if (!value) return std::nullopt;
        parts[2] = Pretty(parts[1] == "backward" ? -*value : *value);
        parts.erase(parts.begin() + 1);
        if (parts.back() == "relative") parts.pop_back();
    } else if (first.name == "cycle" && parts.size() >= 2 &&
               (parts[1] == "flip" || parts[1] == "mirror")) {
        return "@iina toggle-" + parts[1];
    }
    if (first.applicationCommand) parts.insert(parts.begin(), "@iina");
    return Join(parts);
}
}

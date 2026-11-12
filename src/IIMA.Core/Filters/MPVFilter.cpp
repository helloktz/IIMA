// Derived from IINA iina/MPVFilter.swift, copyright (c) 2016 lhc.
// Distributed under GNU GPL v3; see references/iina/LICENSE and iina-source.txt.
#include "MPVFilter.h"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <locale>
#include <sstream>
#include <utility>

namespace iima {
namespace {
bool IsNameCharacter(char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_' || value == '-';
}

std::string OptionalNumber(std::optional<int> value) {
    return value ? std::to_string(*value) : std::string{};
}

std::string FloatString(float value) {
    // Locale independent and shortest round-trip representation. Using a
    // stream fallback keeps even out-of-range/nonfinite input visible to mpv.
    char buffer[64];
    auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    if (result.ec == std::errc{}) return std::string(buffer, result.ptr);
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << value;
    return stream.str();
}
}

std::optional<MPVFilter::Type> MPVFilter::TypeForName(std::string_view name) noexcept {
    if (name == "crop") return Type::Crop;
    if (name == "expand") return Type::Expand;
    if (name == "flip") return Type::Flip;
    if (name == "hflip") return Type::Mirror;
    if (name == "lavfi") return Type::Lavfi;
    return std::nullopt;
}

std::vector<std::string_view> MPVFilter::ParameterOrder(std::optional<Type> type) {
    if (type == Type::Crop) return {"w", "h", "x", "y"};
    if (type == Type::Expand) return {"w", "h", "x", "y", "aspect", "round"};
    return {};
}

MPVFilter::MPVFilter(std::string name, std::optional<std::string> label,
                     std::optional<Parameters> parameters)
    : name_(std::move(name)), label_(std::move(label)), type_(TypeForName(name_)),
      parameters_(std::move(parameters)) {
    auto const order = ParameterOrder(type_);
    if (!parameters_ || order.empty()) return;
    Parameters translated;
    for (auto const& [key, value] : *parameters_) {
        // mpv node maps use @0, @1 ... for positional arguments. Unlike the
        // Swift implementation, do not interpret arbitrary 'x0' as position 0
        // and never index past the known positional argument list.
        std::size_t position{};
        if (key.size() > 1 && key.front() == '@') {
            auto result = std::from_chars(key.data() + 1, key.data() + key.size(), position);
            if (result.ec == std::errc{} && result.ptr == key.data() + key.size() &&
                position < order.size()) {
                translated[std::string(order[position])] = value;
                continue;
            }
        }
        translated[key] = value;
    }
    parameters_ = std::move(translated);
}

MPVFilter MPVFilter::WithRawParameters(std::string name,
    std::optional<std::string> label, std::string parameters) {
    MPVFilter result(std::move(name), std::move(label), std::nullopt);
    result.rawParameters_ = std::move(parameters);
    return result;
}

std::optional<MPVFilter> MPVFilter::Parse(std::string_view text) {
    if (text.empty() || text.find('\0') != std::string_view::npos) return std::nullopt;
    auto const equal = text.find('=');
    auto name = text.substr(0, equal);
    std::optional<std::string> label;
    if (name.starts_with('@')) {
        auto const colon = name.find(':');
        if (colon == std::string_view::npos || colon <= 1 || colon + 1 == name.size())
            return std::nullopt;
        label = std::string(name.substr(1, colon - 1));
        name.remove_prefix(colon + 1);
    }
    if (name.empty()) return std::nullopt;
    MPVFilter result(std::string(name), std::move(label), std::nullopt);
    if (equal != std::string_view::npos) {
        result.rawParameters_ = std::string(text.substr(equal + 1));
        result.parameters_ = ParseParameters(name, *result.rawParameters_);
    }
    return result;
}

MPVFilter MPVFilter::Crop(std::optional<int> width, std::optional<int> height,
                          std::optional<int> x, std::optional<int> y) {
    return MPVFilter("crop", std::nullopt, Parameters{
        {"w", OptionalNumber(width)}, {"h", OptionalNumber(height)},
        {"x", OptionalNumber(x)}, {"y", OptionalNumber(y)}});
}

MPVFilter MPVFilter::Flip() { return MPVFilter("vflip", std::nullopt, std::nullopt); }
MPVFilter MPVFilter::Mirror() { return MPVFilter("hflip", std::nullopt, std::nullopt); }

MPVFilter MPVFilter::Unsharp(float amount, int matrixSize) {
    auto const size = std::to_string(matrixSize);
    auto const strength = FloatString(amount);
    return Lavfi("unsharp", std::nullopt,
                 std::vector<std::string>{size, size, strength, size, size, strength});
}

MPVFilter MPVFilter::Lavfi(std::string_view name,
    std::optional<std::string> label, std::vector<std::string> const& parameters) {
    std::string graph = "[" + std::string(name) + "=";
    bool first = true;
    for (auto const& value : parameters) {
        if (!first) graph += ':';
        first = false;
        graph += value;
    }
    graph += ']';
    return WithRawParameters("lavfi", std::move(label), std::move(graph));
}

MPVFilter MPVFilter::Lavfi(std::string_view name,
    std::optional<std::string> label, Parameters const& parameters) {
    std::vector<std::string> entries;
    entries.reserve(parameters.size());
    for (auto const& [key, value] : parameters) entries.push_back(key + "=" + value);
    return Lavfi(name, std::move(label), entries);
}

std::string MPVFilter::QuoteValue(std::string_view value) {
    if (std::all_of(value.begin(), value.end(), IsNameCharacter)) return std::string(value);
    return "%" + std::to_string(value.size()) + "%" + std::string(value);
}

std::string MPVFilter::StringFormat() const {
    std::string result;
    if (label_) result = "@" + *label_ + ":";
    result += name_;
    if (rawParameters_) return result + "=" + *rawParameters_;
    if (!parameters_ || parameters_->empty()) return result;
    auto const order = ParameterOrder(type_);
    if (!order.empty()) {
        result += '=';
        bool first = true;
        for (auto const key : order) {
            if (!first) result += ':';
            first = false;
            auto const value = parameters_->find(key);
            if (value != parameters_->end()) result += value->second;
        }
        return result;
    }
    if (name_ == "lavfi") {
        auto const graph = parameters_->find("graph");
        // Missing graph is malformed input, not permission to dereference end.
        // Preserve the named parameters for mpv to reject with useful context.
        if (graph != parameters_->end()) return result + "=[" + graph->second + "]";
    }
    result += '=';
    bool first = true;
    for (auto const& [key, value] : *parameters_) {
        if (!first) result += ':';
        first = false;
        result += key + "=" + QuoteValue(value);
    }
    return result;
}

std::optional<MPVFilter::Parameters> MPVFilter::ParseParameters(
    std::string_view name, std::string_view parameters) {
    if (name == "crop" || name == "flip" || name == "hflip" || name == "lavfi")
        return std::nullopt;
    Parameters parsed;
    std::size_t count = 0;
    // Do not split before reading %n% strings: a quoted value can contain ':'
    // or '='. The upstream split-based parser cannot handle that case.
    while (!parameters.empty()) {
        auto const equal = parameters.find('=');
        auto const colon = parameters.find(':');
        if (equal == std::string_view::npos || equal == 0 || colon < equal)
            return std::nullopt;
        auto const key = parameters.substr(0, equal);
        parameters.remove_prefix(equal + 1);
        std::string_view value;
        if (parameters.starts_with('%')) {
            auto const end = parameters.find('%', 1);
            if (end == std::string_view::npos || end == 1) return std::nullopt;
            std::size_t length{};
            auto const result = std::from_chars(parameters.data() + 1,
                                                parameters.data() + end, length);
            if (result.ec != std::errc{} || result.ptr != parameters.data() + end)
                return std::nullopt;
            parameters.remove_prefix(end + 1);
            if (length > parameters.size()) return std::nullopt;
            value = parameters.substr(0, length);
            parameters.remove_prefix(length);
            if (!parameters.empty() && parameters.front() != ':') return std::nullopt;
        } else {
            auto const delimiter = parameters.find(':');
            value = parameters.substr(0, delimiter);
            parameters.remove_prefix(value.size());
        }
        parsed[std::string(key)] = std::string(value);
        ++count;
        if (!parameters.empty()) parameters.remove_prefix(1);
    }
    // Preserve upstream's single-parameter textual comparison rule.
    if (count < 2) return std::nullopt;
    return parsed;
}

bool MPVFilter::operator==(MPVFilter const& other) const {
    if (name_ != other.name_ || label_ != other.label_) return false;
    if (parameters_ && other.parameters_) return *parameters_ == *other.parameters_;
    return StringFormat() == other.StringFormat();
}
}

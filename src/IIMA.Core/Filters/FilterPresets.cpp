// Derived from IINA FilterPresets.swift, copyright (c) 2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#include "FilterPresets.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace iima {
std::optional<std::string> BuildLut3dGraph(std::string_view path,std::string_view interpolation) {
    if (path.empty() || path.size()>16384 || path.find('\0')!=path.npos ||
        (interpolation!="nearest" && interpolation!="trilinear" && interpolation!="tetrahedral")) return {};
    auto escape = [](std::string_view value, std::string_view special) {
        std::string result;
        for (char c : value) {
            if (special.find(c) != std::string_view::npos) result += '\\';
            result += c;
        }
        return result;
    };
    auto option = escape(path, "\\': \t\r\n");
    auto graphValue = escape(option, "\\'[],; \t\r\n");
    return "lut3d=file=" + graphValue + ":interp=" + std::string(interpolation);
}

std::string FilterParameterValue::StringValue() const {
    if (auto value = std::get_if<std::string>(&value_)) return *value;
    if (auto value = std::get_if<int>(&value_)) return std::to_string(*value);
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), std::get<float>(value_));
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
}
int FilterParameterValue::IntValue() const noexcept {
    auto value = std::get_if<int>(&value_);
    return value ? *value : 0;
}
float FilterParameterValue::FloatValue() const noexcept {
    auto value = std::get_if<float>(&value_);
    return value ? *value : 0;
}
FilterParameter FilterParameter::Text(std::string value) {
    return {Type::Text, FilterParameterValue(std::move(value))};
}
FilterParameter FilterParameter::Integer(int minimum, int maximum, int increment, int value) {
    if (minimum > maximum || increment < 1) throw std::invalid_argument("Invalid integer filter schema");
    FilterParameter result{Type::Integer, FilterParameterValue(value)};
    result.minimumInt = minimum;
    result.maximumInt = maximum;
    result.step = increment;
    if (!result.Accepts(result.defaultValue)) throw std::invalid_argument("Invalid integer filter default");
    return result;
}
FilterParameter FilterParameter::Float(float minimum, float maximum, float value) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
        throw std::invalid_argument("Invalid float filter schema");
    FilterParameter result{Type::Float, FilterParameterValue(value)};
    result.minimum = minimum;
    result.maximum = maximum;
    if (!result.Accepts(result.defaultValue)) throw std::invalid_argument("Invalid float filter default");
    return result;
}
FilterParameter FilterParameter::Choice(std::vector<std::string> choices, std::size_t index) {
    if (index >= choices.size()) throw std::invalid_argument("Invalid filter choice default");
    FilterParameter result{Type::Choice, FilterParameterValue(choices[index])};
    result.choices = std::move(choices);
    return result;
}
bool FilterParameter::Accepts(FilterParameterValue const& value) const {
    switch (type) {
    case Type::Text: return value.IsText();
    case Type::Integer:
        return value.IsInteger() && value.IntValue() >= minimumInt &&
            value.IntValue() <= maximumInt && step > 0 &&
            (static_cast<std::int64_t>(value.IntValue()) - minimumInt) % step == 0;
    case Type::Float:
        return value.IsFloat() && std::isfinite(value.FloatValue()) &&
            value.FloatValue() >= minimum && value.FloatValue() <= maximum;
    case Type::Choice:
        if (!value.IsText()) return false;
        return std::find(choices.begin(), choices.end(), value.StringValue()) != choices.end();
    }
    return false;
}
std::string FilterPreset::LocalizedName(MPVFilter::Parameters const& translations) const {
    auto found = translations.find(name);
    return found == translations.end() ? name : found->second;
}
std::string FilterPreset::LocalizedParameterName(std::string_view parameter,
    MPVFilter::Parameters const& translations) const {
    auto found = translations.find(name + "." + std::string(parameter));
    return found == translations.end() ? std::string(parameter) : found->second;
}
std::vector<FilterPreset> FilterPreset::AudioPresets() {
    using P = FilterParameter;
    return {
        {"custom_mpv", {{"name", P::Text()}, {"string", P::Text()}},
         {"name", "string"}, Transform::CustomMpv},
        {"custom_ffmpeg", {{"name", P::Text()}, {"string", P::Text()}},
         {"name", "string"}, Transform::CustomFfmpeg}
    };
}
std::vector<FilterPreset> FilterPreset::VideoPresets() {
    using P = FilterParameter;
    std::vector<FilterPreset> result{
        {"crop", {{"x", P::Text()}, {"y", P::Text()}, {"w", P::Text()}, {"h", P::Text()}},
         {"w", "h", "x", "y"}, Transform::Mpv},
        {"expand", {{"x", P::Text()}, {"y", P::Text()}, {"w", P::Text()}, {"h", P::Text()},
                    {"aspect", P::Text("0")}, {"round", P::Text("1")}},
         {"w", "h", "x", "y", "aspect", "round"}, Transform::Mpv},
        // IINA #4259: 13, not 23, respects FFmpeg's combined matrix-size limit.
        {"sharpen", {{"amount", P::Float(0, 1.5f)}, {"msize", P::Integer(3, 13, 2, 5)}},
         {"msize", "amount"}, Transform::Sharpen},
        {"blur", {{"amount", P::Float(0, 1.5f)}, {"msize", P::Integer(3, 13, 2, 5)}},
         {"msize", "amount"}, Transform::Blur},
        {"delogo", {{"x", P::Text("1")}, {"y", P::Text("1")},
                    {"w", P::Text("1")}, {"h", P::Text("1")}}, {"x", "y", "w", "h"}},
        {"negative", {}, {}, Transform::Negative},
        {"vflip", {}, {}, Transform::Mpv},
        {"hflip", {}, {}, Transform::Mpv},
        {"lut3d", {{"file", P::Text()},
                   {"interp", P::Choice({"nearest", "trilinear", "tetrahedral"})}}, {"file", "interp"}}
    };
    for (auto& custom : AudioPresets()) result.push_back(std::move(custom));
    return result;
}
bool FilterPresetInstance::Set(std::string_view name, FilterParameterValue value) {
    auto const schema = preset_.parameters.find(name);
    if (schema == preset_.parameters.end() || !schema->second.Accepts(value)) return false;
    values_.insert_or_assign(std::string(name), std::move(value));
    return true;
}
bool FilterPresetInstance::Reset(std::string_view name) {
    if (!preset_.parameters.contains(name)) return false;
    auto const value = values_.find(name);
    if (value != values_.end()) values_.erase(value);
    return true;
}
FilterParameterValue const* FilterPresetInstance::Value(std::string_view name) const noexcept {
    auto value = values_.find(name);
    if (value != values_.end()) return &value->second;
    auto schema = preset_.parameters.find(name);
    return schema == preset_.parameters.end() ? nullptr : &schema->second.defaultValue;
}
std::optional<MPVFilter> FilterPresetInstance::CreateFilter() const {
    // Resolve defaults here, not only after a UI control has emitted a change.
    // This also makes untouched presets deterministic when invoked headlessly.
    MPVFilter::Parameters parameters;
    for (auto const& [name, schema] : preset_.parameters) {
        auto const value = Value(name);
        if (!value || !schema.Accepts(*value)) return std::nullopt;
        parameters[name] = value->StringValue();
    }
    using T = FilterPreset::Transform;
    switch (preset_.transform) {
    case T::Mpv: return MPVFilter(preset_.name, std::nullopt, std::move(parameters));
    case T::Lavfi: {
        if (preset_.name != "lut3d")
            return MPVFilter::Lavfi(preset_.name, std::nullopt, parameters);
        auto graph=BuildLut3dGraph(parameters.at("file"),parameters.at("interp"));
        if (!graph) return {};
        // Do not bracket-quote: a filename may itself contain square brackets.
        // m_option.c read_subparam consumes an exact UTF-8 byte length.
        return MPVFilter::WithRawParameters("lavfi", std::nullopt,
                                            "graph=" + MPVFilter::QuoteValue(*graph));
    }
    case T::Sharpen:
    case T::Blur: {
        auto amount = Value("amount");
        auto size = Value("msize");
        if (!amount || !size) return std::nullopt;
        return MPVFilter::Unsharp(amount->FloatValue() * (preset_.transform == T::Blur ? -1 : 1), size->IntValue());
    }
    case T::Negative:
        return MPVFilter::Lavfi("lutrgb", std::nullopt,
            MPVFilter::Parameters{{"r", "negval"}, {"g", "negval"}, {"b", "negval"}});
    case T::CustomMpv:
    case T::CustomFfmpeg: {
        auto name = parameters.find("name");
        auto raw = parameters.find("string");
        if (name == parameters.end() || raw == parameters.end() || name->second.empty()) return std::nullopt;
        if (preset_.transform == T::CustomMpv) return MPVFilter::Parse(name->second + "=" + raw->second);
        return MPVFilter::WithRawParameters("lavfi", std::nullopt,
                                            "[" + name->second + "=" + raw->second + "]");
    }
    }
    return std::nullopt;
}
}

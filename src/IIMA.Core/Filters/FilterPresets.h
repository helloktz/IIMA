// Derived from IINA FilterPresets.swift, copyright (c) 2017 lhc.
// GNU GPL v3; see references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include "MPVFilter.h"
#include <cstdint>
#include <variant>
#include <utility>

namespace iima {
class FilterParameterValue final {
public:
    explicit FilterParameterValue(std::string value) : value_(std::move(value)) {}
    explicit FilterParameterValue(int value) : value_(value) {}
    explicit FilterParameterValue(float value) : value_(value) {}
    std::string StringValue() const;
    int IntValue() const noexcept;
    float FloatValue() const noexcept;
    bool IsText() const noexcept { return std::holds_alternative<std::string>(value_); }
    bool IsInteger() const noexcept { return std::holds_alternative<int>(value_); }
    bool IsFloat() const noexcept { return std::holds_alternative<float>(value_); }
private:
    std::variant<std::string, int, float> value_;
};

struct FilterParameter {
    enum class Type { Text, Integer, Float, Choice };
    Type type;
    FilterParameterValue defaultValue;
    float minimum{}, maximum{};
    int minimumInt{}, maximumInt{}, step{1};
    std::vector<std::string> choices;
    static FilterParameter Text(std::string value = {});
    static FilterParameter Integer(int minimum, int maximum, int step = 1, int value = 0);
    static FilterParameter Float(float minimum, float maximum, float value = 0);
    static FilterParameter Choice(std::vector<std::string> choices, std::size_t index = 0);
    bool Accepts(FilterParameterValue const& value) const;
};

struct FilterPreset {
    enum class Transform { Lavfi, Mpv, Sharpen, Blur, Negative, CustomMpv, CustomFfmpeg };
    std::string name;
    std::map<std::string, FilterParameter, std::less<>> parameters;
    std::vector<std::string> parameterOrder;
    Transform transform{Transform::Lavfi};
    // Inject Windows resource strings, rather than loading Cocoa .strings.
    std::string LocalizedName(MPVFilter::Parameters const& translations) const;
    std::string LocalizedParameterName(std::string_view parameter,
                                      MPVFilter::Parameters const& translations) const;
    static std::vector<FilterPreset> VideoPresets();
    static std::vector<FilterPreset> AudioPresets();
};

class FilterPresetInstance final {
public:
    explicit FilterPresetInstance(FilterPreset preset) : preset_(std::move(preset)) {}
    FilterPreset const& Preset() const noexcept { return preset_; }
    // Unknown fields, wrong types, out-of-range values and invalid integer
    // slider increments are rejected without modifying the instance.
    bool Set(std::string_view name, FilterParameterValue value);
    bool Reset(std::string_view name);
    FilterParameterValue const* Value(std::string_view name) const noexcept;
    std::optional<MPVFilter> CreateFilter() const;
private:
    FilterPreset preset_;
    std::map<std::string, FilterParameterValue, std::less<>> values_;
};
}

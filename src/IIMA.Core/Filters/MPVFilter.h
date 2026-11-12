// Derived from IINA iina/MPVFilter.swift, copyright (c) 2016 lhc.
// Distributed under GNU GPL v3; see references/iina/LICENSE and iina-source.txt.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iima {
// A filter is a value, not an mpv resource. Strings at this boundary are UTF-8.
class MPVFilter final {
public:
    enum class Type { Crop, Expand, Flip, Mirror, Lavfi };
    using Parameters = std::map<std::string, std::string, std::less<>>;

    MPVFilter(std::string name, std::optional<std::string> label,
              std::optional<Parameters> parameters);
    static MPVFilter WithRawParameters(std::string name,
        std::optional<std::string> label, std::string parameters);
    // A malformed label/name is rejected. Unrecognised parameter syntax is
    // retained verbatim and compared textually, rather than silently dropped.
    static std::optional<MPVFilter> Parse(std::string_view filter);

    static MPVFilter Crop(std::optional<int> width, std::optional<int> height,
                          std::optional<int> x, std::optional<int> y);
    static MPVFilter Flip();
    static MPVFilter Mirror();
    // As upstream, range validation belongs to the caller/preset controls.
    static MPVFilter Unsharp(float amount, int matrixSize = 5);
    static MPVFilter Lavfi(std::string_view name,
        std::optional<std::string> label, std::vector<std::string> const& parameters);
    static MPVFilter Lavfi(std::string_view name,
        std::optional<std::string> label, Parameters const& parameters);

    std::string const& Name() const noexcept { return name_; }
    std::optional<std::string> const& Label() const noexcept { return label_; }
    std::optional<Type> FilterType() const noexcept { return type_; }
    std::optional<Parameters> const& Params() const noexcept { return parameters_; }
    std::optional<std::string> const& RawParameters() const noexcept { return rawParameters_; }
    std::string StringFormat() const;
    bool operator==(MPVFilter const& other) const;

    // mpv's %BYTECOUNT%STRING format, not Swift's grapheme count. No shell
    // escaping is involved: the returned value is passed to the mpv C API.
    static std::string QuoteValue(std::string_view value);

private:
    static std::optional<Type> TypeForName(std::string_view name) noexcept;
    static std::vector<std::string_view> ParameterOrder(std::optional<Type> type);
    static std::optional<Parameters> ParseParameters(std::string_view name,
                                                    std::string_view parameters);
    std::string name_;
    std::optional<std::string> label_;
    std::optional<Type> type_;
    std::optional<Parameters> parameters_;
    std::optional<std::string> rawParameters_;
};
}

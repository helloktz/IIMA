// Query layer for the IINA mpv named-constant catalog. GPL v3.
// Source/version/license: references/iina-source.txt, references/iina/LICENSE.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iima::mpv_catalog {
enum class Kind { Command, Option, Property };
enum class Argument { None, Index, Name };
struct Symbol {
    Kind kind;
    std::string_view category; // IINA option group; empty for commands/properties
    std::string_view name; // Exact Swift identifier, even when C++ requires suffix _
    std::string_view value; // Exact wire value, or {} interpolation for dynamic property
    std::string_view syntax; // Original generated declaration's protocol hint
    Argument argument;
};
struct SearchFilter {
    std::optional<Kind> kind;
    std::string_view category;
    std::size_t limit{100};
};
// Static storage, no DLL calls. Pointer/span validity is the application lifetime.
std::span<const Symbol> AllSymbols() noexcept;
std::string QualifiedName(Symbol const& symbol);
const Symbol* FindSymbol(Kind kind, std::string_view name,
                         std::string_view category = {}) noexcept;
std::vector<const Symbol*> FindLiteral(Kind kind, std::string_view value);
// ASCII case-insensitive substring search over identifiers, category, wire value
// and source syntax. No regex execution, scripting or locale-dependent ordering.
std::vector<const Symbol*> SearchSymbols(std::string_view query,
                                        SearchFilter const& filter = {});
// Dynamic property helpers preserve Swift interpolation, including negative
// indices. A negative index may be rejected by mpv; it is not silently clamped.
std::optional<std::string> ExpandProperty(Symbol const& symbol, std::int64_t index);
std::optional<std::string> ExpandProperty(Symbol const& symbol, std::string_view name);
// Recognizes a concrete property path, returning the template and argument.
// This is a syntactic catalog lookup, not a runtime availability check.
struct PropertyPathMatch {
    const Symbol* symbol{};
    std::string argument;
};
std::optional<PropertyPathMatch> MatchPropertyPath(std::string_view path);
}

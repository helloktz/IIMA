// Lookup and search over pinned IINA MPVCommand/MPVOption/MPVProperty data.
// GPL v3; references/iina/LICENSE. Does not authorize or execute any command.
#include "Catalog.h"
#include "MPVCommand.h"
#include "MPVOption.h"
#include "MPVProperty.h"
#include <algorithm>
#include <charconv>
#include <iterator>

namespace iima::mpv_catalog {
namespace {
constexpr Symbol symbols[]{
#include "CatalogData.inc"
};
constexpr char Fold(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
bool Contains(std::string_view haystack, std::string_view needle) noexcept {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) { return Fold(a) == Fold(b); }) != haystack.end();
}
std::optional<std::string> Interpolate(Symbol const& symbol, std::string_view argument) {
    auto split = symbol.value.find("{}");
    if (split == std::string_view::npos || argument.find('\0') != std::string_view::npos) return {};
    std::string result(symbol.value.substr(0, split));
    result.append(argument);
    result.append(symbol.value.substr(split + 2));
    return result;
}
std::string_view Container(Kind kind) noexcept {
    switch (kind) {
    case Kind::Command: return "MPVCommand";
    case Kind::Option: return "MPVOption";
    case Kind::Property: return "MPVProperty";
    }
    return {};
}
}
std::span<const Symbol> AllSymbols() noexcept { return symbols; }
std::string QualifiedName(Symbol const& symbol) {
    auto result = std::string(Container(symbol.kind));
    if (!symbol.category.empty()) result += "." + std::string(symbol.category);
    return result + "." + std::string(symbol.name);
}
const Symbol* FindSymbol(Kind kind, std::string_view name, std::string_view category) noexcept {
    auto found = std::find_if(std::begin(symbols), std::end(symbols), [&](auto const& symbol) {
        return symbol.kind == kind && symbol.name == name && symbol.category == category;
    });
    return found == std::end(symbols) ? nullptr : found;
}
std::vector<const Symbol*> FindLiteral(Kind kind, std::string_view value) {
    std::vector<const Symbol*> result;
    for (auto const& symbol : symbols)
        if (symbol.kind == kind && symbol.argument == Argument::None && symbol.value == value)
            result.push_back(&symbol);
    return result;
}
std::vector<const Symbol*> SearchSymbols(std::string_view query, SearchFilter const& filter) {
    std::vector<const Symbol*> result;
    if (!filter.limit) return result;
    for (auto const& symbol : symbols) {
        if (filter.kind && symbol.kind != *filter.kind) continue;
        if (!filter.category.empty() && symbol.category != filter.category) continue;
        if (!query.empty() && !Contains(symbol.name, query) && !Contains(symbol.value, query) &&
            !Contains(symbol.category, query) && !Contains(symbol.syntax, query)) continue;
        result.push_back(&symbol);
        if (result.size() >= filter.limit) break;
    }
    return result;
}
std::optional<std::string> ExpandProperty(Symbol const& symbol, std::int64_t index) {
    if (symbol.kind != Kind::Property || symbol.argument != Argument::Index) return {};
    return Interpolate(symbol, std::to_string(index));
}
std::optional<std::string> ExpandProperty(Symbol const& symbol, std::string_view name) {
    if (symbol.kind != Kind::Property || symbol.argument != Argument::Name) return {};
    return Interpolate(symbol, name);
}
std::optional<PropertyPathMatch> MatchPropertyPath(std::string_view path) {
    if (path.find('\0') != std::string_view::npos) return {};
    // Literal properties take precedence over general option/name patterns.
    for (auto const& symbol : symbols)
        if (symbol.kind == Kind::Property && symbol.argument == Argument::None && symbol.value == path)
            return PropertyPathMatch{&symbol, {}};
    std::optional<PropertyPathMatch> best;
    std::size_t bestSpecificity{};
    for (auto const& symbol : symbols) {
        if (symbol.kind != Kind::Property || symbol.argument == Argument::None) continue;
        auto split = symbol.value.find("{}");
        if (split == std::string_view::npos) continue;
        auto prefix = symbol.value.substr(0, split);
        auto suffix = symbol.value.substr(split + 2);
        if (path.size() < prefix.size() + suffix.size() || !path.starts_with(prefix) || !path.ends_with(suffix)) continue;
        auto argument = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
        if (symbol.argument == Argument::Index) {
            std::int64_t index{};
            auto [end, error] = std::from_chars(argument.data(), argument.data() + argument.size(), index);
            if (error != std::errc{} || end != argument.data() + argument.size()) continue;
        }
        auto specificity = prefix.size() + suffix.size();
        if (!best || specificity > bestSpecificity) {
            best = PropertyPathMatch{&symbol, std::string(argument)};
            bestSpecificity = specificity;
        }
    }
    return best;
}
}

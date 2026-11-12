// Derived from IINA FileGroup.swift / ObjcUtils.m, Copyright (c) 2017 lhc.
// GPL-3.0; source/version/license: references/iina-source.txt, references/iina/LICENSE.
#include "FileGroup.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace iima {
void CheckMatchingCancelled(const MatchCancellation& cancelled) {
    if (cancelled && cancelled()) throw MatchingCancelled{};
}

std::u32string DecodeMatchUtf8(std::string_view value) {
    std::u32string output;
    for (std::size_t at = 0; at < value.size();) {
        const auto first = static_cast<unsigned char>(value[at]);
        std::size_t length = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
                             first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        char32_t scalar = length == 1 ? first : length == 2 ? first & 0x1f :
                          length == 3 ? first & 0x0f : first & 7;
        bool valid = length != 0 && length <= value.size() - at;
        for (std::size_t offset = 1; valid && offset < length; ++offset) {
            const auto next = static_cast<unsigned char>(value[at + offset]);
            valid = (next & 0xc0) == 0x80;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        valid = valid && !(length == 2 && scalar < 0x80) && !(length == 3 && scalar < 0x800) &&
                !(length == 4 && scalar < 0x10000) && scalar <= 0x10ffff &&
                !(scalar >= 0xd800 && scalar <= 0xdfff);
        output.push_back(valid ? scalar : U'\ufffd');
        at += valid ? length : 1;
    }
    return output;
}

MatchFile::MatchFile(std::filesystem::path value) : path(std::move(value)) {
    const auto utf8 = path.stem().u8string();
    filename = DecodeMatchUtf8(std::string_view(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
    suffix = filename;
}

namespace {
bool Decimal(char32_t value) {
    // Common decimal blocks used by local episode names. Other Unicode Nd
    // blocks remain literal; this does not pretend to be a Unicode database.
    return (value >= U'0' && value <= U'9') || (value >= 0xff10 && value <= 0xff19) ||
           (value >= 0x0660 && value <= 0x0669) || (value >= 0x06f0 && value <= 0x06f9);
}
char32_t Fold(char32_t value) {
    return value >= U'A' && value <= U'Z' ? value - U'A' + U'a' : value;
}
bool StopGrouping(const std::vector<char32_t>& chars) {
    std::size_t chinese = 0;
    constexpr std::u32string_view numbers = U"零一二三四五六七八九十";
    for (const auto value : chars) {
        if (value >= U'0' && value <= U'9') return true;
        if (numbers.find(value) != std::u32string_view::npos && ++chinese >= 3) return true;
    }
    return false;
}

void Group(std::u32string prefix, const std::vector<std::size_t>& indices,
           std::vector<MatchFile>& files, FileGroups& output,
           const MatchCancellation& cancelled) {
    CheckMatchingCancelled(cancelled);
    if (indices.size() < 3) {
        // IINA does not call the FileInfo prefix setter for a small group.
        output.emplace(std::move(prefix), indices);
        return;
    }
    FileGroups groups;
    std::vector<char32_t> chars;
    auto at = prefix.size();
    while (groups.size() < 2) {
        CheckMatchingCancelled(cancelled);
        auto lastPrefix = prefix;
        bool processed = false;
        for (const auto index : indices) {
            const auto& name = files[index].filename;
            if (at >= name.size()) {
                groups[prefix].push_back(index);
                chars.push_back(U'/');
                continue;
            }
            auto childPrefix = prefix + name[at];
            lastPrefix = childPrefix;
            auto [position, inserted] = groups.try_emplace(childPrefix);
            if (inserted) chars.push_back(name[at]);
            position->second.push_back(index);
            processed = true;
        }
        if (groups.size() == 1) {
            prefix = std::move(lastPrefix);
            groups.clear();
            chars.clear();
        }
        ++at;
        if (!processed) break;
    }
    std::size_t largest = 0;
    for (const auto& [key, values] : groups) largest = std::max(largest, values.size());
    if (StopGrouping(chars) || largest < 3) {
        for (const auto index : indices) files[index].SetPrefix(prefix);
        output.emplace(std::move(prefix), indices);
    } else {
        for (const auto& [key, values] : groups) Group(key, values, files, output, cancelled);
    }
}
} // namespace

void MatchFile::SetPrefix(std::u32string value) {
    nameInSeries.reset();
    if (value.size() >= filename.size()) {
        prefix.clear();
        suffix = filename;
        return;
    }
    prefix = std::move(value);
    suffix = filename.substr(prefix.size());
    bool foundDigit = false;
    std::size_t length = 0;
    for (const auto character : suffix) {
        if (Decimal(character)) foundDigit = true;
        else if (foundDigit) break;
        ++length;
    }
    nameInSeries = suffix.substr(0, length);
}

FileGroups GroupSeries(std::vector<MatchFile>& files, const MatchCancellation& cancelled) {
    for (auto& file : files) {
        file.prefix.clear();
        file.suffix = file.filename;
        file.nameInSeries.reset();
    }
    std::vector<std::size_t> indices(files.size());
    std::iota(indices.begin(), indices.end(), 0);
    FileGroups result;
    Group({}, indices, files, result, cancelled);
    return result;
}

std::size_t FilenameDistance(std::u32string_view left, std::u32string_view right,
                             const MatchCancellation& cancelled) {
    if (left.size() < right.size()) std::swap(left, right);
    std::vector<std::size_t> previous(right.size() + 1), current(right.size() + 1);
    std::iota(previous.begin(), previous.end(), 0);
    for (std::size_t row = 0; row < left.size(); ++row) {
        CheckMatchingCancelled(cancelled);
        current[0] = row + 1;
        for (std::size_t column = 0; column < right.size(); ++column) {
            current[column + 1] = std::min({previous[column + 1] + 1, current[column] + 1,
                                          previous[column] + (left[row] == right[column] ? 0 : 4)});
        }
        previous.swap(current);
    }
    return previous.back();
}

bool NaturalFilenameLess(std::u32string_view left, std::u32string_view right) {
    std::size_t a = 0, b = 0;
    auto digit = [](char32_t c) { return c >= U'0' && c <= U'9'; };
    while (a < left.size() && b < right.size()) {
        if (digit(left[a]) && digit(right[b])) {
            const auto beginA = a, beginB = b;
            while (a < left.size() && digit(left[a])) ++a;
            while (b < right.size() && digit(right[b])) ++b;
            auto firstA = beginA, firstB = beginB;
            while (firstA + 1 < a && left[firstA] == U'0') ++firstA;
            while (firstB + 1 < b && right[firstB] == U'0') ++firstB;
            if (a - firstA != b - firstB) return a - firstA < b - firstB;
            const auto cmp = left.substr(firstA, a - firstA).compare(right.substr(firstB, b - firstB));
            if (cmp) return cmp < 0;
        } else {
            const auto lhs = Fold(left[a++]), rhs = Fold(right[b++]);
            if (lhs != rhs) return lhs < rhs;
        }
    }
    if (a != left.size() || b != right.size()) return a == left.size();
    return left < right; // deterministic zero padding and case tie-break
}
} // namespace iima

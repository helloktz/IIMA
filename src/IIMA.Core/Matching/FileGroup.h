// Derived from IINA FileGroup.swift / ObjcUtils.m, Copyright (c) 2017 lhc.
// GPL-3.0; source/version/license: references/iina-source.txt, references/iina/LICENSE.
#pragma once
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iima {
using MatchCancellation = std::function<bool()>;
struct MatchingCancelled {};
void CheckMatchingCancelled(const MatchCancellation& cancelled);

struct MatchFile {
    std::filesystem::path path;
    std::u32string filename;
    std::u32string prefix;
    std::u32string suffix;
    std::optional<std::u32string> nameInSeries;
    explicit MatchFile(std::filesystem::path value);
    void SetPrefix(std::u32string value);
};
using FileGroups = std::map<std::u32string, std::vector<std::size_t>>;
FileGroups GroupSeries(std::vector<MatchFile>& files, const MatchCancellation& cancelled = {});
// Matches ObjcUtils weights: insert/delete = 1, substitution = 4.
std::size_t FilenameDistance(std::u32string_view left, std::u32string_view right,
                             const MatchCancellation& cancelled = {});
// Deterministic numeric ordering, independent of the cloud machine's locale.
// This replaces localizedStandardCompare; it is not a macOS collation clone.
bool NaturalFilenameLess(std::u32string_view left, std::u32string_view right);
std::u32string DecodeMatchUtf8(std::string_view value);
} // namespace iima

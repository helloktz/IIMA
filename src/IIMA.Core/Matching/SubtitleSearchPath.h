// IINA AutoFileMatcher.getAllPossibleSubs, Copyright (c) 2017 lhc.
// GPL-3.0; references/iina/LICENSE. Windows uses ';', not drive-letter ':'.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace iima {
struct SubtitleSearchPath {
    std::wstring directory;
    bool immediateChildren{};
};
// Syntax only: no filesystem access. Empty items are ignored. '~' expansion
// and wildcards other than one trailing directory /* are deliberately unsupported.
std::optional<std::vector<SubtitleSearchPath>> ParseSubtitleSearchPaths(
    std::wstring_view text, std::wstring& error);
}

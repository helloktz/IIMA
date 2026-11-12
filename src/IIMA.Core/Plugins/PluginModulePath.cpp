// Derived from IINA JavascriptPolyfill.require/currentFile directory resolution.
// Copyright (c) 2018-2020 lhc and IINA contributors. GPL-3.0.
#include "PluginModulePath.h"
#include "PluginManifest.h"
#include <algorithm>
#include <vector>

namespace iima::plugins {
std::optional<std::string> ResolvePluginModulePath(
    std::string_view currentFile, std::string_view requested) {
    auto current = NormalizePluginRelativePath(currentFile);
    if (!current || requested.empty() || requested.size() > 4096 ||
        requested.front() == '/' || requested.front() == '\\' ||
        requested.back() == '/' || requested.back() == '\\') return std::nullopt;
    if (std::any_of(requested.begin(), requested.end(), [](unsigned char c) {
            return c < 32 || c == 127;
        })) return std::nullopt;

    std::vector<std::string> components;
    // NormalizePluginRelativePath already rejects traversal, device paths and
    // empty components in the calling file. Only its parent directory is used.
    std::size_t offset{};
    while (true) {
        auto end = current->find('/', offset);
        if (end == current->npos) break;
        components.emplace_back(current->substr(offset, end - offset));
        offset = end + 1;
    }
    offset = 0;
    while (offset < requested.size()) {
        auto end = requested.find_first_of("/\\", offset);
        if (end == requested.npos) end = requested.size();
        auto part = requested.substr(offset, end - offset);
        if (part.empty()) {
            // Match standardized relative paths with internal repeated slash.
            // A leading separator was already rejected as an absolute path.
        } else if (part == ".") {
            if (end == requested.size()) return std::nullopt; // directory, not file
        } else if (part == "..") {
            if (components.empty() || end == requested.size()) return std::nullopt;
            components.pop_back();
        } else {
            // Validate even a component later removed by ../. This rejects
            // drive-relative names, ADS and reserved devices before collapsing.
            auto safe = NormalizePluginRelativePath(part);
            if (!safe) return std::nullopt;
            components.push_back(std::move(*safe));
        }
        if (end == requested.size()) break;
        offset = end + 1;
    }
    std::string path;
    for (auto const& part : components) {
        if (!path.empty()) path += '/';
        path += part;
    }
    // Preserve spelling/case: Windows directories can be case sensitive.
    // Lexical aliases share a cache key; hard links/case aliases are not guessed.
    return NormalizePluginRelativePath(path);
}
}

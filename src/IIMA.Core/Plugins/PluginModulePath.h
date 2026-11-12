// Derived from IINA JavascriptPolyfill.register(require) and
// JavascriptPluginInstance.currentFileStack/evaluateFile.
// Copyright (c) 2018-2020 lhc and IINA contributors. GPL-3.0.
#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace iima::plugins {
// Both inputs are UTF-8 package-relative paths, not URLs. currentFile identifies
// the script making the call. Relative ./ and ../ requests are resolved against
// its directory, never against process CWD. Returns a '/' separated lexical
// cache key; does not append .js, infer an index file, decode URLs, fold case,
// read disk or authorize file access. The package reader must separately reject
// reparse points and resolve containment beneath its held root directory.
std::optional<std::string> ResolvePluginModulePath(
    std::string_view currentFile, std::string_view requested);
}

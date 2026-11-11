#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace iima {
// Translated from IINA Utility extension tables and DraggingDetect.getPlayableFiles.
// Source/version/license: references/iina-source.txt and references/iina/LICENSE.
bool IsPlaylistPath(std::wstring_view path);
bool AcceptExplicitFile(std::wstring_view path);
bool AcceptDiscoveredFile(std::wstring_view path);
void SortUniquePaths(std::vector<std::wstring>& paths);
}

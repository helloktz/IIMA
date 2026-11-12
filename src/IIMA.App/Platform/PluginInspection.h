#pragma once
#include <filesystem>
#include <stop_token>
#include <string>
namespace iima {
// Read-only inspection. A successful report does not enable/execute a plugin.
std::wstring InspectPluginFolder(std::filesystem::path const& root, std::stop_token stop = {});
}

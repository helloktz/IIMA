#pragma once
#include <filesystem>
#include <stop_token>
#include <string>
#include <map>
#include "../../IIMA.Core/Plugins/PluginManifest.h"
namespace iima {
struct PluginProgram {
    PluginManifest manifest;
    std::string source;
    std::map<std::string, std::string> modules; // package-local .js snapshots
};
// Snapshot source before confirmation. No later reread can change approved code.
PluginProgram LoadPluginProgram(std::filesystem::path const& root, std::stop_token stop = {});
// Read-only inspection. A successful report does not enable/execute a plugin.
std::wstring InspectPluginFolder(std::filesystem::path const& root, std::stop_token stop = {});
}

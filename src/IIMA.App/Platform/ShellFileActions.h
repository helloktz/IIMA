#pragma once
#include <filesystem>
namespace iima {
// Native Explorer selection; never interpolates a path into a shell command.
void RevealFile(std::filesystem::path const& path);
}

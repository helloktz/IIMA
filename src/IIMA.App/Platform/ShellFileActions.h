#pragma once
#include <filesystem>
#include <span>
#include <stop_token>
#include <vector>
namespace iima {
// Native Explorer selection; never interpolates a path into a shell command.
void RevealFile(std::filesystem::path const& path);
struct RevealBatchResult {
    std::size_t accepted{}, failed{}, skipped{};
    bool cancelled{};
};
// Caller runs on a COM-initialized worker. One Explorer request per directory,
// bounded at 64 files / 16 directories; never executes a path as a command.
RevealBatchResult RevealFiles(std::span<const std::filesystem::path> paths,
    std::stop_token stop);
}

// Adapted from IINA MPVCommandWrappers.swift, copyright (c) 2025 lhc.
// GNU GPL v3; see references/iina/LICENSE.
#include "PlaylistCommands.h"
#include <utility>

namespace iima {
namespace {
bool ValidPath(std::string const& path) {
    return !path.empty() && path.find('\0') == std::string::npos;
}
}
std::optional<MpvCommandArguments> PlaylistInsertCommand(std::string path, std::int64_t index) {
    if (!ValidPath(path) || index < 0) return std::nullopt;
    return MpvCommandArguments{"loadfile", std::move(path), "insert-at", std::to_string(index)};
}
std::optional<MpvCommandArguments> PlaylistAppendCommand(std::string path) {
    if (!ValidPath(path)) return std::nullopt;
    return MpvCommandArguments{"loadfile", std::move(path), "append"};
}
std::optional<MpvCommandArguments> PlaylistMoveCommand(std::int64_t from, std::int64_t to) {
    if (from < 0 || to < 0) return std::nullopt;
    return MpvCommandArguments{"playlist-move", std::to_string(from), std::to_string(to)};
}
std::optional<MpvCommandArguments> PlaylistRemoveCommand(std::int64_t index) {
    if (index < 0) return std::nullopt;
    return MpvCommandArguments{"playlist-remove", std::to_string(index)};
}
}

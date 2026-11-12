// Adapted from IINA MPVCommandWrappers.swift, copyright (c) 2025 lhc.
// GNU GPL v3; see references/iina/LICENSE.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iima {
// argv is sent directly to MpvClient::Command. Do not shell-quote file names.
// The current mpv playlist remains authoritative; these are requests only.
using MpvCommandArguments = std::vector<std::string>;
std::optional<MpvCommandArguments> PlaylistInsertCommand(std::string path, std::int64_t index);
std::optional<MpvCommandArguments> PlaylistAppendCommand(std::string path);
std::optional<MpvCommandArguments> PlaylistMoveCommand(std::int64_t from, std::int64_t to);
std::optional<MpvCommandArguments> PlaylistRemoveCommand(std::int64_t index);
}

// IINA Preference.ToolBarButton and PrefOSCToolbarSettingsSheetController.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace iima::settings {
enum class ToolbarAction { Settings, Playlist, Pip, Fullscreen, MusicMode, SubtitleTrack, Screenshot };
inline constexpr std::size_t MaxToolbarActions=5;
std::span<ToolbarAction const> AvailableToolbarActions() noexcept;
std::span<ToolbarAction const> DefaultToolbarActions() noexcept;
std::string_view ToolbarActionToken(ToolbarAction) noexcept;
// Empty is an intentionally empty toolbar. Invalid action lists are rejected.
std::optional<std::vector<ToolbarAction>> ParseToolbarActions(std::string_view);
// Restore older settings by dropping the retired toolbar action.
std::optional<std::vector<ToolbarAction>> ParseSavedToolbarActions(std::string_view);
// UI normalization: preserve first occurrence order, omit invalid enum values,
// and retain at most the upstream five slots. Result always round-trips.
std::string SerializeToolbarActions(std::span<ToolbarAction const>);
}

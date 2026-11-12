// Adapted from IINA IINACommand.swift and PlayerWindowController.swift.
// Copyright (c) 2017 lhc. GNU GPL v3; see references/iina/LICENSE.
#include "ApplicationCommand.h"
#include <array>
#include <utility>

namespace iima {
namespace {
using C = ApplicationCommand;
constexpr std::pair<C, std::string_view> names[] = {
    {C::TogglePictureInPicture, "toggle-pip"}, {C::OpenFile, "open-file"},
    {C::OpenUrl, "open-url"}, {C::AudioPanel, "audio-panel"}, {C::VideoPanel, "video-panel"},
    {C::SubtitlePanel, "sub-panel"}, {C::PlaylistPanel, "playlist-panel"},
    {C::ChapterPanel, "chapter-panel"}, {C::ToggleMusicMode, "toggle-music-mode"},
    {C::LiveText, "live-text"}, {C::ToggleFlip, "toggle-flip"}, {C::ToggleMirror, "toggle-mirror"},
    {C::BiggerWindow, "bigger-window"}, {C::SmallerWindow, "smaller-window"},
    {C::FitToScreen, "fit-to-screen"}, {C::SavePlaylist, "save-playlist"},
    // Retain the imported key spelling; Windows handler opens Explorer.
    {C::RevealCurrentFile, "show-current-file-in-finder"},
    {C::DeleteCurrentFile, "delete-current-file"},
    {C::DeleteCurrentFilePermanently, "delete-current-file-hard"},
    {C::FindOnlineSubtitles, "find-online-subs"}, {C::SaveDownloadedSubtitle, "save-downloaded-sub"}
};
}
std::optional<ApplicationCommand> ParseApplicationCommand(std::string_view name) {
    for (auto const& [command, spelling] : names) if (spelling == name) return command;
    return std::nullopt;
}
std::string_view ApplicationCommandName(ApplicationCommand command) noexcept {
    for (auto const& [value, spelling] : names) if (value == command) return spelling;
    return {};
}
std::optional<ApplicationCommand> ApplicationCommandForBinding(KeyMapping const& binding) {
    if (!binding.IsIinaCommand()) return std::nullopt;
    // Match IINA's rawAction enum parse, not merely the first word. Extra
    // arguments or a command chain must not invoke a destructive partial match.
    return ParseApplicationCommand(binding.RawAction());
}
void ApplicationCommandRouter::Register(ApplicationCommand command,
    std::function<void()> execute, std::function<bool()> canExecute) {
    if (!execute) { Unregister(command); return; }
    handlers_.insert_or_assign(command, Handler{std::move(execute), std::move(canExecute)});
}
void ApplicationCommandRouter::Unregister(ApplicationCommand command) { handlers_.erase(command); }
bool ApplicationCommandRouter::CanExecute(ApplicationCommand command) const {
    auto found = handlers_.find(command);
    if (found == handlers_.end()) return false;
    // Copy before invoking callbacks, which may synchronously change menus.
    auto handler = found->second;
    return handler.execute && (!handler.canExecute || handler.canExecute());
}
ApplicationCommandRouter::Result ApplicationCommandRouter::Execute(KeyMapping const& binding) const {
    if (!binding.IsIinaCommand()) return Result::NotApplicationBinding;
    auto command = ApplicationCommandForBinding(binding);
    return command ? Execute(*command) : Result::InvalidCommand;
}
ApplicationCommandRouter::Result ApplicationCommandRouter::Execute(ApplicationCommand command) const {
    auto found = handlers_.find(command);
    if (found == handlers_.end()) return Result::Unavailable;
    auto handler = found->second;
    if (!handler.execute || (handler.canExecute && !handler.canExecute())) return Result::Unavailable;
    handler.execute();
    return Result::Executed;
}
}

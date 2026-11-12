// Adapted from IINA IINACommand.swift and PlayerWindowController.swift.
// Copyright (c) 2017 lhc. GNU GPL v3; see references/iina/LICENSE.
#pragma once
#include "../Input/KeyMapping.h"
#include <functional>
#include <map>

namespace iima {
enum class ApplicationCommand {
    TogglePictureInPicture, OpenFile, OpenUrl, AudioPanel, VideoPanel,
    SubtitlePanel, PlaylistPanel, ChapterPanel, ToggleMusicMode, LiveText,
    ToggleFlip, ToggleMirror, BiggerWindow, SmallerWindow, FitToScreen,
    SavePlaylist, RevealCurrentFile, DeleteCurrentFile, DeleteCurrentFilePermanently,
    FindOnlineSubtitles, SaveDownloadedSubtitle
};
std::optional<ApplicationCommand> ParseApplicationCommand(std::string_view name);
std::string_view ApplicationCommandName(ApplicationCommand command) noexcept;
std::optional<ApplicationCommand> ApplicationCommandForBinding(KeyMapping const& binding);

// Only registered, available commands are executed. Unimplemented IINA actions
// are reported as unavailable instead of silently treated as handled. A command
// callback owns UI dispatch and any user confirmation required for its action.
class ApplicationCommandRouter final {
public:
    enum class Result { NotApplicationBinding, InvalidCommand, Unavailable, Executed };
    void Register(ApplicationCommand command, std::function<void()> execute,
                  std::function<bool()> canExecute = {});
    void Unregister(ApplicationCommand command);
    bool CanExecute(ApplicationCommand command) const;
    Result Execute(KeyMapping const& binding) const;
    Result Execute(ApplicationCommand command) const;
private:
    struct Handler {
        std::function<void()> execute;
        std::function<bool()> canExecute;
    };
    std::map<ApplicationCommand, Handler> handlers_;
};
}

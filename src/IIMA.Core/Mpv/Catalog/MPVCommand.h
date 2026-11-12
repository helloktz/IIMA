// Transcribed from IINA's pinned mpv constant catalog; GPL v3.
// Copyright IINA contributors. Source/license: references/iina-source.txt,
// references/iina/LICENSE. Regenerate with scripts/generate-mpv-catalog.py.
// Names describe the reference API, not availability in the loaded libmpv.

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace iima::mpv_catalog {
struct MPVCommand final {
    static constexpr std::string_view seek = "seek";
    static constexpr std::string_view revertSeek = "revert-seek";
    static constexpr std::string_view subSeek = "sub-seek";
    static constexpr std::string_view frameStep = "frame-step";
    static constexpr std::string_view frameBackStep = "frame-back-step";
    static constexpr std::string_view stop = "stop";
    static constexpr std::string_view set = "set";
    static constexpr std::string_view del = "del";
    static constexpr std::string_view add = "add";
    static constexpr std::string_view multiply = "multiply";
    static constexpr std::string_view cycle = "cycle";
    static constexpr std::string_view cycleValues = "cycle-values";
    static constexpr std::string_view changeList = "change-list";
    static constexpr std::string_view playlistNext = "playlist-next";
    static constexpr std::string_view playlistPrev = "playlist-prev";
    static constexpr std::string_view playlistNextPlaylist = "playlist-next-playlist";
    static constexpr std::string_view playlistPrevPlaylist = "playlist-prev-playlist";
    static constexpr std::string_view playlistPlayIndex = "playlist-play-index";
    static constexpr std::string_view loadfile = "loadfile";
    static constexpr std::string_view loadlist = "loadlist";
    static constexpr std::string_view playlistClear = "playlist-clear";
    static constexpr std::string_view playlistRemove = "playlist-remove";
    static constexpr std::string_view playlistMove = "playlist-move";
    static constexpr std::string_view playlistShuffle = "playlist-shuffle";
    static constexpr std::string_view playlistUnshuffle = "playlist-unshuffle";
    static constexpr std::string_view subAdd = "sub-add";
    static constexpr std::string_view subRemove = "sub-remove";
    static constexpr std::string_view subReload = "sub-reload";
    static constexpr std::string_view subStep = "sub-step";
    static constexpr std::string_view audioAdd = "audio-add";
    static constexpr std::string_view audioRemove = "audio-remove";
    static constexpr std::string_view audioReload = "audio-reload";
    static constexpr std::string_view videoAdd = "video-add";
    static constexpr std::string_view videoRemove = "video-remove";
    static constexpr std::string_view videoReload = "video-reload";
    static constexpr std::string_view rescanExternalFiles = "rescan-external-files";
    static constexpr std::string_view printText = "print-text";
    static constexpr std::string_view expandText = "expand-text";
    static constexpr std::string_view expandPath = "expand-path";
    static constexpr std::string_view normalizePath = "normalize-path";
    static constexpr std::string_view escapeAss = "escape-ass";
    static constexpr std::string_view applyProfile = "apply-profile";
    static constexpr std::string_view loadConfigFile = "load-config-file";
    static constexpr std::string_view writeWatchLaterConfig = "write-watch-later-config";
    static constexpr std::string_view deleteWatchLaterConfig = "delete-watch-later-config";
    static constexpr std::string_view showText = "show-text";
    static constexpr std::string_view showProgress = "show-progress";
    static constexpr std::string_view overlayAdd = "overlay-add";
    static constexpr std::string_view overlayRemove = "overlay-remove";
    static constexpr std::string_view osdOverlay = "osd-overlay";
    static constexpr std::string_view mouse = "mouse";
    static constexpr std::string_view keypress = "keypress";
    static constexpr std::string_view keydown = "keydown";
    static constexpr std::string_view keyup = "keyup";
    static constexpr std::string_view keybind = "keybind";
    static constexpr std::string_view enableSection = "enable-section";
    static constexpr std::string_view disableSection = "disable-section";
    static constexpr std::string_view defineSection = "define-section";
    static constexpr std::string_view loadInputConf = "load-input-conf";
    static constexpr std::string_view run = "run";
    static constexpr std::string_view subprocess = "subprocess";
    static constexpr std::string_view quit = "quit";
    static constexpr std::string_view quitWatchLater = "quit-watch-later";
    static constexpr std::string_view scriptMessage = "script-message";
    static constexpr std::string_view scriptMessageTo = "script-message-to";
    static constexpr std::string_view scriptBinding = "script-binding";
    static constexpr std::string_view loadScript = "load-script";
    static constexpr std::string_view screenshot = "screenshot";
    static constexpr std::string_view screenshotToFile = "screenshot-to-file";
    static constexpr std::string_view screenshotRaw = "screenshot-raw";
    static constexpr std::string_view af = "af";
    static constexpr std::string_view vf = "vf";
    static constexpr std::string_view vfCommand = "vf-command";
    static constexpr std::string_view afCommand = "af-command";
    static constexpr std::string_view ignore = "ignore";
    static constexpr std::string_view dropBuffers = "drop-buffers";
    static constexpr std::string_view dumpCache = "dump-cache";
    static constexpr std::string_view abLoop = "ab-loop";
    static constexpr std::string_view abLoopDumpCache = "ab-loop-dump-cache";
    static constexpr std::string_view abLoopAlignCache = "ab-loop-align-cache";
    static constexpr std::string_view beginVoDragging = "begin-vo-dragging";
    static constexpr std::string_view contextMenu = "context-menu";
};
}

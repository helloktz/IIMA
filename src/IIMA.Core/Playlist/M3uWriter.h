// Derived from IINA MainMenuActions.swift menuSavePlaylist (lhc, 2016).
// GPL-3.0; references/iina-source.txt and references/iina/LICENSE.
#pragma once
#include "../Mpv/MpvClient.h"
#include <span>

namespace iima {
struct M3uText {
    std::string utf8;
    std::string error;
    explicit operator bool() const noexcept { return error.empty(); }
};
// Serializes the current observed queue, not a speculative edit plan. Paths
// and URLs are retained as UTF-8, one per line, as IINA's .m3u8 save action.
// No disk write here: the WinUI save picker and atomic file store own that.
M3uText SerializeM3u8(std::span<const PlaylistItem> items);
} // namespace iima

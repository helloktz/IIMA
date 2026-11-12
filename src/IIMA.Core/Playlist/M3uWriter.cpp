// Derived from IINA MainMenuActions.swift menuSavePlaylist (lhc, 2016).
// GPL-3.0; references/iina-source.txt and references/iina/LICENSE.
#include "M3uWriter.h"

namespace iima {
M3uText SerializeM3u8(std::span<const PlaylistItem> items) {
    M3uText result;
    for (const auto& item : items) {
        // Unescaped line terminators or a leading '#' cannot round-trip as an
        // M3U entry. Reject instead of silently injecting a playlist directive.
        // Full Windows paths such as C:\\movies\\#title.mkv are not rejected.
        if (item.path.empty() || item.path.front() == '#' || item.path.find('\r') != std::string::npos ||
            item.path.find('\n') != std::string::npos || item.path.find('\0') != std::string::npos) {
            result.utf8.clear();
            result.error = "A playlist entry cannot be represented safely as a single M3U line";
            return result;
        }
        result.utf8 += item.path;
        result.utf8 += '\n';
    }
    return result;
}
} // namespace iima

// Windows replacement for IINA KeyCodeHelper.mpvKeyCode(from:).
// IINA Copyright © 2016 lhc; GPL-3.0, references/iina/LICENSE.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace iima {
// Call on the UI thread while handling a key event. Does not consume dead-key
// state or IME composition. Pure modifier and system-reserved chords return null.
std::optional<std::string> WindowsMpvKey(std::uint32_t virtualKey, std::uint32_t scanCode, bool extended);
}

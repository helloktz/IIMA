#include "pch.h"
#include "KeyboardInput.h"
#include "../../IIMA.Core/Input/KeyMapping.h"
#include <array>
namespace iima {
std::optional<std::string> WindowsMpvKey(std::uint32_t key, std::uint32_t scanCode, bool extended) {
    auto down = [](int vk) { return (GetKeyState(vk) & 0x8000) != 0; };
    if (key == VK_PROCESSKEY || key == VK_PACKET || key == VK_SHIFT || key == VK_CONTROL ||
        key == VK_MENU || key == VK_LWIN || key == VK_RWIN) return {};
    auto control = down(VK_CONTROL), alt = down(VK_MENU), shift = down(VK_SHIFT);
    // Do not steal AltGr text entry or Windows shell shortcuts.
    if ((down(VK_RMENU) && control) || down(VK_LWIN) || down(VK_RWIN)) return {};
    std::string name;
    switch (key) {
    case VK_SPACE: name = "SPACE"; break;
    case VK_RETURN: name = extended ? "KP_ENTER" : "ENTER"; break;
    case VK_ESCAPE: name = "ESC"; break;
    case VK_TAB: name = "TAB"; break;
    case VK_BACK: name = "BS"; break;
    case VK_DELETE: name = "DEL"; break;
    case VK_INSERT: name = "INS"; break;
    case VK_HOME: name = "HOME"; break;
    case VK_END: name = "END"; break;
    case VK_PRIOR: name = "PGUP"; break;
    case VK_NEXT: name = "PGDWN"; break;
    case VK_LEFT: name = "LEFT"; break;
    case VK_RIGHT: name = "RIGHT"; break;
    case VK_UP: name = "UP"; break;
    case VK_DOWN: name = "DOWN"; break;
    case VK_DECIMAL: name = "KP_DEC"; break;
    case VK_MULTIPLY: name = "KP_MULTIPLY"; break;
    case VK_ADD: name = "KP_ADD"; break;
    case VK_SUBTRACT: name = "KP_SUBTRACT"; break;
    case VK_DIVIDE: name = "KP_DIVIDE"; break;
    default:
        if (key >= VK_F1 && key <= VK_F24) name = "F" + std::to_string(key - VK_F1 + 1);
        else if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) name = "KP" + std::to_string(key - VK_NUMPAD0);
        else {
            std::array<BYTE, 256> state{};
            if (!GetKeyboardState(state.data())) return {};
            // Preserve layout/Shift; ignore Caps Lock for stable binding identity.
            state[VK_CAPITAL] = 0;
            state[VK_CONTROL] = state[VK_LCONTROL] = state[VK_RCONTROL] = 0;
            state[VK_MENU] = state[VK_LMENU] = state[VK_RMENU] = 0;
            wchar_t characters[8]{};
            // Bit 2 prevents ToUnicodeEx from altering keyboard/dead-key state.
            int count = ToUnicodeEx(key, scanCode, state.data(), characters, 8, 4, GetKeyboardLayout(0));
            if (count <= 0 || count > 2 || characters[0] < 32) return {};
            if (count == 2 && !(characters[0] >= 0xd800 && characters[0] <= 0xdbff &&
                characters[1] >= 0xdc00 && characters[1] <= 0xdfff)) return {};
            name = winrt::to_string(std::wstring_view(characters, count));
            name = EscapeReservedMpvKey(name);
            shift = false; // Already represented in the printable character.
        }
    }
    std::string chord;
    if (control) chord += "Ctrl+";
    if (alt) chord += "Alt+";
    if (shift) chord += "Shift+";
    return NormalizeMpvKey(chord + name);
}
}

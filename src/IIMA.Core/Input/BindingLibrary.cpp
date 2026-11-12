// IINA SettingsPageKeyBindings.ConfigEditor adaptation; GPL-3.0.
// Copyright (c) lhc and contributors. See references/iina/LICENSE.
#include "BindingLibrary.h"
#include "KeyMapping.h"
namespace iima {
namespace {
bool SameName(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return true;
}
}
bool ValidBindingLibraryName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128 || name.front() == ' ' || name.back() == ' ' ||
        name == "." || name == "..") return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':') return false;
    return true;
}
bool ValidBindingLibrary(BindingLibrary const& library) noexcept {
    if (library.size() > 32) return false;
    std::size_t bytes{};
    for (auto a = library.begin(); a != library.end(); ++a) {
        if (!ValidBindingLibraryName(a->first) || a->second.size() > 256 * 1024 ||
            a->second.find('\0') != std::string::npos) return false;
        bytes += a->first.size() + a->second.size();
        if (bytes > 1024 * 1024) return false;
        for (auto b = library.begin(); b != a; ++b) if (SameName(a->first, b->first)) return false;
    }
    return true;
}
std::string BindingLibraryError(BindingLibrary const& library, std::string_view name,
    std::string_view text, bool replace) {
    if (!ValidBindingLibraryName(name)) return "Name must be 1..128 UTF-8 bytes without boundary spaces, separators or controls";
    if (text.size() > 256 * 1024 || text.find('\0') != std::string_view::npos)
        return "Configuration exceeds 256 KiB or contains NUL";
    auto found = library.find(name);
    if (replace && found == library.end()) return "Selected configuration no longer exists";
    for (auto const& [existing, value] : library)
        if (SameName(existing, name) && (!replace || existing != name)) return "Configuration name already exists";
    auto parsed = ParseInputConf(std::string(text));
    if (!parsed.issues.empty()) return "Line " + std::to_string(parsed.issues.front().line) + ": " + parsed.issues.front().message;
    auto candidate = library;
    candidate.insert_or_assign(std::string(name), std::string(text));
    if (!ValidBindingLibrary(candidate)) return "Configuration library exceeds 32 entries or 1 MiB";
    return {};
}
}

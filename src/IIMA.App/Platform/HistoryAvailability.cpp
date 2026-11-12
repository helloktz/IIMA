#include "pch.h"
#include "HistoryAvailability.h"
#include <filesystem>
#include <chrono>
#include <vector>
namespace iima {
namespace {
HistoryAvailability Probe(std::string const& utf8) {
    using A = HistoryAvailability;
    if (utf8.empty() || utf8.size() > 1024 || utf8.find('\0') != utf8.npos) return A::Unknown;
    auto wide = winrt::to_hstring(utf8);
    std::filesystem::path path(wide.c_str());
    // No UNC/device/relative paths, ADS or legacy long-path interpretation.
    auto const& text = path.native();
    if (text.size() < 3 || text.size() >= MAX_PATH || text[1] != L':' ||
        (text[2] != L'\\' && text[2] != L'/') ||
        !((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) ||
        text.find(L':', 2) != text.npos) return A::Unknown;
    auto root = path.root_path();
    if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) return A::Unknown;
    std::vector<std::filesystem::path> parts;
    for (auto const& part : path.relative_path()) {
        auto const& name = part.native();
        if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ' ||
            name.find_first_of(L"*?\"<>|") != name.npos) return A::Unknown;
        parts.push_back(part);
    }
    if (parts.empty() || parts.size() > 64) return A::Unknown;
    std::vector<winrt::handle> held;
    auto open = [&](std::filesystem::path const& target, bool directory) -> A {
        // Do not share delete: ancestors cannot be exchanged underneath later
        // opens. OPEN_REPARSE_POINT inspects each component without following it.
        auto raw = CreateFileW(target.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (raw == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? A::Missing : A::Unknown;
        }
        winrt::handle handle{raw};
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(handle.get(), &information)) return A::Unknown;
        const auto attributes = information.dwFileAttributes;
        if ((attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE)) ||
            (((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory)) return A::Unknown;
        held.push_back(std::move(handle));
        return A::Available;
    };
    // An unavailable root is not proof that a history file was deleted.
    if (open(root, true) != A::Available) return A::Unknown;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        root /= parts[i];
        auto result = open(root, i + 1 < parts.size());
        if (result != A::Available) return result;
    }
    return A::Available;
}
}
HistoryAvailabilityScan ScanHistoryAvailability(std::vector<std::string> const& paths, std::stop_token stop) {
    HistoryAvailabilityScan result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (auto const& path : paths) {
        if (stop.stop_requested() || result.attempted >= 4096 || std::chrono::steady_clock::now() >= deadline) {
            result.limited = true; break;
        }
        ++result.attempted;
        try { result.entries.emplace(path, Probe(path)); }
        catch (...) { result.entries.emplace(path, HistoryAvailability::Unknown); }
    }
    return result;
}
}

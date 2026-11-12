// Derived behavior: SidebarPlaylistPane.contextMenuDeleteFile.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PlaylistFileDeletion.h"
#include <shellapi.h>
#include <algorithm>
#pragma comment(lib, "shell32.lib")
namespace iima {
PlaylistFileDeletionResult DeletePlaylistFiles(
    std::vector<std::filesystem::path> const& paths, std::stop_token stop) {
    PlaylistFileDeletionResult result;
    if (paths.size() > 64) throw winrt::hresult_invalid_argument(L"Select at most 64 files");
    result.items.reserve(paths.size());
    for (auto const& path : paths) {
        if (stop.stop_requested()) { result.cancelled = true; break; }
        PlaylistFileDeletionItem item{path};
        auto native = path.native();
        // Only ordinary drive-qualified file paths. Never expand wildcards,
        // resolve device aliases, recurse directories, or traverse network roots.
        const bool drive = native.size() > 3 && native[1] == L':' && native[2] == L'\\' &&
            ((native[0] >= L'A' && native[0] <= L'Z') || (native[0] >= L'a' && native[0] <= L'z'));
        bool invalidPart{};
        for (auto const& part : path.relative_path()) {
            auto name = part.native();
            if (name == L"." || name == L".." || name.empty() || name.back() == L'.' || name.back() == L' ')
                invalidPart = true;
        }
        if (!drive || invalidPart || native.find_first_of(L"*?\"<>|") != std::wstring::npos ||
            native.find(L'\0') != std::wstring::npos || native.find(L':',2) != std::wstring::npos ||
            native.size() >= MAX_PATH) {
            item.shellError = ERROR_INVALID_NAME; result.items.push_back(std::move(item)); continue;
        }
        auto root = path.root_path().native();
        const auto driveType = GetDriveTypeW(root.c_str());
        bool unsafeParent = driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE;
        for (auto parent = path.parent_path(); !unsafeParent && parent != parent.root_path(); parent = parent.parent_path()) {
            const auto flags = GetFileAttributesW(parent.c_str());
            if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT) ||
                !(flags & FILE_ATTRIBUTE_DIRECTORY)) unsafeParent = true;
        }
        if (unsafeParent) {
            item.shellError = ERROR_NOT_SUPPORTED; result.items.push_back(std::move(item)); continue;
        }
        const auto attributes = GetFileAttributesW(native.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            item.shellError = attributes == INVALID_FILE_ATTRIBUTES ? static_cast<int>(GetLastError()) : ERROR_NOT_SUPPORTED;
            result.items.push_back(std::move(item)); continue;
        }
        // SHFileOperation needs two terminating NULs, even for a single item.
        // One operation per file lets partial successes be reconciled exactly.
        native.push_back(L'\0'); native.push_back(L'\0');
        SHFILEOPSTRUCTW operation{};
        operation.wFunc = FO_DELETE;
        operation.pFrom = native.c_str();
        operation.fFlags = FOF_ALLOWUNDO | FOF_WANTNUKEWARNING | FOF_SIMPLEPROGRESS;
        operation.lpszProgressTitle = L"IIMA：删除所选播放列表文件";
        // Unowned native UI remains valid if the player window closes. Never
        // set NOCONFIRMATION, SILENT or NOERRORUI: permanent deletion requires
        // the user's native shell confirmation, not an application assumption.
        item.shellError = SHFileOperationW(&operation);
        item.cancelled = operation.fAnyOperationsAborted != FALSE;
        item.completed = item.shellError == 0 && !item.cancelled;
        result.items.push_back(std::move(item));
        if (operation.fAnyOperationsAborted) { result.cancelled = true; break; }
    }
    return result;
}
}

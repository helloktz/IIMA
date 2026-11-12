// IINA MainMenuActions.menuShowCurrentFileInFinder Windows adaptation.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "ShellFileActions.h"
#include <shlobj.h>
#include <memory>
#include <map>
#include <set>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
namespace iima {
void RevealFile(std::filesystem::path const& path) {
    if (!path.is_absolute() || path.native().find(L'\0') != std::wstring::npos)
        throw winrt::hresult_invalid_argument(L"Explorer requires an absolute local file path");
    PIDLIST_ABSOLUTE raw{};
    winrt::check_hresult(SHParseDisplayName(path.c_str(), nullptr, &raw, 0, nullptr));
    struct FreePidl { void operator()(ITEMIDLIST* value) const noexcept { CoTaskMemFree(value); } };
    std::unique_ptr<ITEMIDLIST, FreePidl> owned(raw);
    winrt::check_hresult(SHOpenFolderAndSelectItems(owned.get(), 0, nullptr, 0));
}
// HistoryWindowController.showInFinderAction and SidebarPlaylistPane's
// contextMenuShowInFinder: group selected local entries for the native viewer.
RevealBatchResult RevealFiles(std::span<const std::filesystem::path> paths,
    std::stop_token stop) {
    if (paths.size() > 64) throw winrt::hresult_invalid_argument(L"Select at most 64 files");
    struct FreePidl { void operator()(ITEMIDLIST* value) const noexcept { CoTaskMemFree(value); } };
    using Pidl = std::unique_ptr<ITEMIDLIST, FreePidl>;
    struct Group { Pidl parent; std::vector<Pidl> files; };
    std::map<std::filesystem::path, Group> groups;
    std::set<std::filesystem::path> seen;
    RevealBatchResult result;
    for (auto const& input : paths) {
        if (stop.stop_requested()) { result.cancelled = true; return result; }
        auto path = input.lexically_normal();
        if (!path.is_absolute() || path.native().find(L'\0') != std::wstring::npos || path.filename().empty()) {
            ++result.skipped; continue;
        }
        if (!seen.insert(path).second) { ++result.skipped; continue; }
        // No canonicalization/network probing on the UI thread. The shell
        // resolves this path now; a missing or inaccessible file is a failure.
        PIDLIST_ABSOLUTE raw{};
        auto hr = SHParseDisplayName(path.c_str(), nullptr, &raw, 0, nullptr);
        Pidl file(raw);
        if (FAILED(hr) || !file) { ++result.failed; continue; }
        auto directory = path.parent_path();
        auto found = groups.find(directory);
        if (found == groups.end()) {
            if (groups.size() >= 16) throw winrt::hresult_invalid_argument(L"Select files from at most 16 directories");
            Pidl parent(ILCloneFull(file.get()));
            if (!parent || !ILRemoveLastID(parent.get())) { ++result.failed; continue; }
            found = groups.emplace(directory, Group{std::move(parent), {}}).first;
        }
        found->second.files.push_back(std::move(file));
    }
    for (auto const& [directory, group] : groups) {
        if (stop.stop_requested()) { result.cancelled = true; break; }
        std::vector<PCUITEMID_CHILD> children;
        for (auto const& file : group.files) children.push_back(ILFindLastID(file.get()));
        auto hr = SHOpenFolderAndSelectItems(group.parent.get(), static_cast<UINT>(children.size()), children.data(), 0);
        if (SUCCEEDED(hr)) result.accepted += children.size();
        else result.failed += children.size();
        // Once submitted to Explorer a request cannot be recalled on close.
    }
    return result;
}

}

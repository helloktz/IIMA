// IINA MainMenuActions.menuShowCurrentFileInFinder Windows adaptation.
// Copyright © 2016 lhc. GPL v3; references/iina/LICENSE.
#include "pch.h"
#include "ShellFileActions.h"
#include <shlobj.h>
#include <memory>
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
}

// IINA SidebarPlaylistPane.contextMenuDeleteFile Windows shell adaptation.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <filesystem>
#include <stop_token>
#include <vector>
namespace iima {
struct PlaylistFileDeletionItem {
    std::filesystem::path path;
    bool completed{};
    bool cancelled{};
    int shellError{};
};
struct PlaylistFileDeletionResult {
    std::vector<PlaylistFileDeletionItem> items;
    bool cancelled{};
};
// Run on a dedicated STA. Always retain native confirmation, including the
// cannot-recycle warning. completed means the shell delete operation finished,
// NOT proof that a recoverable Recycle Bin entry exists. No fallback retries.
PlaylistFileDeletionResult DeletePlaylistFiles(
    std::vector<std::filesystem::path> const&, std::stop_token);
}

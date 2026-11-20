// IINA HistoryWindowController / SidebarPlaylistPane file-viewer actions.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/ShellFileActions.h"
#include "../IIMA.Core/Network/MediaUrl.h"
using namespace winrt;
namespace winrt::IIMA::implementation {
fire_and_forget MainWindow::RevealSelection(bool history) {
    auto lifetime = get_strong();
    if (closing_ || revealSelectionBusy_) co_return;
    std::vector<std::string> selected;
    if (history) {
        for (auto row : SelectedHistoryRows())
            if (auto entry = historyProjection_.EntryAt(row)) selected.push_back(entry->entry.path);
    } else {
        for (auto row : SelectedPlaylistRows()) selected.push_back(playlist_[row].path);
    }
    std::vector<std::filesystem::path> paths;
    std::size_t excluded{};
    try {
        for (auto const& path : selected) {
            if (!iima::CanPersistMediaPath(path)) { ++excluded; continue; }
            paths.emplace_back(to_hstring(path).c_str());
        }
        if (paths.empty()) { ShowError(iima::loc::Text(L"TheSelectedEntryDoesNotHaveA")); co_return; }
        if (paths.size() > 64) { ShowError(iima::loc::Text(L"PleaseReduceTheSelectionForEachLocation")); co_return; }
        revealSelectionBusy_ = true;
        RevealHistoryButton().IsEnabled(false); RevealPlaylistButton().IsEnabled(false);
        revealSelectionStop_ = std::stop_source{};
        auto stop = revealSelectionStop_.get_token();
        apartment_context ui;
        iima::RevealBatchResult result;
        std::exception_ptr failure;
        co_await resume_background();
        // Threadpool COM may already be initialized; balance only our success.
        auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            if (FAILED(com) && com != RPC_E_CHANGED_MODE) check_hresult(com);
            result = iima::RevealFiles(paths, stop);
        } catch (...) { failure = std::current_exception(); }
        if (SUCCEEDED(com)) CoUninitialize();
        co_await ui;
        if (failure) std::rethrow_exception(failure);
        if (!closing_) ShowError(iima::loc::Text(L"SubmittedToResourceManager") + to_hstring(result.accepted) + iima::loc::Text(L"EntriesFailures") +
            to_hstring(result.failed) + iima::loc::Text(L"RowSkip") + to_hstring(result.skipped + excluded) + iima::loc::Text(L"Item") +
            (result.cancelled ? iima::loc::Text(L"OperationCancelled") : L""));
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
      catch (...) { if (!closing_) ShowError(iima::loc::Text(L"CouldNotLocateTheSelectedFile")); }
    revealSelectionBusy_ = false;
    if (!closing_) { UpdateHistorySelection(); UpdatePlaylistSelection(); }
}
}

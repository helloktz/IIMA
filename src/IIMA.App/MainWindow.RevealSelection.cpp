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
        if (paths.empty()) { ShowError(L"所选条目没有可定位的本地文件。"); co_return; }
        if (paths.size() > 64) { ShowError(L"每次最多定位64个文件，请减少选择。"); co_return; }
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
        if (!closing_) ShowError(L"已向资源管理器提交 " + to_hstring(result.accepted) + L" 项；失败 " +
            to_hstring(result.failed) + L" 项，跳过 " + to_hstring(result.skipped + excluded) + L" 项。" +
            (result.cancelled ? L"操作已取消。" : L""));
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
      catch (...) { if (!closing_) ShowError(L"无法定位所选文件。"); }
    revealSelectionBusy_ = false;
    if (!closing_) { UpdateHistorySelection(); UpdatePlaylistSelection(); }
}
}

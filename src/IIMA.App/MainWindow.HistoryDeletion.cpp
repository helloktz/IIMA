// IINA HistoryWindowController.deleteFileAction/removeFileAfterConfirmation.
// Copyright (c) 2017 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/PlaylistFileDeletion.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include <algorithm>
#include <future>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool SameHistoryFile(std::filesystem::path const& a, std::filesystem::path const& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WireHistoryDeletion() {
    auto weak = get_weak();
    DeleteHistoryFilesButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->DeleteSelectedHistoryFiles();
    });
    CancelHistoryDeletionButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->historyDeletionBusy_) {
            self->historyDeletionStop_.request_stop();
            if (self->historyDeletionDialog_) self->historyDeletionDialog_.Hide();
            self->HistoryFileStatus().Text(L"已请求停止后续文件；正在显示的系统删除确认需在其中取消。已发生的删除不能由历史撤销恢复。");
        }
    });
}
fire_and_forget MainWindow::DeleteSelectedHistoryFiles() {
    auto lifetime = get_strong();
    if (closing_ || !userDataLoaded_ || projectingHistory_ || pickerOpen_ ||
        historyDeletionBusy_ || playlistDeletionBusy_ || cropOriginal_) co_return;
    std::vector<std::filesystem::path> paths;
    std::size_t skipped{};
    try {
        const auto epoch = navigationEpoch_;
        const auto selected = iima::SelectHistoryRows(historyProjection_, SelectedHistoryRows());
        const auto active = std::filesystem::path(to_hstring(session_->Snapshot().path).c_str()).lexically_normal();
        for (auto const& entry : selected) {
            if (!iima::CanPersistMediaPath(entry.path)) { ++skipped; continue; }
            auto path = std::filesystem::path(to_hstring(entry.path).c_str()).lexically_normal();
            if (SameHistoryFile(path, active)) { ++skipped; continue; }
            if (std::none_of(paths.begin(), paths.end(), [&](auto const& existing) {
                return SameHistoryFile(path, existing);
            })) paths.push_back(std::move(path));
        }
        if (paths.empty()) {
            HistoryFileStatus().Text(L"没有可删除的非当前本地文件；网络历史和当前播放文件不会删除。");
            co_return;
        }
        if (paths.size() > 64) {
            HistoryFileStatus().Text(L"每批最多64个不同文件，请减少选择。"); co_return;
        }
        std::wstring preview = L"删除下面的磁盘文件，但保留历史记录。历史的“撤销移除”不能恢复磁盘文件。Windows优先使用回收站；不能回收时，系统会另行要求永久删除确认，不同意请在系统对话框取消。\n\n";
        for (auto const& path : paths) {
            if (preview.size() + path.native().size() + 1 > 24000) {
                HistoryFileStatus().Text(L"无法完整显示全部路径，请减少选择。"); co_return;
            }
            preview += path.native() + L"\n";
        }
        historyDeletionBusy_ = true; pickerOpen_ = true;
        historyDeletionStop_ = std::stop_source{};
        UpdateHistorySelection(); CancelHistoryDeletionButton().IsEnabled(true);
        historyDeletionDialog_ = Controls::ContentDialog();
        historyDeletionDialog_.XamlRoot(Root().XamlRoot());
        historyDeletionDialog_.Title(box_value(L"删除历史对应的磁盘文件？"));
        Controls::TextBlock text; text.Text(preview); text.TextWrapping(TextWrapping::Wrap);
        text.IsTextSelectionEnabled(true);
        Controls::ScrollViewer scroll; scroll.MaxHeight(420); scroll.Content(text);
        historyDeletionDialog_.Content(scroll);
        historyDeletionDialog_.PrimaryButtonText(L"继续到系统删除确认");
        historyDeletionDialog_.CloseButtonText(L"取消");
        historyDeletionDialog_.DefaultButton(Controls::ContentDialogButton::Close);
        const auto approved = co_await historyDeletionDialog_.ShowAsync();
        historyDeletionDialog_ = nullptr;
        auto stop = historyDeletionStop_.get_token();
        if (approved == Controls::ContentDialogResult::Primary && !closing_ && !stop.stop_requested()) {
            if (navigationEpoch_ != epoch)
                throw hresult_error(E_ABORT, L"确认期间媒体已改变，未开始删除。请重新选择。");
            // Freeze the explicitly confirmed path list. A search/selection
            // change must never substitute different files in this operation.
            apartment_context ui;
            iima::PlaylistFileDeletionResult result;
            std::exception_ptr failure;
            co_await resume_background();
            try {
                result = std::async(std::launch::async, [paths, stop] {
                    init_apartment(apartment_type::single_threaded);
                    struct Apartment { ~Apartment() { uninit_apartment(); } } apartment;
                    return iima::DeletePlaylistFiles(paths, stop);
                }).get();
            } catch (...) { failure = std::current_exception(); }
            co_await ui;
            if (failure) std::rethrow_exception(failure);
            if (!closing_) {
                const auto completed = std::count_if(result.items.begin(), result.items.end(),
                    [](auto const& item) { return item.completed; });
                const auto notCompleted = paths.size() - static_cast<std::size_t>(completed);
                // IINA retains history after trashing files. Refresh the view
                // only; do not touch persistence, playlist or undo. Availability
                // is independently rechecked, not inferred from deletion success.
                RefreshHistory();
                RequestHistoryAvailability(true);
                HistoryFileStatus().Text(L"系统完成删除 " + to_hstring(completed) + L" 个文件；未完成 " +
                    to_hstring(notCompleted) + L" 个；跳过 " + to_hstring(skipped) + L" 条网络/当前播放记录。历史记录保留。" +
                    (result.cancelled ? L"后续操作已取消。" : L"") +
                    (notCompleted ? L"请核对文件与系统提示，不会自动重试。" : L""));
            }
        } else if (!closing_) HistoryFileStatus().Text(L"未开始删除；历史和磁盘文件未因本次操作改变。");
    } catch (hresult_error const& error) { if (!closing_) HistoryFileStatus().Text(error.message()); }
      catch (...) {
        if (!closing_) HistoryFileStatus().Text(L"文件操作未完整完成；已发生的删除不会回滚，请核对文件后再决定是否重试。历史记录保留。");
    }
    historyDeletionDialog_ = nullptr;
    historyDeletionBusy_ = false; pickerOpen_ = false;
    if (!closing_) {
        CancelHistoryDeletionButton().IsEnabled(false);
        UpdateHistorySelection();
    }
}
}

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
            self->HistoryFileStatus().Text(iima::loc::Text(L"AFollowUpDocumentHasBeenRequested"));
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
            HistoryFileStatus().Text(iima::loc::Text(L"ThereAreNoNonCurrentLocalFiles"));
            co_return;
        }
        if (paths.size() > 64) {
            HistoryFileStatus().Text(iima::loc::Text(L"AMaximumOf64DifferentFilesPer")); co_return;
        }
        std::wstring preview = iima::loc::Text(L"DeletesTheDiskFileBelowButKeeps");
        for (auto const& path : paths) {
            if (preview.size() + path.native().size() + 1 > 24000) {
                HistoryFileStatus().Text(iima::loc::Text(L"CouldNotCompleteThePathPleaseReduce")); co_return;
            }
            preview += path.native() + L"\n";
        }
        historyDeletionBusy_ = true; pickerOpen_ = true;
        historyDeletionStop_ = std::stop_source{};
        UpdateHistorySelection(); CancelHistoryDeletionButton().IsEnabled(true);
        historyDeletionDialog_ = Controls::ContentDialog();
        historyDeletionDialog_.XamlRoot(Root().XamlRoot());
        historyDeletionDialog_.Title(box_value(iima::loc::Text(L"DeleteTheHistoricalDiskFile")));
        Controls::TextBlock text; text.Text(preview); text.TextWrapping(TextWrapping::Wrap);
        text.IsTextSelectionEnabled(true);
        Controls::ScrollViewer scroll; scroll.MaxHeight(420); scroll.Content(text);
        historyDeletionDialog_.Content(scroll);
        historyDeletionDialog_.PrimaryButtonText(iima::loc::Text(L"ContinueToSystemDeleteConfirmation"));
        historyDeletionDialog_.CloseButtonText(iima::loc::Text(L"Cancel"));
        historyDeletionDialog_.DefaultButton(Controls::ContentDialogButton::Close);
        const auto approved = co_await historyDeletionDialog_.ShowAsync();
        historyDeletionDialog_ = nullptr;
        auto stop = historyDeletionStop_.get_token();
        if (approved == Controls::ContentDialogResult::Primary && !closing_ && !stop.stop_requested()) {
            if (navigationEpoch_ != epoch)
                throw hresult_error(E_ABORT, iima::loc::Text(L"TheMediaHaveChangedDuringTheConfirmation"));
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
                HistoryFileStatus().Text(iima::loc::Text(L"SystemCompleteForDeletion") + to_hstring(completed) + iima::loc::Text(L"DocumentsNotCompleted") +
                    to_hstring(notCompleted) + iima::loc::Text(L"IndividualSkipping") + to_hstring(skipped) + iima::loc::Text(L"ANetworkCurrentPlayRecordHistoricalRecords") +
                    (result.cancelled ? iima::loc::Text(L"FollowUpHasBeenCancelled") : L"") +
                    (notCompleted ? iima::loc::Text(L"PleaseCheckTheFileWithTheSystem") : L""));
            }
        } else if (!closing_) HistoryFileStatus().Text(iima::loc::Text(L"NoDeletionStartedHistoryAndDiskFiles"));
    } catch (hresult_error const& error) { if (!closing_) HistoryFileStatus().Text(error.message()); }
      catch (...) {
        if (!closing_) HistoryFileStatus().Text(iima::loc::Text(L"TheFileOperationIsNotFullyCompleted"));
    }
    historyDeletionDialog_ = nullptr;
    historyDeletionBusy_ = false; pickerOpen_ = false;
    if (!closing_) {
        CancelHistoryDeletionButton().IsEnabled(false);
        UpdateHistorySelection();
    }
}
}

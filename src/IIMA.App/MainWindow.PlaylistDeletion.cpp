// IINA SidebarPlaylistPane.contextMenuDeleteFile: remove only successful rows.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "Platform/PlaylistFileDeletion.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include <future>
#include <algorithm>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool SamePath(std::filesystem::path const& a, std::filesystem::path const& b) {
    return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE) == CSTR_EQUAL;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WirePlaylistDeletion() {
    auto weak = get_weak();
    DeletePlaylistFilesButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->DeleteSelectedPlaylistFiles();
    });
    CancelPlaylistDeletionButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && self->playlistDeletionBusy_) {
            self->playlistDeletionStop_.request_stop();
            self->StatusText().Text(iima::loc::Text(L"AFollowUpFileHasBeenRequested"));
        }
    });
}
fire_and_forget MainWindow::DeleteSelectedPlaylistFiles() {
    auto lifetime = get_strong();
    if (closing_ || pickerOpen_ || playlistDeletionBusy_ || cropOriginal_) co_return;
    struct Entry { std::int64_t id; std::string original; std::filesystem::path path; };
    std::vector<Entry> entries;
    std::vector<std::filesystem::path> paths;
    std::size_t skipped{};
    try {
        auto const& state = session_->Snapshot();
        const auto epoch = navigationEpoch_;
        const auto revision = state.playlistRevision;
        const auto active = std::filesystem::path(to_hstring(state.path).c_str());
        for (auto row : SelectedPlaylistRows()) {
            auto const& source = playlist_[row];
            if (!iima::CanPersistMediaPath(source.path)) { ++skipped; continue; }
            std::filesystem::path path(to_hstring(source.path).c_str());
            path = path.lexically_normal();
            // Do not stop playback or silently unlock a playing file. All
            // selected aliases of the current path are excluded from this job.
            if (source.current || SamePath(path,active.lexically_normal())) { ++skipped; continue; }
            entries.push_back({source.id,source.path,path});
            if (std::none_of(paths.begin(),paths.end(),[&](auto const& p) { return SamePath(p,path); }))
                paths.push_back(path);
        }
        if (paths.empty()) { ShowError(iima::loc::Text(L"ThereAreNoNonCurrentLocalFiles2")); co_return; }
        if (paths.size() > 64) { ShowError(iima::loc::Text(L"PleaseReduceTheSelectionForEachDeletion")); co_return; }
        std::wstring preview = iima::loc::Text(L"ThisIsToRemoveDiskFilesNot");
        for (auto const& path : paths) {
            if (preview.size()+path.native().size()+1 > 24000) {
                ShowError(iima::loc::Text(L"SelectAPathThatIsTooMany")); co_return;
            }
            preview += path.native()+L"\n";
        }
        playlistDeletionBusy_ = true; pickerOpen_ = true;
        playlistDeletionStop_ = std::stop_source{};
        UpdatePlaylistSelection(); CancelPlaylistDeletionButton().IsEnabled(true);
        playlistDeletionDialog_ = Controls::ContentDialog();
        playlistDeletionDialog_.XamlRoot(Root().XamlRoot());
        playlistDeletionDialog_.Title(box_value(iima::loc::Text(L"ConfirmingToDeleteTheseDiskFiles")));
        Controls::TextBlock text; text.Text(preview); text.TextWrapping(TextWrapping::Wrap);
        Controls::ScrollViewer scroll; scroll.MaxHeight(420); scroll.Content(text);
        playlistDeletionDialog_.Content(scroll);
        playlistDeletionDialog_.PrimaryButtonText(iima::loc::Text(L"ContinueToSystemDeleteConfirmation"));
        playlistDeletionDialog_.CloseButtonText(iima::loc::Text(L"Cancel"));
        playlistDeletionDialog_.DefaultButton(Controls::ContentDialogButton::Close);
        const auto approved = co_await playlistDeletionDialog_.ShowAsync();
        playlistDeletionDialog_ = nullptr;
        auto stop = playlistDeletionStop_.get_token();
        if (approved == Controls::ContentDialogResult::Primary && !closing_ && !stop.stop_requested()) {
            if (navigationEpoch_ != epoch || session_->Snapshot().playlistRevision != revision || session_->PlaylistEditBusy())
                throw hresult_error(E_ABORT,iima::loc::Text(L"TheMediaOrPlaylistChangesDuringThe"));
            apartment_context ui;
            iima::PlaylistFileDeletionResult result;
            std::exception_ptr error;
            co_await resume_background();
            try {
                // Shell UI needs a dedicated STA, not WinRT's MTA pool.
                result = std::async(std::launch::async,[paths,stop] {
                    init_apartment(apartment_type::single_threaded);
                    struct Apartment { ~Apartment() { uninit_apartment(); } } apartment;
                    return iima::DeletePlaylistFiles(paths,stop);
                }).get();
            } catch (...) { error = std::current_exception(); }
            co_await ui;
            if (error) std::rethrow_exception(error);
            if (!closing_) {
                std::vector<std::size_t> rows;
                auto const& current = session_->Snapshot();
                const auto completed = std::count_if(result.items.begin(),result.items.end(),[](auto const& i) { return i.completed; });
                // IDs and original path, not stale positional indices. Do not
                // touch a new media generation, newly inserted duplicate, or
                // an item that became current while the shell was open.
                if (navigationEpoch_ == epoch && !session_->PlaylistEditBusy()) {
                    for (std::size_t row=0;row<current.playlist.size();++row) {
                        auto const& item = current.playlist[row];
                        if (item.current) continue;
                        auto selected = std::find_if(entries.begin(),entries.end(),[&](auto const& e) {
                            return e.id == item.id && e.original == item.path;
                        });
                        if (selected == entries.end()) continue;
                        if (std::any_of(result.items.begin(),result.items.end(),[&](auto const& r) {
                            return r.completed && SamePath(r.path,selected->path);
                        })) rows.push_back(row);
                    }
                }
                const auto requestedRows = rows.size();
                if (!rows.empty()) session_->RemovePlaylistItems(std::move(rows));
                StatusText().Text(iima::loc::Text(L"SystemCompleteForDeletion")+to_hstring(completed)+iima::loc::Text(L"FilesRequiredForRemoval")+
                    to_hstring(requestedRows)+iima::loc::Text(L"ListEntriesThatStillMatchSkipping")+to_hstring(skipped)+iima::loc::Text(L"SelectedItems")+
                    (result.cancelled ? iima::loc::Text(L"FollowUpHasBeenCancelled") : L""));
                if (completed != static_cast<std::ptrdiff_t>(paths.size()))
                    ShowError(iima::loc::Text(L"SomeFilesHaveNotBeenDeletedOr"));
            }
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
      catch (...) { if (!closing_) ShowError(iima::loc::Text(L"DeletesIncompletelyTheSystemOperationThatHas")); }
    playlistDeletionDialog_ = nullptr;
    playlistDeletionBusy_ = false; pickerOpen_ = false;
    if (!closing_) { CancelPlaylistDeletionButton().IsEnabled(false); UpdatePlaylistSelection(); }
}
}

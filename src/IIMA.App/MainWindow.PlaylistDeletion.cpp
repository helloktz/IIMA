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
            self->StatusText().Text(L"已请求停止后续文件。当前系统删除对话框需在其中取消；已完成的删除不能撤回。");
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
        if (paths.empty()) { ShowError(L"没有可删除的非当前本地文件。网络条目和当前播放文件不会删除。"); co_return; }
        if (paths.size() > 64) { ShowError(L"每次最多删除64个不同文件，请减少选择。"); co_return; }
        std::wstring preview = L"这是删除磁盘文件，不是仅移除播放列表。Windows将优先使用回收站；不能回收时会另行要求系统确认永久删除。请在系统对话框取消不希望永久删除的文件。\n\n";
        for (auto const& path : paths) {
            if (preview.size()+path.native().size()+1 > 24000) {
                ShowError(L"选择的路径过多，无法完整展示，请减少选择。"); co_return;
            }
            preview += path.native()+L"\n";
        }
        playlistDeletionBusy_ = true; pickerOpen_ = true;
        playlistDeletionStop_ = std::stop_source{};
        UpdatePlaylistSelection(); CancelPlaylistDeletionButton().IsEnabled(true);
        playlistDeletionDialog_ = Controls::ContentDialog();
        playlistDeletionDialog_.XamlRoot(Root().XamlRoot());
        playlistDeletionDialog_.Title(box_value(L"确认删除这些磁盘文件？"));
        Controls::TextBlock text; text.Text(preview); text.TextWrapping(TextWrapping::Wrap);
        Controls::ScrollViewer scroll; scroll.MaxHeight(420); scroll.Content(text);
        playlistDeletionDialog_.Content(scroll);
        playlistDeletionDialog_.PrimaryButtonText(L"继续到系统删除确认");
        playlistDeletionDialog_.CloseButtonText(L"取消");
        playlistDeletionDialog_.DefaultButton(Controls::ContentDialogButton::Close);
        const auto approved = co_await playlistDeletionDialog_.ShowAsync();
        playlistDeletionDialog_ = nullptr;
        auto stop = playlistDeletionStop_.get_token();
        if (approved == Controls::ContentDialogResult::Primary && !closing_ && !stop.stop_requested()) {
            if (navigationEpoch_ != epoch || session_->Snapshot().playlistRevision != revision || session_->PlaylistEditBusy())
                throw hresult_error(E_ABORT,L"确认期间媒体或播放列表变化，未开始删除。请重新选择。");
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
                StatusText().Text(L"系统完成删除 "+to_hstring(completed)+L" 个文件；已请求移除 "+
                    to_hstring(requestedRows)+L" 个仍匹配的列表条目；跳过 "+to_hstring(skipped)+L" 个所选条目。"+
                    (result.cancelled ? L"后续操作已取消。" : L""));
                if (completed != static_cast<std::ptrdiff_t>(paths.size()))
                    ShowError(L"部分文件未删除或尚未处理，请检查列表。目录、链接、网络/设备路径和超长路径不支持；系统错误不会自动重试。");
            }
        }
    } catch (hresult_error const& error) { if (!closing_) ShowError(error.message()); }
      catch (...) { if (!closing_) ShowError(L"删除未完整完成。已发生的系统操作不会回滚，请检查文件后再决定是否重试。"); }
    playlistDeletionDialog_ = nullptr;
    playlistDeletionBusy_ = false; pickerOpen_ = false;
    if (!closing_) { CancelPlaylistDeletionButton().IsEnabled(false); UpdatePlaylistSelection(); }
}
}

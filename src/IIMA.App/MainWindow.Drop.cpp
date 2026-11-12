// IINA DraggingDetect.acceptFromPasteboard/openFromPasteboard.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Input/LocalInputPolicy.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include <algorithm>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::ApplicationModel::DataTransfer;
namespace winrt::IIMA::implementation {
namespace {
bool InternalDrop(DragEventArgs const& args) {
    // Reject even stale/foreign internal capabilities; never reinterpret a
    // playlist move's fallback text as a new media open when it bubbles.
    return args.DataView().Properties().HasKey(L"IIMA.Playlist.LocalDrag");
}
bool SupportedDrop(DataPackageView const& data) {
    return data.Contains(StandardDataFormats::StorageItems()) ||
        data.Contains(StandardDataFormats::WebLink()) || data.Contains(StandardDataFormats::Text());
}
bool ValidDropPath(std::wstring const& value) {
    auto normalized = value;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    return !value.empty() && value.size() <= 32767 &&
        std::none_of(value.begin(), value.end(), [](wchar_t c) { return c < 32 || c == 127; }) &&
        !normalized.starts_with(L"\\\\?\\") && !normalized.starts_with(L"\\\\.\\") &&
        value.find(L':', 2) == value.npos && std::filesystem::path(value).is_absolute();
}
bool LutPath(std::wstring const& value) {
    auto extension = std::filesystem::path(value).extension().native();
    for (auto& c : extension) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return extension == L".cube" || extension == L".3dl" || extension == L".dat" || extension == L".m3d";
}

}
void MainWindow::DragOverFiles(DragEventArgs const& args) {
    if (args.Handled()) return;
    args.AcceptedOperation(DataPackageOperation::None);
    try {
        if (InternalDrop(args)) { args.Handled(true); return; }
        if (closing_ || !userDataLoaded_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ ||
            session_->PlaylistEditBusy()) return;
        if (SupportedDrop(args.DataView())) args.AcceptedOperation(DataPackageOperation::Copy);
    } catch (...) { /* A malformed data provider cannot authorize a drop. */ }
}
fire_and_forget MainWindow::DropFiles(DragEventArgs args, std::optional<std::size_t> playlistRow) {
    auto lifetime = get_strong();
    if (args.Handled()) co_return;
    args.Handled(true);
    args.AcceptedOperation(DataPackageOperation::None);
    if (closing_ || !userDataLoaded_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ ||
        session_->PlaylistEditBusy()) co_return;
    if (playlistRow && (session_->Snapshot().session!=iima::SessionState::Ready ||
        inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty())) co_return;
    // RAII completes the native drop even on cancellation, provider exceptions,
    // or a failure to dispatch back to a closing window.
    decltype(args.GetDeferral()) deferral{nullptr};
    try { deferral = args.GetDeferral(); } catch (...) { co_return; }
    struct Complete { decltype(deferral) value; ~Complete() { try { value.Complete(); } catch (...) {} } } complete{deferral};
    const auto operation = ++dropReadGeneration_;
    const auto inputRevision = inputGeneration_;
    const auto generation = session_->Snapshot().mediaGeneration;
    const auto intent = session_->Snapshot().mediaOpenIntentRevision;
    const auto revision=session_->Snapshot().playlistRevision;
    const auto editIntent=session_->PlaylistEditIntentRevision();
    std::vector<std::int64_t> ids;
    if (playlistRow) {
        auto const& items=session_->Snapshot().playlist;
        if (*playlistRow>items.size() || playlist_.size()!=items.size()) co_return;
        for (std::size_t i=0;i<items.size();++i) {
            if (items[i].id<0 || playlist_[i].id!=items[i].id) co_return;
            ids.push_back(items[i].id);
        }
        // An accepted insertion gesture supersedes background auto expansion
        // before the provider/resolver yields; it must not race for this row.
        CancelAutomaticFiles();
    }
    auto current = [this, operation, inputRevision, generation, intent, playlistRow, revision, editIntent, ids] {
        auto const& state = session_->Snapshot();
        if (playlistRow) {
            if (state.session!=iima::SessionState::Ready || inputResolving_ || !pendingFiles_.empty() ||
                !activationInputs_.empty() || state.playlistRevision!=revision ||
                session_->PlaylistEditIntentRevision()!=editIntent || state.playlist.size()!=ids.size() ||
                playlist_.size()!=ids.size()) return false;
            for (std::size_t i=0;i<ids.size();++i)
                if (state.playlist[i].id!=ids[i] || playlist_[i].id!=ids[i]) return false;
        }
        return !closing_ && userDataLoaded_ && !pickerOpen_ && !interactiveCrop_.Active() && !cropOriginal_ &&
            !session_->PlaylistEditBusy() && dropReadGeneration_ == operation && inputGeneration_ == inputRevision &&
            state.mediaGeneration == generation && state.mediaOpenIntentRevision == intent;
    };
    try {
        if (InternalDrop(args)) co_return;
        auto data = args.DataView();
        std::vector<std::wstring> inputs;
        if (data.Contains(StandardDataFormats::StorageItems())) {
            auto items = co_await data.GetStorageItemsAsync();
            if (!current()) co_return;
            if (items.Size() == 0 || items.Size() > 1000)
                throw hresult_invalid_argument(L"每次拖放1到1000个文件或文件夹。");
            std::size_t bytes{};
            for (auto const& item : items) {
                std::wstring path{item.Path()};
                bytes += path.size() * sizeof(wchar_t);
                if (!ValidDropPath(path) || bytes > 1024 * 1024)
                    throw hresult_invalid_argument(L"拖放包含虚拟对象、无效/设备路径或超过1MiB的路径数据，未打开。");
                inputs.push_back(std::move(path));
            }
        } else {
            hstring raw;
            if (data.Contains(StandardDataFormats::WebLink())) {
                auto uri = co_await data.GetWebLinkAsync();
                if (uri) raw = uri.AbsoluteUri();
            } else if (data.Contains(StandardDataFormats::Text())) raw = co_await data.GetTextAsync();
            else co_return;
            if (!current()) co_return;
            if (raw.empty() || raw.size() > 32767)
                throw hresult_invalid_argument(L"请拖放一个绝对文件路径或支持的网络URL（最多32767字符）。");
            auto text = to_string(raw);
            if (iima::IsNetworkMediaPath(text)) {
                auto prepared = iima::PrepareMediaUrl(text);
                if (!prepared) throw hresult_invalid_argument(L"拖放的网络URL无效。");
                Windows::Foundation::Uri uri(to_hstring(prepared->value));
                if (uri.Host().empty()) throw hresult_invalid_argument(L"拖放的网络URL缺少主机名。");
                if (playlistRow) {
                    CancelAutomaticFiles();
                    session_->InsertPlaylist({to_string(uri.AbsoluteUri())},playlistRow);
                } else ReceiveResolvedInput({to_string(uri.AbsoluteUri())});
                args.AcceptedOperation(DataPackageOperation::Copy);
                co_return;
            }
            std::wstring path{raw};
            if (!ValidDropPath(path)) throw hresult_invalid_argument(L"仅支持绝对本地路径或支持的网络URL，不执行拖放文本。");
            inputs.push_back(std::move(path));
        }
        std::vector<std::wstring> media;
        std::vector<std::string> subtitles;
        bool skippedLut{}, invalidSubtitle{}, rejectedPlaylist{};
        apartment_context ui;
        std::exception_ptr classificationFailure;
        co_await resume_background();
        try {
        // Only explicit regular subtitle files are attached, never recursively
        // discovered sidecars. Directories, including *.srt folders, use resolver.
        for (auto const& path : inputs) {
            auto attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) media.push_back(path);
            else if (playlistRow && iima::IsPlaylistPath(path)) rejectedPlaylist = true;
            else if (LutPath(path)) skippedLut = true;
            else if (!iima::AcceptExplicitFile(path) && !iima::IsPlaylistPath(path)) {
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) invalidSubtitle = true;
                else subtitles.push_back(to_string(path));
            } else media.push_back(path);
        }
        } catch (...) { classificationFailure = std::current_exception(); }
        try { co_await ui; } catch (...) { co_return; }
        if (classificationFailure) std::rethrow_exception(classificationFailure);
        if (!current()) co_return;
        if (inputs.size() == 1 && skippedLut) {
            if (playlistRow) ShowError(L"LUT不是播放列表媒体，请拖到播放区域。");
            else if (!QuickLutAvailable()) ShowError(L"请等待当前视频及其他操作完成后再导入LUT。");
            else { ImportQuickLut(std::move(inputs.front())); args.AcceptedOperation(DataPackageOperation::Copy); }
            co_return;
        }
        if (rejectedPlaylist) {
            ShowError(L"行插入不支持播放列表文件的异步展开，请先打开该列表；本次未插入任何项。");
            co_return;
        }
        auto weak = get_weak();
        auto finish = [weak, operation, inputRevision, generation, intent,
            current, playlistRow, subtitles = std::move(subtitles), skippedLut, invalidSubtitle, singleSource = inputs.size() == 1](iima::LocalInputResult result) {
            auto self = weak.get();
            if (!self || self->closing_ || result.cancelled || !self->userDataLoaded_ || self->pickerOpen_ ||
                self->interactiveCrop_.Active() || self->cropOriginal_ || self->session_->PlaylistEditBusy() ||
                self->dropReadGeneration_ != operation || self->inputGeneration_ != inputRevision) return;
            // current captures this without ownership. The successful weak.get()
            // above holds self strongly for this entire callback before invoking it.
            auto const& state = self->session_->Snapshot();
            if (state.mediaGeneration != generation || state.mediaOpenIntentRevision != intent || !current()) return;
            if (!result.paths.empty()) {
                if (playlistRow) {
                    if (result.paths.size()>1000) {
                        self->ShowError(L"展开文件夹后超过1000项，未插入；请减少选择。"); return;
                    }
                    std::size_t bytes{};
                    for (auto const& path:result.paths) {
                        if (path.size()>1024*1024-bytes) {
                            self->ShowError(L"展开后的UTF-8路径超过1MiB，未插入。"); return;
                        }
                        bytes+=path.size();
                    }
                    self->CancelAutomaticFiles();
                    self->session_->InsertPlaylist(std::move(result.paths),playlistRow);
                } else self->ReceiveResolvedInput(std::move(result.paths), singleSource && result.singleExplicitMedia);
            } else if (!subtitles.empty() && playlistRow) {
                self->ShowError(L"播放列表行只接收媒体；字幕请拖到播放区域或使用添加字幕。");
                return;
            } else if (!subtitles.empty()) {
                if (self->inputResolving_ || !self->pendingFiles_.empty() || !self->activationInputs_.empty()) {
                    self->ShowError(L"另一次媒体打开尚未完成，请等待后重新拖放字幕。"); return;
                }
                if (subtitles.size() > 64) {
                    self->ShowError(L"每次最多附加64个字幕文件，未附加任何字幕。"); return;
                }
                if (state.session != iima::SessionState::Ready || state.media != iima::MediaState::Loaded) {
                    self->ShowError(L"请先打开媒体，再拖放字幕。"); return;
                }
                // Sub-add remains asynchronous: observed tracks own selection
                // and errors. Never mark a subtitle selected optimistically.
                for (auto const& path : subtitles) self->session_->AddSubtitle(path);
            } else self->ShowError(L"拖放内容中没有可打开的媒体或字幕。");
            std::wstring warning;
            for (auto const& line : result.warnings) { if (!warning.empty()) warning += L"\n"; warning += line; }
            if (playlistRow && !subtitles.empty()) warning += L"\n已跳过字幕；播放列表插入不会给当前媒体附加字幕。";
            if (skippedLut) warning += L"\n混合拖放中的3D LUT已跳过；请将单个LUT单独拖到播放区域。";
            if (invalidSubtitle) warning += L"\n无法读取字幕或字幕为链接文件，已跳过。";
            if (!warning.empty()) self->ShowError(hstring{warning});
        };
        // A new drop supersedes old drops, not an unrelated validated input.
        dropResolveStop_.request_stop();
        if (media.empty()) finish({});
        else dropResolveStop_ = iima::LocalInputResolver::Resolve(std::move(media), DispatcherQueue(), std::move(finish));
        args.AcceptedOperation(DataPackageOperation::Copy);
    } catch (hresult_error const& error) { if (current()) ShowError(error.message()); }
      catch (std::exception const& error) { if (current()) ShowError(to_hstring(error.what())); }
      catch (...) { if (current()) ShowError(L"无法读取拖放内容，未打开。"); }
}
}

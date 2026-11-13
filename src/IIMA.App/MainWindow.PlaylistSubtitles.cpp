// IINA SidebarPlaylistPane.contextMenuAddSubtitle/contextMenuWrongSubtitle.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Input/LocalInputPolicy.h"
#include <algorithm>
#include <ShObjIdl_core.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace {
bool LocalPlaylistMedia(std::string const& value) {
    try {
        std::wstring path{to_hstring(value)};
        std::replace(path.begin(),path.end(),L'/',L'\\');
        return !path.empty() && path.size()<=32767 && std::filesystem::path(path).is_absolute() &&
            path.find(L':',2)==path.npos && !path.starts_with(L"\\\\?\\") && !path.starts_with(L"\\\\.\\") &&
            std::none_of(path.begin(),path.end(),[](wchar_t c){return c<32 || c==127;});
    } catch(...) { return false; }
}
bool SameSubtitlePath(std::string const& a,std::string const& b) {
    auto left=std::filesystem::path(to_hstring(a).c_str()).lexically_normal().native();
    auto right=std::filesystem::path(to_hstring(b).c_str()).lexically_normal().native();
    return CompareStringOrdinal(left.c_str(),static_cast<int>(left.size()),right.c_str(),static_cast<int>(right.size()),TRUE)==CSTR_EQUAL;
}
bool SafeSubtitleFile(std::wstring const& value) {
    auto normalized=value;
    std::replace(normalized.begin(),normalized.end(),L'/',L'\\');
    if(value.empty() || value.size()>32767 || normalized.starts_with(L"\\\\?\\") ||
       normalized.starts_with(L"\\\\.\\") || value.find(L':',2)!=value.npos ||
       std::any_of(value.begin(),value.end(),[](wchar_t c){return c<32 || c==127;}) ||
       !std::filesystem::path(value).is_absolute() || iima::AcceptExplicitFile(value) || iima::IsPlaylistPath(value)) return false;
    const auto attributes=GetFileAttributesW(value.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))) return false;
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::path(value),error) && !error;
}
}
namespace winrt::IIMA::implementation {
void MainWindow::WirePlaylistSubtitleControls() {
    auto weak=get_weak();
    AddPlaylistSubtitleAssociationButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get()) self->AddPlaylistSubtitleAssociation();
    });
    ClearPlaylistSubtitleAssociationButton().Click([weak](auto const&,auto const&) {
        if(auto self=weak.get()) self->ClearPlaylistSubtitleAssociation();
    });
    PlaylistView().SelectionChanged([weak](auto const&,auto const&) {
        if(auto self=weak.get(); self && !self->updating_ && !self->closing_) {
            ++self->playlistSubtitleSelectionRevision_;
            self->UpdatePlaylistSubtitleControls();
        }
    });
    UpdatePlaylistSubtitleControls();
}
void MainWindow::UpdatePlaylistSubtitleControls() {
    auto ids=SelectedPlaylistIds();
    auto const& state=session_->Snapshot();
    auto selected=ids.size()==1 ? std::find_if(state.playlist.begin(),state.playlist.end(),
        [&](auto const& item){return item.id==ids.front();}) : state.playlist.end();
    const bool valid=selected!=state.playlist.end() && selected->id>=0 && LocalPlaylistMedia(selected->path);
    const bool allowed=valid && !closing_ && userDataLoaded_ && !pickerOpen_ &&
        !interactiveCrop_.Active() && !cropOriginal_ && !session_->PlaylistEditBusy() &&
        state.session==iima::SessionState::Ready && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty();
    AddPlaylistSubtitleAssociationButton().IsEnabled(allowed);
    // Clearing an empty automatic list creates an explicit suppression override.
    ClearPlaylistSubtitleAssociationButton().IsEnabled(allowed);
    auto paths=valid ? PlaylistSubtitlePaths(selected->id,selected->path) : std::vector<std::string>{};
    const bool overridden=valid && HasPlaylistSubtitleOverride(selected->id,selected->path);
    PlaylistSubtitleAssociationStatus().Text(!valid ? L"请选择一个队列条目。" :
        overridden ? (paths.empty() ? L"已清除IIMA关联；mpv自身自动加载不受影响。" : L"手动关联将在下次播放此条目时应用。") :
        paths.empty() ? L"此条目暂无字幕关联。" : L"当前自动匹配关联（最多显示64项）。");
    if(paths!=playlistSubtitleDisplayPaths_) {
        playlistSubtitleDisplayPaths_=paths;
        PlaylistSubtitleAssociationList().Items().Clear();
        for(std::size_t i=0;i<paths.size() && i<64;++i) {
            Controls::TextBlock text;
            text.Text(to_hstring(paths[i])); text.TextWrapping(TextWrapping::Wrap);
            PlaylistSubtitleAssociationList().Items().Append(text);
        }
    }
}
void MainWindow::ClearPlaylistSubtitleAssociation() {
    if(closing_ || !userDataLoaded_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ ||
       inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty()) return;
    auto rows=SelectedPlaylistRows();
    if(rows.size()!=1) return;
    auto item=session_->Snapshot().playlist[rows.front()];
    if(!LocalPlaylistMedia(item.path)) return;
    if(SetPlaylistSubtitlePaths(item.id,item.path,{})) {
        UpdatePlaylistSubtitleControls();
        ShowError(L"已清除IIMA关联；不卸载当前字幕，也不关闭mpv自身自动加载。");
    } else ShowError(L"队列已变化或关联无法更新，请重新选择。");
}
fire_and_forget MainWindow::AddPlaylistSubtitleAssociation() {
    auto lifetime=get_strong();
    if(closing_ || !userDataLoaded_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ ||
       inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty()) co_return;
    auto rows=SelectedPlaylistRows();
    if(rows.size()!=1) co_return;
    auto const& state=session_->Snapshot();
    const auto item=state.playlist[rows.front()];
    if(!LocalPlaylistMedia(item.path)) co_return;
    const auto revision=state.playlistRevision, intent=state.mediaOpenIntentRevision, media=state.mediaGeneration;
    const auto input=inputGeneration_, edit=session_->PlaylistEditIntentRevision(), selection=playlistSubtitleSelectionRevision_;
    // Snapshot effective automatic/manual associations before pickerOpen may
    // suspend automatic matching. Cancel never writes or clears an override.
    auto paths=PlaylistSubtitlePaths(item.id,item.path);
    auto current=[this,item,revision,intent,media,input,edit,selection] {
        auto const& now=session_->Snapshot();
        auto ids=SelectedPlaylistIds();
        return !closing_ && userDataLoaded_ && !interactiveCrop_.Active() && !cropOriginal_ &&
            !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
            now.session==iima::SessionState::Ready && !session_->PlaylistEditBusy() &&
            now.playlistRevision==revision && now.mediaOpenIntentRevision==intent && now.mediaGeneration==media &&
            inputGeneration_==input && session_->PlaylistEditIntentRevision()==edit &&
            playlistSubtitleSelectionRevision_==selection && ids.size()==1 && ids.front()==item.id &&
            std::any_of(now.playlist.begin(),now.playlist.end(),[&](auto const& entry){return entry.id==item.id && entry.path==item.path;});
    };
    pickerOpen_=true;
    UpdatePlaylistSubtitleControls();
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        HWND hwnd{}; check_hresult(GetNativeWindowHandle(&hwnd));
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(hwnd));
        for(auto extension:{L".utf",L".utf8",L".utf-8",L".idx",L".sub",L".srt",L".smi",L".rt",L".ssa",
            L".aqt",L".jss",L".js",L".ass",L".mks",L".vtt",L".sup",L".scc",L".lrc"}) picker.FileTypeFilter().Append(extension);
        auto files=co_await picker.PickMultipleFilesAsync();
        if(files && files.Size()) {
            if(!current()) throw hresult_invalid_argument(L"选择字幕期间队列或播放请求已变化，未修改关联。");
            if(files.Size()>64) throw hresult_invalid_argument(L"每次最多选择64个字幕文件，未修改关联。");
            std::vector<std::wstring> selected;
            std::size_t bytes{};
            for(auto const& file:files) {
                std::wstring path{file.Path()}; bytes+=path.size()*sizeof(wchar_t);
                if(bytes>1024*1024) throw hresult_invalid_argument(L"字幕路径超过1MiB，未修改关联。");
                selected.push_back(std::move(path));
            }
            apartment_context ui;
            std::exception_ptr failure;
            co_await resume_background();
            try {
                for(auto const& path:selected) {
                    if(!SafeSubtitleFile(path)) throw hresult_invalid_argument(L"所选项目不是有效的普通字幕文件，未修改关联。");
                    auto utf8=to_string(path);
                    if(std::none_of(paths.begin(),paths.end(),[&](auto const& existing){return SameSubtitlePath(existing,utf8);}))
                        paths.push_back(std::move(utf8));
                    if(paths.size()>64) throw hresult_invalid_argument(L"合并后超过每条目64个字幕的上限，未修改关联。");
                }
            } catch(...) { failure=std::current_exception(); }
            try { co_await ui; } catch(...) { co_return; }
            if(failure) std::rethrow_exception(failure);
            if(!current()) throw hresult_invalid_argument(L"检查字幕期间队列或播放请求已变化，未修改关联。");
            // Setter owns association storage; no sub-add/sub-remove here.
            if(!SetPlaylistSubtitlePaths(item.id,item.path,std::move(paths)))
                throw hresult_invalid_argument(L"队列已变化或关联超过存储预算，未修改关联。");
            ShowError(L"已保存此队列条目的字幕关联，下次播放此条目时应用；当前字幕未更改。");
        }
    } catch(hresult_error const& error) { if(!closing_) ShowError(error.message()); }
      catch(...) { if(!closing_) ShowError(L"无法修改字幕关联。"); }
    pickerOpen_=false;
    if(!closing_) { UpdatePlaylistSubtitleControls(); OpenPendingFiles(); FlushActivation(); }
}
}

// IINA SidebarPlaylistPane.validateDrop/acceptDrop insertion-row semantics.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Network/MediaUrl.h"
#include "../IIMA.Core/Input/LocalInputPolicy.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::ApplicationModel::DataTransfer;
namespace winrt::IIMA::implementation {
namespace {
constexpr wchar_t DragKey[] = L"IIMA.Playlist.LocalDrag";
// Every MainWindow uses the same UI dispatcher. Only a capability and weak
// owner cross windows; source paths stay in its bounded in-memory snapshot.
struct ActivePlaylistDrag { guid token; weak_ref<MainWindow> source; };
std::optional<ActivePlaylistDrag> activePlaylistDrag;
com_ptr<MainWindow> PlaylistDragSource(DragEventArgs const& args) {
    if(!activePlaylistDrag || unbox_value<guid>(args.DataView().Properties().Lookup(DragKey))!=activePlaylistDrag->token) return {};
    return activePlaylistDrag->source.get();
}
bool CopyablePlaylistPath(std::string const& path) {
    if(path.empty() || path.size()>32768 || std::any_of(path.begin(),path.end(),[](unsigned char c){return c<32 || c==127;})) return false;
    if(path.find("://")!=std::string::npos) {
        auto prepared=iima::PrepareMediaUrl(path);
        if(!prepared || !prepared->suppliedScheme || path.front()==' ' || path.back()==' ') return false;
        Windows::Foundation::Uri uri{to_hstring(path)};
        return !uri.Host().empty(); // Validate, but do not rewrite signed URL bytes.
    }
    if(path.size()<4 || path[1]!=':' || (path[2]!='/' && path[2]!='\\') ||
        !((path[0]>='A' && path[0]<='Z') || (path[0]>='a' && path[0]<='z')) || path.find(':',2)!=path.npos ||
        path.find_first_of("<>\"|?*")!=path.npos || iima::IsPlaylistPath(to_hstring(path))) return false;
    std::size_t begin=3;
    while(begin<path.size()) {
        auto end=path.find_first_of("/\\",begin);
        auto part=path.substr(begin,end==path.npos ? path.size()-begin : end-begin);
        if(part.empty() || part=="." || part==".." || part.back()=='.' || part.back()==' ') return false;
        auto stem=part.substr(0,part.find('.'));
        while(!stem.empty() && stem.back()==' ') stem.pop_back();
        std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return c>='a' && c<='z' ? c-'a'+'A' : c;});
        if(stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL" || stem=="CONIN$" || stem=="CONOUT$" ||
            ((stem.starts_with("COM") || stem.starts_with("LPT")) && ((stem.size()==4 && stem[3]>='1' && stem[3]<='9') ||
                stem.substr(3)=="\xc2\xb9" || stem.substr(3)=="\xc2\xb2" || stem.substr(3)=="\xc2\xb3"))) return false;
        if(end==path.npos) return true;
        begin=end+1;
    }
    return false;
}
bool HasPlaylistDrag(DragEventArgs const& args) {
    return args.DataView().Properties().HasKey(DragKey);
}
}
bool MainWindow::PlaylistDragReady() const {
    auto const& state=session_->Snapshot();
    return !closing_ && userDataLoaded_ && !pickerOpen_ && !inputResolving_ && pendingFiles_.empty() &&
        activationInputs_.empty() && !interactiveCrop_.Active() && !cropOriginal_ &&
        state.session==iima::SessionState::Ready && !state.mediaOriginPending && !session_->PlaylistEditBusy();
}
void MainWindow::ResetPlaylistDrag() {
    if(playlistDragToken_ && activePlaylistDrag && activePlaylistDrag->token==*playlistDragToken_) activePlaylistDrag.reset();
    playlistDragToken_.reset();playlistDragRows_.clear();playlistDragItems_.clear();playlistDragCopyAllowed_=false;
}
bool MainWindow::PlaylistDragCurrent(DragEventArgs const& args,bool verifyVisual) {
    try {
        if(!PlaylistDragReady() || !playlistDragToken_ || !HasPlaylistDrag(args)) {ResetPlaylistDrag();return false;}
        if(unbox_value<guid>(args.DataView().Properties().Lookup(DragKey))!=*playlistDragToken_) return false;
        auto const& state=session_->Snapshot();
        if(state.playlistRevision!=playlistDragRevision_ || state.mediaGeneration!=playlistDragMedia_ ||
            state.mediaOpenIntentRevision!=playlistDragOpenIntent_ || inputGeneration_!=playlistDragInput_ ||
            session_->PlaylistEditIntentRevision()!=playlistDragEditIntent_ || state.playlist.size()!=playlistDragItems_.size() ||
            playlist_.size()!=playlistDragItems_.size() || (verifyVisual && PlaylistView().Items().Size()!=playlistDragItems_.size())) {
            ResetPlaylistDrag();return false;
        }
        for(std::size_t i=0;i<playlistDragItems_.size();++i) {
            auto const& [id,path]=playlistDragItems_[i];
            if(state.playlist[i].id!=id || state.playlist[i].path!=path || playlist_[i].id!=id || playlist_[i].path!=path) {
                ResetPlaylistDrag();return false;
            }
            if(verifyVisual) {
                auto item=PlaylistView().Items().GetAt(static_cast<uint32_t>(i)).try_as<Controls::ListViewItem>();
                if(!item || unbox_value<std::int64_t>(item.Tag())!=id) {ResetPlaylistDrag();return false;}
            }
        }
        return !playlistDragRows_.empty();
    } catch(...) {ResetPlaylistDrag();return false;}
}
bool MainWindow::PlaylistDropTargetCurrent(bool verifyVisual) const {
    if(!PlaylistDragReady()) return false;
    auto const& items=session_->Snapshot().playlist;
    if(items.size()!=playlist_.size() || (verifyVisual && PlaylistView().Items().Size()!=items.size())) return false;
    for(std::size_t i=0;i<items.size();++i) {
        if(items[i].id!=playlist_[i].id || items[i].path!=playlist_[i].path) return false;
        if(verifyVisual) {
            auto item=PlaylistView().Items().GetAt(static_cast<uint32_t>(i)).try_as<Controls::ListViewItem>();
            if(!item || unbox_value<std::int64_t>(item.Tag())!=items[i].id) return false;
        }
    }
    return true;
}
bool MainWindow::CopyPlaylistDrag(MainWindow& target,DragEventArgs const& args,std::size_t row) {
    auto const& initial=target.session_->Snapshot();
    const auto revision=initial.playlistRevision,media=initial.mediaGeneration,intent=initial.mediaOpenIntentRevision;
    const auto input=target.inputGeneration_,edit=target.session_->PlaylistEditIntentRevision();
    if(!playlistDragCopyAllowed_ || !PlaylistDragCurrent(args,true) || !target.PlaylistDropTargetCurrent(true) || row>initial.playlist.size()) return false;
    std::vector<std::pair<std::int64_t,std::string>> identities;
    std::size_t bytes{};
    constexpr std::size_t budget=32*1024*1024,overhead=sizeof(std::pair<std::int64_t,std::string>);
    for(auto const& item:initial.playlist) {
        if(bytes>budget-overhead || item.path.size()>budget-overhead-bytes) {
            target.ShowError(L"目标列表过大，无法安全保留复制落点快照。");return false;
        }
        bytes+=item.path.size()+overhead;identities.emplace_back(item.id,item.path);
    }
    std::vector<std::string> paths;bytes=0;
    if(playlistDragRows_.empty() || playlistDragRows_.size()>1000) return false;
    for(auto index:playlistDragRows_) {
        if(index>=playlistDragItems_.size()) return false;
        auto const& path=playlistDragItems_[index].second;
        if(path.size()>1024*1024-bytes || !CopyablePlaylistPath(path)) return false;
        bytes+=path.size();paths.push_back(path); // Keep source visual order and duplicates.
    }
    target.CancelAutomaticFiles();
    auto const& now=target.session_->Snapshot();
    if(!PlaylistDragCurrent(args,true) || !target.PlaylistDropTargetCurrent(true) ||
        now.playlistRevision!=revision || now.mediaGeneration!=media || now.mediaOpenIntentRevision!=intent ||
        target.inputGeneration_!=input || target.session_->PlaylistEditIntentRevision()!=edit || now.playlist.size()!=identities.size()) return false;
    for(std::size_t i=0;i<identities.size();++i)
        if(now.playlist[i].id!=identities[i].first || now.playlist[i].path!=identities[i].second) return false;
    ResetPlaylistDrag(); // Consume the source capability before any synchronous Notify.
    return target.session_->InsertPlaylist(std::move(paths),row);
}
std::optional<std::size_t> MainWindow::PlaylistDropRow(DragEventArgs const& args) const {
    try {
    // Ignore scrollbar surfaces rather than interpreting a scroll operation as
    // a drop onto the nearest row. No automatic ListView model reordering.
    for (auto node = args.OriginalSource().try_as<DependencyObject>(); node && node != PlaylistView();
         node = Media::VisualTreeHelper::GetParent(node))
        if (node.try_as<Controls::Primitives::ScrollBar>()) return {};
    auto point = args.GetPosition(PlaylistView());
    if(!std::isfinite(point.X) || !std::isfinite(point.Y) || !std::isfinite(PlaylistView().ActualWidth()) ||
        !std::isfinite(PlaylistView().ActualHeight()) || PlaylistView().ActualWidth()<=0 || PlaylistView().ActualHeight()<=0) return {};
    if (point.X < 0 || point.Y < 0 || point.X > PlaylistView().ActualWidth() ||
        point.Y > PlaylistView().ActualHeight()) return {};
    if (playlist_.empty() && session_->Snapshot().playlist.empty()) return 0;
    std::optional<std::size_t> lastRealized, nextRealized;
    auto panel = PlaylistView().ItemsPanelRoot();
    if (!panel) return {};
    // Inspect only realized containers, not every item across the WinRT ABI
    // on every pointer movement in a virtualized long playlist.
    for (auto const& child : panel.Children()) {
        auto item = child.try_as<FrameworkElement>();
        if (!item || item.ActualHeight() <= 0) continue;
        auto index = PlaylistView().IndexFromContainer(item);
        if (index < 0 || static_cast<std::size_t>(index) >= playlist_.size()) continue;
        auto row = static_cast<std::size_t>(index);
        auto top = item.TransformToVisual(PlaylistView()).TransformPoint({0, 0}).Y;
        if(!std::isfinite(top) || !std::isfinite(item.ActualHeight())) return {};
        if (top + item.ActualHeight() < 0 || top > PlaylistView().ActualHeight()) continue;
        // Upper half inserts before this row; lower half inserts after it.
        if (point.Y < top + item.ActualHeight() / 2) {
            if (!nextRealized || row < *nextRealized) nextRealized = row;
        } else if (!lastRealized || row > *lastRealized) lastRealized = row;
    }
    if (nextRealized) return nextRealized;
    if (lastRealized) return *lastRealized + 1;
    return {}; // No realized rows: do not guess an off-screen target.
    } catch(...) {return {};}
}
void MainWindow::WirePlaylistDrag() {
    auto weak = get_weak();
    PlaylistView().DragItemsStarting([weak](auto const&, Controls::DragItemsStartingEventArgs const& args) {
        auto self = weak.get();
        if (!self) { args.Cancel(true); return; }
        self->ResetPlaylistDrag();
        try {
            if(!self->PlaylistDragReady()) {args.Cancel(true);return;}
            auto const& state=self->session_->Snapshot();
            if(state.playlist.size()!=self->playlist_.size() || self->PlaylistView().Items().Size()!=state.playlist.size()) {args.Cancel(true);return;}
            std::size_t bytes{};
            std::set<std::int64_t> ids;
            for(std::size_t i=0;i<state.playlist.size();++i) {
                auto const& entry=state.playlist[i];
                auto item=self->PlaylistView().Items().GetAt(static_cast<uint32_t>(i)).try_as<Controls::ListViewItem>();
                // Bound the extra retained identity snapshot without truncation.
                constexpr std::size_t budget=32*1024*1024;
                const auto overhead=sizeof(std::pair<std::int64_t,std::string>);
                if(bytes>budget-overhead || entry.path.size()>budget-overhead-bytes) {
                    args.Cancel(true);self->ShowError(L"播放列表过大，无法安全保留拖动快照；请改用队列按钮分批操作。");return;
                }
                if(entry.path.empty() || entry.id<0 || !ids.insert(entry.id).second ||
                    self->playlist_[i].id!=entry.id || self->playlist_[i].path!=entry.path ||
                    !item || unbox_value<std::int64_t>(item.Tag())!=entry.id) {args.Cancel(true);return;}
                bytes+=entry.path.size()+overhead;
            }
            auto rows = self->SelectedPlaylistRows();
            if (rows.empty() || rows.size() != args.Items().Size()) { args.Cancel(true); return; }
            // The drag framework and the selected UI rows must refer to exactly
            // the same entries; never silently move only a subset of a drag.
            std::set<std::int64_t> draggedIds;
            for (auto const& dragged : args.Items()) {
                auto item = dragged.try_as<Controls::ListViewItem>();
                if (!item) { args.Cancel(true); return; }
                auto id = unbox_value<std::int64_t>(item.Tag());
                if (!draggedIds.insert(id).second || std::none_of(rows.begin(), rows.end(), [&](auto row) { return self->playlist_[row].id == id; })) {
                    args.Cancel(true); return;
                }
            }
            GUID token{}; check_hresult(CoCreateGuid(&token));
            self->playlistDragToken_ = guid{token};
            self->playlistDragRows_ = std::move(rows);
            for (auto const& item : self->playlist_) self->playlistDragItems_.emplace_back(item.id,item.path);
            self->playlistDragRevision_=state.playlistRevision;
            self->playlistDragMedia_=state.mediaGeneration;self->playlistDragOpenIntent_=state.mediaOpenIntentRevision;
            self->playlistDragInput_=self->inputGeneration_;self->playlistDragEditIntent_=self->session_->PlaylistEditIntentRevision();
            // Only an opaque, per-drag capability leaves this window. File paths
            // and row indices are not placed on the OS drag clipboard.
            args.Data().Properties().Insert(DragKey, box_value(*self->playlistDragToken_));
            args.Data().SetData(DragKey, box_value(*self->playlistDragToken_));
            // Preflight copy once; DragOver never reparses every URL.
            self->playlistDragCopyAllowed_=self->playlistDragRows_.size()<=1000;
            std::size_t copyBytes{};
            try {
                for(auto row:self->playlistDragRows_) {
                    auto const& path=self->playlistDragItems_[row].second;
                    if(path.size()>1024*1024-copyBytes || !CopyablePlaylistPath(path)) {self->playlistDragCopyAllowed_=false;break;}
                    copyBytes+=path.size();
                }
            } catch(...) {self->playlistDragCopyAllowed_=false;}
            activePlaylistDrag=ActivePlaylistDrag{*self->playlistDragToken_,self->get_weak()};
            args.Data().RequestedOperation(DataPackageOperation::Move | DataPackageOperation::Copy);
        } catch (...) { self->ResetPlaylistDrag(); args.Cancel(true); }
    });
    PlaylistView().DragItemsCompleted([weak](auto const&, auto const&) {
        if (auto self = weak.get()) {
            self->ResetPlaylistDrag();
        }
    });
    PlaylistView().DragOver([weak](auto const&, DragEventArgs const& args) {
        try {
        if (!HasPlaylistDrag(args)) {
            args.AcceptedOperation(DataPackageOperation::None);
            if (auto self=weak.get()) try {
                if (self->session_->Snapshot().session==iima::SessionState::Ready &&
                    !self->inputResolving_ && self->pendingFiles_.empty() && self->activationInputs_.empty()) {
                    self->DragOverFiles(args);
                    if (auto row=self->PlaylistDropRow(args); row && args.AcceptedOperation()==DataPackageOperation::Copy) {
                        args.DragUIOverride().Caption(*row==self->playlist_.size() ? L"插入到列表末尾（不播放）" :
                            L"插入到第 " + to_hstring(*row+1) + L" 项之前（不播放）");
                        args.DragUIOverride().IsCaptionVisible(true);
                    } else args.AcceptedOperation(DataPackageOperation::None);
                }
            } catch (...) { args.AcceptedOperation(DataPackageOperation::None); }
            args.Handled(true); // Never fall back to opening/replacing from Root.
            return;
        }
        args.Handled(true); args.AcceptedOperation(DataPackageOperation::None);
        auto source=PlaylistDragSource(args);
        if(auto self=weak.get(); self && source && source->PlaylistDragCurrent(args)) {
            const bool same=source.get()==self.get();
            if(!same && (!source->playlistDragCopyAllowed_ || !self->PlaylistDropTargetCurrent(false))) return;
            if(auto row=self->PlaylistDropRow(args)) {
                args.AcceptedOperation(same ? DataPackageOperation::Move : DataPackageOperation::Copy);
                args.DragUIOverride().Caption(same ? (*row==self->playlist_.size() ? hstring(L"移动到列表末尾") :
                    L"移动到第 "+to_hstring(*row+1)+L" 项之前") :
                    (*row==self->playlist_.size() ? hstring(L"复制到此窗口列表末尾（不播放）") : L"复制到此窗口第 "+to_hstring(*row+1)+L" 项之前（不播放）"));
                args.DragUIOverride().IsCaptionVisible(true);
            }
        }
        } catch(...) {try {args.Handled(true);args.AcceptedOperation(DataPackageOperation::None);}catch(...) {}}
    });
    PlaylistView().Drop([weak](auto const&, DragEventArgs const& args) {
        try {
        if (!HasPlaylistDrag(args)) {
            try {
                if (auto self=weak.get()) {
                    if (auto row=self->PlaylistDropRow(args)) { self->DropFiles(args,*row); return; }
                }
            } catch (...) {}
            args.Handled(true); args.AcceptedOperation(DataPackageOperation::None);
            return;
        }
        args.Handled(true); args.AcceptedOperation(DataPackageOperation::None);
        auto source=PlaylistDragSource(args);
        if(auto self=weak.get();self && source) {
            const auto originalToken=unbox_value<guid>(args.DataView().Properties().Lookup(DragKey));
            auto consumeOriginal=[source,originalToken] {
                if(source->playlistDragToken_ && *source->playlistDragToken_==originalToken) source->ResetPlaylistDrag();
            };
            try {
                if(!source->PlaylistDragCurrent(args,true)) {
                    consumeOriginal();
                    if(!self->closing_) self->ShowError(L"拖动来源或播放列表已变化，请重新拖动。");
                    return;
                }
                const auto targetRevision=self->session_->Snapshot().playlistRevision;
                const auto targetInput=self->inputGeneration_,targetEdit=self->session_->PlaylistEditIntentRevision();
                const auto targetMedia=self->session_->Snapshot().mediaGeneration,targetOpen=self->session_->Snapshot().mediaOpenIntentRevision;
                auto row=self->PlaylistDropRow(args);
                if(!row || self->session_->Snapshot().playlistRevision!=targetRevision || self->inputGeneration_!=targetInput ||
                    self->session_->PlaylistEditIntentRevision()!=targetEdit || self->session_->Snapshot().mediaGeneration!=targetMedia ||
                    self->session_->Snapshot().mediaOpenIntentRevision!=targetOpen) {consumeOriginal();return;}
                const bool same=source.get()==self.get();
                bool submitted{};
                if(same) {
                    if(!source->PlaylistDragCurrent(args,true)) {consumeOriginal();return;}
                    auto rows=std::move(source->playlistDragRows_);
                    consumeOriginal();
                    submitted=self->session_->DropPlaylistItems(std::move(rows),*row);
                } else {
                    if(!source->playlistDragCopyAllowed_) {
                        consumeOriginal();
                        self->ShowError(L"此选择不能跨窗口复制：最多1000项／1MiB，仅普通绝对盘符路径或显式网络URL；不支持本地播放列表、设备路径或特殊协议。");return;
                    }
                    submitted=source->CopyPlaylistDrag(*self,args,*row);
                    consumeOriginal(); // Also consume on a rejected/no-op drop.
                }
                self->UpdatePlaylistSelection();
                if(submitted) args.AcceptedOperation(same ? DataPackageOperation::Move : DataPackageOperation::Copy);
                // Accepted means a fresh-read editing transaction was submitted,
                // never that all mpv mutations completed. Copy never removes source.
            } catch(std::exception const& error) {consumeOriginal();if(!self->closing_) self->ShowError(to_hstring(error.what()));}
              catch(hresult_error const& error) {consumeOriginal();if(!self->closing_) self->ShowError(error.message());}
              catch(...) {consumeOriginal();throw;}
        }
        } catch(...) {
            try {args.Handled(true);args.AcceptedOperation(DataPackageOperation::None);}catch(...) {}
        }
    });
}
}

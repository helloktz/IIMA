// IINA SidebarPlaylistPane.PlaylistCellView.update and PlayerCore.refreshCachedVideoInfo.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Windows adaptation: observed current position or one exact-path history sample.
// Never combine a historical position with a newly probed duration. No file IO:
// history is a prior observation, not proof that the file still has that identity.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <string_view>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::UpdatePlaylistProgress() {
    auto const& state=session_->Snapshot();
    if(playlist_.size()!=state.playlist.size() || PlaylistView().Items().Size()!=state.playlist.size() ||
        !std::equal(playlist_.begin(),playlist_.end(),state.playlist.begin(),[](auto const& a,auto const& b){
            return a.id==b.id && a.path==b.path;
        })) return;
    // Views into UI-owned history live only during this non-awaiting projection.
    std::unordered_map<std::string_view,iima::HistoryEntry const*> history;
    if(!closing_ && userDataLoaded_ && userData_.settings.recordHistory)
        for(auto const& entry:userData_.history) {
            auto [it,inserted]=history.try_emplace(entry.path,&entry);
            if(!inserted && entry.playedAt>it->second->playedAt) it->second=&entry;
        }
    const bool liveReady=!closing_ && state.session==iima::SessionState::Ready &&
        state.media==iima::MediaState::Loaded && !state.mediaOriginPending &&
        !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() && !session_->PlaylistEditBusy();
    for(std::size_t i=0;i<state.playlist.size();++i) {
        auto const& entry=state.playlist[i];
        auto item=PlaylistView().Items().GetAt(static_cast<uint32_t>(i)).try_as<Controls::ListViewItem>();
        if(!item || unbox_value<std::int64_t>(item.Tag())!=entry.id) continue;
        auto row=item.Content().try_as<Controls::Grid>();
        if(!row || row.Children().Size()!=3) continue;
        auto panel=row.Children().GetAt(2).try_as<Controls::StackPanel>();
        if(!panel || panel.Children().Size()!=2) continue;
        auto label=panel.Children().GetAt(0).try_as<Controls::TextBlock>();
        auto bar=panel.Children().GetAt(1).try_as<Controls::ProgressBar>();
        if(!label || !bar) continue;
        std::optional<double> position,duration;
        const bool current=entry.current || entry.id==state.entryId;
        if(current) {
            // During loading/ended/identity uncertainty do not substitute old history.
            if(liveReady && entry.current && entry.id==state.entryId && entry.path==state.path) {
                position=state.position; duration=state.duration;
            }
        } else if(auto found=history.find(entry.path);found!=history.end()) {
            position=found->second->position; duration=found->second->duration;
        }
        if(!position || !duration || !std::isfinite(*position) || *position<0 ||
            !std::isfinite(*duration) || *duration<=0) {
            panel.Visibility(Visibility::Collapsed);
            if(!label.Text().empty()) label.Text(L"");
            if(bar.Value()!=0) bar.Value(0);
            Automation::AutomationProperties::SetItemStatus(item,L"");
            Automation::AutomationProperties::SetName(bar,L"");
            continue;
        }
        // Divide only when position < duration, avoiding overflow for extreme values.
        const auto percent=*position>=*duration ? 100.0 : (*position / *duration)*100.0;
        const auto whole=static_cast<unsigned>(std::floor(percent));
        const auto text=(current ? hstring(L"当前进度 ") : hstring(L"上次进度 "))+to_hstring(whole)+L"%";
        if(label.Text()!=text) {
            label.Text(text);
            Automation::AutomationProperties::SetItemStatus(item,text);
            Automation::AutomationProperties::SetName(bar,text);
        }
        if(bar.Value()!=percent) bar.Value(percent);
        panel.Visibility(Visibility::Visible);
    }
}
}

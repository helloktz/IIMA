// IINA SidebarPlaylistPane.PlaylistCellView.update / PlayerCore.refreshCachedVideoInfo.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
// Use both title and artist, otherwise retain the observed queue title. Metadata
// is a bounded point-in-time local probe, not a file-identity watcher or a sort key.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <unordered_map>
#include <string_view>
using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace winrt::IIMA::implementation {
void MainWindow::UpdatePlaylistMetadata() {
    auto const& state=session_->Snapshot();
    if(playlist_.size()!=state.playlist.size() || PlaylistView().Items().Size()!=state.playlist.size() ||
        !std::equal(playlist_.begin(),playlist_.end(),state.playlist.begin(),[](auto const& a,auto const& b){
            return a.id==b.id && a.path==b.path;
        })) return;
    const bool enabled=!closing_ && userDataLoaded_ && preferences_.Boolean("prefetchPlaylistVideoDuration") &&
        preferences_.Boolean("playlistShowMetadata") &&
        (!preferences_.Boolean("playlistShowMetadataInMusicMode") || musicMode_.IsMusic()) &&
        playlistDurationInput_==inputGeneration_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty();
    std::unordered_map<std::int64_t,std::string_view> identities;
    if(enabled) for(auto const& entry:state.playlist) identities.emplace(entry.id,entry.path);
    for(std::size_t i=0;i<state.playlist.size();++i) {
        auto const& entry=state.playlist[i];
        auto item=PlaylistView().Items().GetAt(static_cast<uint32_t>(i)).try_as<Controls::ListViewItem>();
        if(!item || unbox_value<std::int64_t>(item.Tag())!=entry.id) continue;
        auto row=item.Content().try_as<Controls::Grid>();
        if(!row || row.Children().Size()!=4) continue;
        auto title=row.Children().GetAt(0).try_as<Controls::TextBlock>();
        auto artist=row.Children().GetAt(3).try_as<Controls::TextBlock>();
        if(!title || !artist) continue;
        auto slash=entry.path.find_last_of("/\\");
        auto display=entry.title.empty() ? entry.path.substr(slash==std::string::npos ? 0 : slash+1) : entry.title;
        hstring artistText;
        auto cached=playlistDurationCache_.find(entry.path);
        if(enabled && cached!=playlistDurationCache_.end() && cached->second.complete && cached->second.fingerprint &&
            cached->second.title && !cached->second.title->empty() && cached->second.artist && !cached->second.artist->empty() &&
            identities.contains(cached->second.entryId) && identities.at(cached->second.entryId)==entry.path) {
            display=*cached->second.title;
            artistText=to_hstring(*cached->second.artist);
        }
        auto text=(entry.current ? hstring(L"▶ ") : hstring{})+to_hstring(display);
        if(title.Text()!=text) title.Text(text);
        if(artist.Text()!=artistText) artist.Text(artistText);
        artist.Visibility(artistText.empty() ? Visibility::Collapsed : Visibility::Visible);
    }
}
}

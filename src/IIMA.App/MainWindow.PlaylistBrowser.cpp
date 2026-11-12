// IINA SidebarPlaylistPane.contextOpenInBrowser, constrained to one HTTP(S)
// resource on Windows. Copyright (c) 2016-2026 lhc and contributors.
// GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <algorithm>
#include <winrt/Windows.System.h>
using namespace winrt;
namespace {
Windows::Foundation::Uri BrowserMediaUri(std::string const& path) {
    if(path.empty() || path.size()>32768 ||
       std::any_of(path.begin(),path.end(),[](unsigned char c){return c<=32 || c==127;})) return nullptr;
    try {
        Windows::Foundation::Uri uri(to_hstring(path));
        auto scheme=to_string(uri.SchemeName());
        std::transform(scheme.begin(),scheme.end(),scheme.begin(),[](char c){return c>='A' && c<='Z' ? c+('a'-'A') : c;});
        if((scheme!="http" && scheme!="https") || uri.Host().empty() ||
            !uri.UserName().empty() || !uri.Password().empty()) return nullptr;
        // Reject even an empty userinfo delimiter, not just decoded credentials.
        auto authority=path.find("://");
        if(authority==std::string::npos) return nullptr;
        authority+=3;
        auto end=path.find_first_of("/?#",authority);
        auto at=path.find('@',authority);
        if(at!=std::string::npos && (end==std::string::npos || at<end)) return nullptr;
        return uri;
    } catch(...) { return nullptr; }
}
}
namespace winrt::IIMA::implementation {
void MainWindow::UpdatePlaylistBrowserControls() {
    auto ids=SelectedPlaylistIds();
    auto const& state=session_->Snapshot();
    bool enabled=!closing_ && userDataLoaded_ && !pickerOpen_ && !interactiveCrop_.Active() && !cropOriginal_ &&
        !playlistBrowserBusy_ && !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
        !session_->PlaylistEditBusy() && state.session==iima::SessionState::Ready && ids.size()==1;
    if(enabled) {
        auto item=std::find_if(state.playlist.begin(),state.playlist.end(),[&](auto const& value){return value.id==ids.front();});
        enabled=item!=state.playlist.end() && static_cast<bool>(BrowserMediaUri(item->path));
    }
    OpenPlaylistBrowserButton().IsEnabled(enabled);
}
fire_and_forget MainWindow::OpenSelectedPlaylistInBrowser() {
    auto lifetime=get_strong();
    if(closing_ || !userDataLoaded_ || pickerOpen_ || interactiveCrop_.Active() || cropOriginal_ || playlistBrowserBusy_ ||
        inputResolving_ || !pendingFiles_.empty() || !activationInputs_.empty()) co_return;
    auto rows=SelectedPlaylistRows();
    if(rows.size()!=1) co_return;
    auto const& state=session_->Snapshot();
    const auto item=state.playlist[rows.front()];
    auto uri=BrowserMediaUri(item.path);
    if(!uri) { ShowError(L"浏览器打开仅支持不含用户名或密码的单个HTTP(S)地址。"); co_return; }
    const auto revision=state.playlistRevision, intent=state.mediaOpenIntentRevision, generation=state.mediaGeneration;
    const auto input=inputGeneration_, edit=session_->PlaylistEditIntentRevision();
    const auto selection=playlistSubtitleSelectionRevision_;
    auto current=[this,item,revision,intent,generation,input,edit,selection] {
        auto const& latest=session_->Snapshot();
        auto ids=SelectedPlaylistIds();
        return !closing_ && userDataLoaded_ && !pickerOpen_ && !interactiveCrop_.Active() && !cropOriginal_ &&
            !inputResolving_ && pendingFiles_.empty() && activationInputs_.empty() &&
            !session_->PlaylistEditBusy() && latest.session==iima::SessionState::Ready &&
            inputGeneration_==input && latest.playlistRevision==revision &&
            latest.mediaOpenIntentRevision==intent && latest.mediaGeneration==generation &&
            session_->PlaylistEditIntentRevision()==edit && playlistSubtitleSelectionRevision_==selection &&
            ids.size()==1 && ids.front()==item.id &&
            std::any_of(latest.playlist.begin(),latest.playlist.end(),[&](auto const& value){return value.id==item.id && value.path==item.path;});
    };
    // There is no await between identity validation and this irreversible OS
    // handoff. Only the selected URL is passed; never shell syntax or commands.
    if(!current()) co_return;
    playlistBrowserBusy_=true;
    UpdatePlaylistBrowserControls();
    try {
        const bool accepted=co_await Windows::System::Launcher::LaunchUriAsync(uri);
        if(current()) ShowError(accepted ? L"已将所选地址交给默认浏览器；未验证页面是否加载。" : L"系统未打开浏览器。队列和当前播放未改变。");
    } catch(...) { if(current()) ShowError(L"无法将所选地址交给默认浏览器。队列和当前播放未改变。"); }
    playlistBrowserBusy_=false;
    if(!closing_) UpdatePlaylistBrowserControls();
}
}

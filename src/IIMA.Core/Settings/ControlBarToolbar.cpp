// IINA Preference.ToolBarButton and PrefOSCToolbarSettingsSheetController.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "ControlBarToolbar.h"
#include <algorithm>
#include <array>
namespace iima::settings {
namespace {
constexpr ToolbarAction available[]{ToolbarAction::Settings,ToolbarAction::Playlist,ToolbarAction::Pip,
    ToolbarAction::Fullscreen,ToolbarAction::MusicMode,ToolbarAction::SubtitleTrack,
    ToolbarAction::Screenshot,ToolbarAction::Plugins};
constexpr ToolbarAction defaults[]{ToolbarAction::Plugins,ToolbarAction::Pip,ToolbarAction::Playlist,ToolbarAction::Settings};
}
std::span<ToolbarAction const> AvailableToolbarActions() noexcept { return available; }
std::span<ToolbarAction const> DefaultToolbarActions() noexcept { return defaults; }
std::string_view ToolbarActionToken(ToolbarAction action) noexcept {
    switch(action) {
    case ToolbarAction::Settings:return "settings";
    case ToolbarAction::Playlist:return "playlist";
    case ToolbarAction::Pip:return "pip";
    case ToolbarAction::Fullscreen:return "fullscreen";
    case ToolbarAction::MusicMode:return "music-mode";
    case ToolbarAction::SubtitleTrack:return "sub-track";
    case ToolbarAction::Screenshot:return "screenshot";
    case ToolbarAction::Plugins:return "plugins";
    default:return {};
    }
}
std::optional<std::vector<ToolbarAction>> ParseToolbarActions(std::string_view text) {
    if(text.size()>128) return {};
    std::vector<ToolbarAction> result;
    if(text.empty()) return result;
    while(true) {
        auto end=text.find(',');auto token=text.substr(0,end);
        auto found=std::find_if(std::begin(available),std::end(available),[&](auto action){return ToolbarActionToken(action)==token;});
        if(found==std::end(available) || result.size()==MaxToolbarActions ||
            std::find(result.begin(),result.end(),*found)!=result.end()) return {};
        result.push_back(*found);
        if(end==std::string_view::npos) return result;
        text.remove_prefix(end+1);
    }
}
std::string SerializeToolbarActions(std::span<ToolbarAction const> actions) {
    std::array<ToolbarAction,MaxToolbarActions> seen{};std::size_t count{};
    std::string result;
    for(auto action:actions) {
        auto token=ToolbarActionToken(action);
        if(token.empty() || std::find(seen.begin(),seen.begin()+count,action)!=seen.begin()+count) continue;
        if(!result.empty()) result+=',';
        result+=token;seen[count++]=action;
        if(count==MaxToolbarActions) break;
    }
    return result;
}
}

// Adapted from IINA PlayerCore.getTrackInfo/getPlaylist/getChapters and MPVTrack.
// Copyright © 2016 lhc. All rights reserved. See references/iina/LICENSE (GPL-3.0).
#include "MpvNodes.h"
#include <cmath>
#include <cstring>
namespace iima {
namespace {
mpv_node const* Field(mpv_node const& node, char const* key) {
    if(node.format!=MPV_FORMAT_NODE_MAP || !node.u.list) return nullptr;
    auto const& list=*node.u.list;
    for(int i=0;i<list.num;++i)
        if(list.keys[i] && std::strcmp(list.keys[i],key)==0) return &list.values[i];
    return nullptr;
}
std::string Text(mpv_node const& node,char const* key) {
    auto p=Field(node,key); return p && p->format==MPV_FORMAT_STRING && p->u.string ? p->u.string : "";
}
std::optional<std::int64_t> Integer(mpv_node const& node,char const* key) {
    auto p=Field(node,key); return p && p->format==MPV_FORMAT_INT64 ? std::optional<std::int64_t>{p->u.int64} : std::nullopt;
}
bool Flag(mpv_node const& node,char const* key) {
    auto p=Field(node,key); return p && p->format==MPV_FORMAT_FLAG && p->u.flag!=0;
}
}
void CopyCollectionProperty(MpvEvent& e, mpv_node const& root) {
    if(root.format!=MPV_FORMAT_NODE_ARRAY || !root.u.list) return;
    for(int i=0;i<root.u.list->num;++i) {
        auto const& node=root.u.list->values[i];
        if(e.name=="playlist") {
            auto id=Integer(node,"id"); if(!id) continue;
            e.playlist.push_back({*id,Text(node,"filename"),Text(node,"title"),Flag(node,"current")});
        } else if(e.name=="track-list") {
            auto id=Integer(node,"id"); auto type=Text(node,"type");
            if(!id || (type!="audio" && type!="video" && type!="sub")) continue;
            e.tracks.push_back({*id,type,Text(node,"title"),Text(node,"lang"),Text(node,"external-filename"),Flag(node,"selected"),Flag(node,"external")});
        } else if(e.name=="chapter-list") {
            auto time=Field(node,"time");
            if(!time || time->format!=MPV_FORMAT_DOUBLE || !std::isfinite(time->u.double_) || time->u.double_<0) continue;
            e.chapters.push_back({i,time->u.double_,Text(node,"title")});
        }
    }
}
}

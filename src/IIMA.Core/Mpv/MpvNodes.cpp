// Adapted from IINA PlayerCore.getTrackInfo/getPlaylist/getChapters and MPVTrack.
// Copyright © 2016 lhc. All rights reserved. See references/iina/LICENSE (GPL-3.0).
#include "MpvNodes.h"
#include "../Media/TrackPresentation.h"
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <locale>
#include <algorithm>
#include <utility>
#include <set>
namespace iima {
namespace {
std::optional<std::size_t> BoundedLength(const char* text, std::size_t maximum) {
    if (!text) return std::nullopt;
    for (std::size_t length = 0; length <= maximum; ++length)
        if (!text[length]) return length;
    return std::nullopt;
}
mpv_node const* Field(mpv_node const& node, char const* key) {
    if(node.format!=MPV_FORMAT_NODE_MAP || !node.u.list || node.u.list->num < 0 ||
       !node.u.list->keys || !node.u.list->values) return nullptr;
    auto const& list=*node.u.list;
    for(int i=0;i<std::min(list.num,1024);++i)
        if(BoundedLength(list.keys[i],1024) && std::strcmp(list.keys[i],key)==0) return &list.values[i];
    return nullptr;
}
std::string Text(mpv_node const& node,char const* key) {
    auto p=Field(node,key); return p && p->format==MPV_FORMAT_STRING && p->u.string ? p->u.string : "";
}
std::string BoundedText(mpv_node const& node, char const* key, std::size_t maximum) {
    auto field=Field(node,key);
    if(!field || field->format!=MPV_FORMAT_STRING) return {};
    auto length=BoundedLength(field->u.string,maximum);
    return length ? std::string(field->u.string,*length) : std::string{};
}
std::optional<std::int64_t> Integer(mpv_node const& node,char const* key) {
    auto p=Field(node,key); return p && p->format==MPV_FORMAT_INT64 ? std::optional<std::int64_t>{p->u.int64} : std::nullopt;
}
bool Flag(mpv_node const& node,char const* key) {
    auto p=Field(node,key); return p && p->format==MPV_FORMAT_FLAG && p->u.flag!=0;
}
}
// Unlike display parsing, absence from this list is used to restore selection.
// Reject the entire result on malformed/duplicate identities or exceeded limits.
bool CopySubtitleStateTracks(MpvEvent& event, mpv_node const& root) {
    event.tracks.clear();
    if(root.format!=MPV_FORMAT_NODE_ARRAY || !root.u.list || root.u.list->num<0 ||
       root.u.list->num>256 || (root.u.list->num && !root.u.list->values)) return false;
    std::vector<MediaTrack> tracks;
    std::set<std::pair<std::string,std::int64_t>> identities;
    std::set<std::string> paths;
    std::size_t budget=1024*1024;
    for(int i=0;i<root.u.list->num;++i) {
        auto const& node=root.u.list->values[i];
        if(node.format!=MPV_FORMAT_NODE_MAP || !node.u.list || node.u.list->num<0 ||
           node.u.list->num>128 || !node.u.list->keys || !node.u.list->values) return false;
        std::set<std::string> keys;
        for(int k=0;k<node.u.list->num;++k) {
            auto length=BoundedLength(node.u.list->keys[k],128);
            if(!length || !*length || !keys.emplace(node.u.list->keys[k],*length).second) return false;
        }
        auto id=Field(node,"id"), type=Field(node,"type"), external=Field(node,"external");
        if(!id || id->format!=MPV_FORMAT_INT64 || id->u.int64<=0 || !type ||
           type->format!=MPV_FORMAT_STRING || !external || external->format!=MPV_FORMAT_FLAG ||
           (external->u.flag!=0 && external->u.flag!=1)) return false;
        auto length=BoundedLength(type->u.string,16);
        if(!length) return false;
        std::string kind(type->u.string,*length);
        if(kind!="sub" && kind!="audio" && kind!="video") return false;
        if(!identities.emplace(kind,id->u.int64).second) return false;
        if(kind!="sub") continue;
        if(tracks.size()>=64) return false;
        MediaTrack track;track.id=id->u.int64;track.type="sub";track.external=external->u.flag!=0;
        auto filename=Field(node,"external-filename");
        if(filename) {
            if(filename->format!=MPV_FORMAT_STRING) return false;
            auto size=BoundedLength(filename->u.string,131072);
            if(!size || *size>budget) return false;
            budget-=*size;track.externalPath.assign(filename->u.string,*size);
        }
        if(track.external && (track.externalPath.empty() || !paths.insert(track.externalPath).second)) return false;
        if(!track.external && !track.externalPath.empty()) return false;
        tracks.push_back(std::move(track));
    }
    event.tracks=std::move(tracks);
    return true;
}
void CopyCollectionProperty(MpvEvent& e, mpv_node const& root) {
    if (e.name == "vf" || e.name == "af") {
        e.filters = ParseFilterChain(root);
        return;
    }
    if (e.name == "metadata" || e.name == "video-params" || e.name == "video-out-params" || e.name == "osd-dimensions" || e.name == "audio-params") {
        if (root.format != MPV_FORMAT_NODE_MAP || !root.u.list || root.u.list->num < 0 ||
            !root.u.list->keys || !root.u.list->values) return;
        std::size_t budget = 256 * 1024;
        for (int index = 0; index < std::min(root.u.list->num, 1024); ++index) {
            auto key = root.u.list->keys[index];
            if (!key) continue;
            auto keyLength = BoundedLength(key,1024);
            if (!keyLength) continue;
            auto const& value = root.u.list->values[index];
            std::string text;
            if (value.format == MPV_FORMAT_STRING && value.u.string) {
                auto length = BoundedLength(value.u.string,16384);
                if (!length) continue;
                text.assign(value.u.string, *length);
            } else if (value.format == MPV_FORMAT_INT64) text = std::to_string(value.u.int64);
            else if (value.format == MPV_FORMAT_DOUBLE && std::isfinite(value.u.double_)) {
                std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(9) << value.u.double_; text = out.str();
            } else if (value.format == MPV_FORMAT_FLAG) text = value.u.flag ? "yes" : "no";
            else continue;
            if (*keyLength + text.size() > budget) break;
            budget -= *keyLength + text.size();
            e.properties.insert_or_assign(std::string(key, *keyLength), std::move(text));
        }
        return;
    }
    if(root.format!=MPV_FORMAT_NODE_ARRAY || !root.u.list || root.u.list->num<0 ||
       (root.u.list->num && !root.u.list->values)) return;
    if (e.name=="audio-device-list") {
        // Absence from a partial list cannot prove that a device disappeared.
        if (root.u.list->num > 1024) return;
        std::set<std::string, std::less<>> names;
        for (int i=0; i<root.u.list->num; ++i) {
            auto const& node=root.u.list->values[i];
            auto name=BoundedText(node,"name",4096);
            auto description=BoundedText(node,"description",4096);
            auto descField=Field(node,"description");
            if (name.empty() || !names.insert(name).second || !descField ||
                descField->format!=MPV_FORMAT_STRING || !BoundedLength(descField->u.string,4096)) {
                e.audioDevices.clear(); return;
            }
            e.audioDevices.push_back({std::move(name),std::move(description)});
        }
        e.audioDevicesComplete=true;
        return;
    }
    const int count=root.u.list->num;
    for(int i=0;i<count;++i) {
        auto const& node=root.u.list->values[i];
        if(e.name=="playlist") {
            auto id=Integer(node,"id"); if(!id) continue;
            e.playlist.push_back({*id,Text(node,"filename"),Text(node,"title"),Flag(node,"current")});
        } else if(e.name=="track-list") {
            auto id=Integer(node,"id"); auto type=Text(node,"type");
            if(!id || (type!="audio" && type!="video" && type!="sub")) continue;
            auto details = TrackPresentation::FromNode(node);
            if (!details) continue;
            MediaTrack track;
            track.id = details->id; track.type = type;
            track.title = details->title.value_or(""); track.lang = details->language.value_or("");
            track.externalPath = details->externalFilename.value_or("");
            track.selected = details->selected; track.external = details->external;
            track.sourceId = details->sourceId; track.width = details->width; track.height = details->height;
            track.channelCount = details->channelCount; track.sampleRate = details->sampleRate;
            track.codec = details->codec; track.decoderDescription = details->decoderDescription;
            track.channelLayout = details->channelLayout; track.framesPerSecond = details->framesPerSecond;
            track.isDefault = details->isDefault; track.forced = details->forced;
            track.image = details->image; track.albumArt = details->albumArt;
            e.tracks.push_back(std::move(track));
        } else if(e.name=="chapter-list") {
            auto time=Field(node,"time");
            if(!time || time->format!=MPV_FORMAT_DOUBLE || !std::isfinite(time->u.double_) || time->u.double_<0) continue;
            e.chapters.push_back({i,time->u.double_,Text(node,"title")});
        }
    }
}
}

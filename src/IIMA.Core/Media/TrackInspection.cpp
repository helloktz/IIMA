// IINA InspectorWindowController.TracksVC / MPVTrack.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "TrackInspection.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
namespace iima {
namespace {
std::string Display(std::string_view source,std::size_t limit,bool& truncated) {
    const auto length=std::min(source.size(),limit);
    if(source.size()>limit) truncated=true;
    std::string out;
    for (std::size_t i=0;i<length;) {
        const auto first=static_cast<unsigned char>(source[i]);
        unsigned count=first<0x80 ? 1 : first>=0xc2 && first<=0xdf ? 2 : first>=0xe0 && first<=0xef ? 3 : first>=0xf0 && first<=0xf4 ? 4 : 0;
        std::uint32_t code=count==1 ? first : count ? first & ((1u<<(7-count))-1) : 0;
        bool valid=count && i+count<=length;
        for (unsigned n=1;valid && n<count;++n) {
            auto c=static_cast<unsigned char>(source[i+n]);
            if ((c&0xc0)!=0x80) valid=false; else code=(code<<6)|(c&0x3f);
        }
        valid=valid && !(count==2 && code<0x80) && !(count==3 && code<0x800) &&
            !(count==4 && code<0x10000) && code<=0x10ffff && !(code>=0xd800 && code<=0xdfff);
        std::string_view piece;
        if (!valid) { piece="\xef\xbf\xbd";count=1; }
        else if (code<32 || (code>=0x7f && code<=0x9f) || code==0x61c || code==0x200e || code==0x200f || code==0x2028 || code==0x2029 ||
            (code>=0x202a && code<=0x202e) || (code>=0x2066 && code<=0x2069)) {
            piece=code=='\n' ? "\\n" : code=='\r' ? "\\r" : code=='\t' ? "\\t" : "?";
        } else piece=source.substr(i,count);
        if (piece.size()>limit-out.size()) { truncated=true;break; }
        out+=piece;i+=count;
    }
    return out;
}
TrackInspection Inspect(MediaTrack const& track) {
    TrackInspection result;result.identity={track.type,track.id};
    auto field=[&](std::string key,std::string_view value) {
        result.fields.emplace_back(std::move(key),value.empty() ? "Unknown" : Display(value,1024,result.truncated));
    };
    auto integer=[&](std::string key,std::optional<std::int64_t> value) {
        field(std::move(key),value ? std::to_string(*value) : "Unknown");
    };
    auto positive=[&](std::string key,std::optional<std::int64_t> value) {
        integer(std::move(key),value && *value>0 ? value : std::nullopt);
    };
    auto flag=[&](std::string key,bool value) {field(std::move(key),value ? "true" : "false");};
    field("Type",track.type);integer("ID",track.id);integer("Source ID",track.sourceId);
    field("Title",track.title);field("Language",track.lang);field("External path",track.externalPath);
    field("Codec",track.codec ? std::string_view(*track.codec) : std::string_view{});field("Decoder",track.decoderDescription ? std::string_view(*track.decoderDescription) : std::string_view{});
    std::string fps;
    if(track.framesPerSecond && std::isfinite(*track.framesPerSecond) && *track.framesPerSecond>0) {
        std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::setprecision(12)<<*track.framesPerSecond;fps=stream.str();
    }
    field("Frames per second",fps);positive("Width",track.width);positive("Height",track.height);
    positive("Channels",track.channelCount);field("Channel layout",track.channelLayout ? std::string_view(*track.channelLayout) : std::string_view{});
    positive("Sample rate",track.sampleRate);
    flag("Default",track.isDefault);flag("Forced",track.forced);flag("Selected",track.selected);
    flag("External",track.external);flag("Image",track.image);flag("Album art",track.albumArt);
    result.label=track.type+" #"+std::to_string(track.id);
    if(!track.title.empty()) result.label+=" "+Display(track.title,1024,result.truncated);
    return result;
}
}
std::optional<std::vector<TrackInspection>> InspectTracks(std::span<MediaTrack const> tracks) {
    if(tracks.size()>256) return {};
    std::set<std::pair<std::string,std::int64_t>> identities;
    for(auto const& track:tracks) {
        if(track.id<0 || (track.type!="video" && track.type!="audio" && track.type!="sub") ||
            !identities.emplace(track.type,track.id).second) return {};
    }
    std::vector<TrackInspection> result;result.reserve(tracks.size());
    for(auto const& track:tracks) result.push_back(Inspect(track));
    return result;
}
std::optional<TrackInspection> InspectTrack(std::span<MediaTrack const> tracks,TrackIdentity const& identity) {
    auto inspected=InspectTracks(tracks);if(!inspected) return {};
    for(auto& track:*inspected) if(track.identity==identity) return std::move(track);
    return {};
}
std::string TrackInspection::Report() const {
    std::string result="Track inspection (observed metadata)\n";
    // Bound even caller-constructed values; never concatenate raw payloads.
    bool shortened=truncated;
    for(std::size_t n=0;n<std::min<std::size_t>(fields.size(),24);++n)
        result+=Display(fields[n].first,64,shortened)+": "+Display(fields[n].second,1024,shortened)+"\n";
    if(shortened || fields.size()>24) result+="[Some fields were truncated]\n";
    return result;
}
}

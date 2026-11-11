#include "../Mpv/MpvNodes.h"
#include <cassert>
using namespace iima;
int main() {
    char idKey[]="id", filenameKey[]="filename", currentKey[]="current", filename[]="C:/中文.mp4";
    char* keys[]={idKey,filenameKey,currentKey};
    mpv_node values[3]{};
    values[0].format=MPV_FORMAT_INT64; values[0].u.int64=42;
    values[1].format=MPV_FORMAT_STRING; values[1].u.string=filename;
    values[2].format=MPV_FORMAT_FLAG; values[2].u.flag=1;
    mpv_node_list map{3,values,keys}; mpv_node row{}; row.format=MPV_FORMAT_NODE_MAP; row.u.list=&map;
    mpv_node_list list{1,&row,nullptr}; mpv_node root{}; root.format=MPV_FORMAT_NODE_ARRAY; root.u.list=&list;
    MpvEvent event{MpvEvent::Kind::Property}; event.name="playlist";
    CopyCollectionProperty(event,root);
    filename[0]='X'; values[0].u.int64=0; // Owned copy survives mpv's borrowed storage mutation.
    assert(event.playlist.size()==1 && event.playlist[0].id==42 && event.playlist[0].current);
    assert(event.playlist[0].path=="C:/中文.mp4");
    event.playlist.clear(); values[0].format=MPV_FORMAT_NONE;
    CopyCollectionProperty(event,root); assert(event.playlist.empty());
    root.format=MPV_FORMAT_NONE; CopyCollectionProperty(event,root); assert(event.playlist.empty());
    root.format=MPV_FORMAT_NODE_ARRAY;
    char typeKey[]="type", selectedKey[]="selected", sub[]="sub";
    keys[1]=typeKey; keys[2]=selectedKey;
    values[0].format=MPV_FORMAT_INT64; values[0].u.int64=7;
    values[1].u.string=sub;
    event.name="track-list"; CopyCollectionProperty(event,root);
    assert(event.tracks.size()==1 && event.tracks[0].id==7 && event.tracks[0].type=="sub" && event.tracks[0].selected);
    char timeKey[]="time", titleKey[]="title", title[]="Intro";
    keys[0]=timeKey; keys[1]=titleKey;
    values[0].format=MPV_FORMAT_DOUBLE; values[0].u.double_=12.5;
    values[1].u.string=title;
    event.name="chapter-list"; CopyCollectionProperty(event,root);
    title[0]='X'; assert(event.chapters.size()==1 && event.chapters[0].index==0 && event.chapters[0].time==12.5 && event.chapters[0].title=="Intro");
    event.chapters.clear(); values[0].u.double_=-1; CopyCollectionProperty(event,root); assert(event.chapters.empty());
}

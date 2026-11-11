#include "../Playback/PlaybackState.h"
#include <cassert>
#include <limits>
using namespace iima;
int main() {
    PlaybackSnapshot s;
    ApplyPlaybackEvent(s,{MpvEvent::Kind::Ready});
    assert(s.session==SessionState::Ready && !s.duration && !s.position);
    MpvEvent start{MpvEvent::Kind::StartFile}; start.entryId=17;
    ApplyPlaybackEvent(s,start);
    assert(s.media==MediaState::Loading && s.entryId==17);
    MpvEvent duration{MpvEvent::Kind::Property}; duration.name="duration"; duration.number=60;
    ApplyPlaybackEvent(s,duration); assert(s.duration==60);
    duration.number.reset(); ApplyPlaybackEvent(s,duration); assert(!s.duration);
    duration.number=std::numeric_limits<double>::infinity(); ApplyPlaybackEvent(s,duration); assert(!s.duration);
    MpvEvent pause{MpvEvent::Kind::Property}; pause.name="pause"; pause.flag=false;
    ApplyPlaybackEvent(s,pause); assert(!s.paused);
    ApplyPlaybackEvent(s,{MpvEvent::Kind::Loaded}); assert(s.media==MediaState::Loaded);
    ApplyPlaybackEvent(s,{MpvEvent::Kind::Seek}); assert(s.seeking);
    ApplyPlaybackEvent(s,{MpvEvent::Kind::Restart}); assert(!s.seeking);
    MpvEvent oldEnd{MpvEvent::Kind::EndFile}; oldEnd.entryId=16; oldEnd.error=-1;
    ApplyPlaybackEvent(s,oldEnd); assert(s.media==MediaState::Loaded);
    oldEnd.entryId=17; oldEnd.text="bad file";
    ApplyPlaybackEvent(s,oldEnd); assert(s.media==MediaState::Error && s.error=="bad file");
    s.swapChain=std::make_shared<int>(1);
    start.entryId=18; ApplyPlaybackEvent(s,start);
    assert(!s.swapChain);
    assert(s.media==MediaState::Loading && s.error.empty() && !s.position && !s.duration);
    MpvEvent reply{MpvEvent::Kind::CommandReply}; reply.error=0;
    ApplyPlaybackEvent(s,reply); assert(s.media==MediaState::Loading);
    MpvEvent property{MpvEvent::Kind::Property}; property.name="loop-playlist"; property.text="inf";
    ApplyPlaybackEvent(s,property); assert(s.repeatMode==RepeatMode::Playlist);
    property.name="loop-file"; property.text="2"; ApplyPlaybackEvent(s,property); assert(s.repeatMode==RepeatMode::File);
    property.text="no"; ApplyPlaybackEvent(s,property); assert(s.repeatMode==RepeatMode::Playlist);
    property.name="loop-playlist"; property.text="no"; ApplyPlaybackEvent(s,property); assert(s.repeatMode==RepeatMode::Off);
    property.name="mute"; property.flag=true; ApplyPlaybackEvent(s,property); assert(s.muted);
    property.name="sub-visibility"; property.flag=false; ApplyPlaybackEvent(s,property); assert(!s.subtitlesVisible);
    property.name="audio-delay"; property.number=-1.25; ApplyPlaybackEvent(s,property); assert(s.audioDelay==-1.25);
    property.number=std::numeric_limits<double>::infinity(); ApplyPlaybackEvent(s,property); assert(s.audioDelay==-1.25);
    property.name="volume-max"; property.number=200; ApplyPlaybackEvent(s,property); assert(s.maxVolume==200);
    auto revision=s.playlistRevision;
    MpvEvent list{MpvEvent::Kind::Property}; list.name="playlist";
    list.playlist={{1,"one","",true}};
    ApplyPlaybackEvent(s,list); assert(s.playlistRevision==revision+1);
    ApplyPlaybackEvent(s,duration); assert(s.playlistRevision==revision+1);
    auto tracksRevision=s.tracksRevision;
    ApplyPlaybackEvent(s,start); assert(s.tracksRevision==tracksRevision+1);
    s.session=SessionState::Closing;
    ApplyPlaybackEvent(s,{MpvEvent::Kind::Loaded}); assert(s.media==MediaState::Loading);
    ApplyPlaybackEvent(s,{MpvEvent::Kind::Ready}); assert(s.session==SessionState::Closing);
    s.session=SessionState::Closed; ApplyPlaybackEvent(s,start); assert(s.entryId==18);
}

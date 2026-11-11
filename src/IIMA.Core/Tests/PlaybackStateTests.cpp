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
    start.entryId=18; ApplyPlaybackEvent(s,start);
    assert(s.media==MediaState::Loading && s.error.empty() && !s.position && !s.duration);
    MpvEvent reply{MpvEvent::Kind::CommandReply}; reply.error=0;
    ApplyPlaybackEvent(s,reply); assert(s.media==MediaState::Loading);
    s.session=SessionState::Closing;
    ApplyPlaybackEvent(s,{MpvEvent::Kind::Loaded}); assert(s.media==MediaState::Loading);
    ApplyPlaybackEvent(s,{MpvEvent::Kind::Ready}); assert(s.session==SessionState::Closing);
    s.session=SessionState::Closed; ApplyPlaybackEvent(s,start); assert(s.entryId==18);
}

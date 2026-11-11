// Copyright © 2016 lhc. All rights reserved. Adapted logic under IINA GPL-3.0.
// Behavior adapted from IINA PlayerCore.swift, MPVController.handleEvent and
// PlaybackInfo.swift; see docs/PORTING.md and references/iina/LICENSE.
#include "PlaybackState.h"
#include <cmath>

namespace iima {
void ApplyPlaybackEvent(PlaybackSnapshot& s, MpvEvent const& e) {
    if(s.session==SessionState::Closing || s.session==SessionState::Closed) return;
    switch(e.kind) {
    case MpvEvent::Kind::Ready:
        if(s.session==SessionState::Initializing) s.session=SessionState::Ready;
        break;
    case MpvEvent::Kind::Failure:
        s.session=SessionState::Failed; s.error=e.text; break;
    case MpvEvent::Kind::Shutdown:
        s.session=SessionState::Closing; break;
    case MpvEvent::Kind::StartFile:
        s.media=MediaState::Loading; s.entryId=e.entryId;
        s.position.reset(); s.duration.reset(); s.path.clear();
        s.seeking=false; s.buffering=false; s.error.clear(); break;
    case MpvEvent::Kind::Loaded:
        if(s.media==MediaState::Loading) s.media=MediaState::Loaded;
        break;
    case MpvEvent::Kind::EndFile:
        // END_FILE IDs belong to the entry being ended, not the latest UI request.
        if(e.entryId!=s.entryId) break;
        s.seeking=false; s.buffering=false;
        if(e.error<0) { s.media=MediaState::Error; s.error=e.text; }
        else s.media=MediaState::Ended;
        break;
    case MpvEvent::Kind::Seek: s.seeking=true; break;
    case MpvEvent::Kind::Restart: s.seeking=false; break;
    case MpvEvent::Kind::Property:
        if(e.name=="time-pos") s.position=e.number && std::isfinite(*e.number) ? e.number : std::nullopt;
        else if(e.name=="duration") s.duration=e.number && std::isfinite(*e.number) && *e.number>=0 ? e.number : std::nullopt;
        else if(e.name=="pause" && e.flag) s.paused=*e.flag;
        else if(e.name=="paused-for-cache" && e.flag) s.buffering=*e.flag;
        else if(e.name=="volume" && e.number && std::isfinite(*e.number)) s.volume=*e.number;
        else if(e.name=="speed" && e.number && std::isfinite(*e.number)) s.speed=*e.number;
        else if(e.name=="path") s.path=e.text;
        break;
    case MpvEvent::Kind::CommandReply:
        if(e.error<0) s.error=e.text;
        break;
    case MpvEvent::Kind::VideoReconfig: break;
    }
}
}

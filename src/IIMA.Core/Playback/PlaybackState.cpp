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
        if(s.session==SessionState::Initializing) { s.session=SessionState::Ready; s.compositionAvailable=e.flag.value_or(false); s.clientApiVersion=static_cast<std::uint64_t>(e.integer.value_or(0)); }
        break;
    case MpvEvent::Kind::Failure:
        s.session=SessionState::Failed; s.error=e.text; s.swapChain.reset(); break;
    case MpvEvent::Kind::Shutdown:
        s.session=SessionState::Closing; s.swapChain.reset(); break;
    case MpvEvent::Kind::StartFile:
        s.media=MediaState::Loading; s.entryId=e.entryId; s.swapChain.reset();
        s.position.reset(); s.duration.reset(); s.path.clear();
        s.seeking=false; s.buffering=false; s.error.clear();
        s.tracks.clear(); s.chapters.clear(); s.chapterIndex.reset();
        s.videoCodec.clear(); s.hwdecCurrent.clear(); s.currentVo.clear(); s.voConfigured=false; break;
    case MpvEvent::Kind::Loaded:
        if(s.media==MediaState::Loading) s.media=MediaState::Loaded;
        s.swapChain=e.swapChain;
        if(!s.swapChain && !e.text.empty()) s.error=e.text;
        else if(s.swapChain && s.error.starts_with("Composition output")) s.error.clear();
        break;
    case MpvEvent::Kind::EndFile:
        // END_FILE IDs belong to the entry being ended, not the latest UI request.
        if(e.entryId!=s.entryId) break;
        s.swapChain.reset();
        s.seeking=false; s.buffering=false;
        if(e.error<0) { s.media=MediaState::Error; s.error=e.text; }
        else s.media=MediaState::Ended;
        break;
    case MpvEvent::Kind::Seek: s.seeking=true; break;
    case MpvEvent::Kind::Restart: s.seeking=false; break;
    case MpvEvent::Kind::Property:
        if(e.name=="playlist") s.playlist=e.playlist;
        else if(e.name=="track-list") s.tracks=e.tracks;
        else if(e.name=="chapter-list") s.chapters=e.chapters;
        else if(e.name=="chapter") s.chapterIndex=e.integer;
        else if(e.name=="time-pos") s.position=e.number && std::isfinite(*e.number) ? e.number : std::nullopt;
        else if(e.name=="duration") s.duration=e.number && std::isfinite(*e.number) && *e.number>=0 ? e.number : std::nullopt;
        else if(e.name=="pause" && e.flag) s.paused=*e.flag;
        else if(e.name=="paused-for-cache" && e.flag) s.buffering=*e.flag;
        else if(e.name=="volume" && e.number && std::isfinite(*e.number)) s.volume=*e.number;
        else if(e.name=="speed" && e.number && std::isfinite(*e.number)) s.speed=*e.number;
        else if(e.name=="path") s.path=e.text;
        else if(e.name=="mpv-version") s.mpvVersion=e.text;
        else if(e.name=="current-tracks/video/codec-desc") s.videoCodec=e.text;
        else if(e.name=="hwdec-current") s.hwdecCurrent=e.text;
        else if(e.name=="current-vo") s.currentVo=e.text;
        else if(e.name=="vo-configured") s.voConfigured=e.flag.value_or(false);
        break;
    case MpvEvent::Kind::CommandReply:
        if(e.error<0) s.error=e.text;
        break;
    case MpvEvent::Kind::VideoReconfig:
        s.swapChain=e.swapChain;
        if(!s.swapChain && !e.text.empty()) s.error=e.text;
        else if(s.swapChain && s.error.starts_with("Composition output")) s.error.clear();
        break;
    }
}
}

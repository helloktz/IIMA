// Copyright © 2016 lhc. All rights reserved. Adapted logic under IINA GPL-3.0.
// Behavior adapted from IINA PlayerCore.swift, MPVController.handleEvent and
// PlaybackInfo.swift; see docs/PORTING.md and references/iina/LICENSE.
#include "PlaybackState.h"
#include <cmath>
#include <charconv>
#include <string_view>

namespace iima {
namespace {
bool LoopEnabled(std::string_view value) {
    if(value=="inf" || value=="force" || value=="yes") return true;
    std::int64_t count{}; auto result=std::from_chars(value.data(),value.data()+value.size(),count);
    return result.ec==std::errc{} && result.ptr==value.data()+value.size() && count!=0;
}
}

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
        s.seekable=false; s.mediaTitle.clear(); s.metadataTitle.clear(); s.metadataAlbum.clear(); s.metadataArtist.clear();
        s.metadata.clear(); s.videoParameters.clear(); s.audioParameters.clear();
        s.videoBitrate.reset(); s.audioBitrate.reset(); s.fileSize.reset(); ++s.informationRevision;
        s.seeking=false; s.buffering=false; s.error.clear();
        s.tracks.clear(); ++s.tracksRevision; s.chapters.clear(); ++s.chaptersRevision; s.chapterIndex.reset();
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
        if (e.name == "vf" || e.name == "af") {
            auto& filters = e.name == "vf" ? s.videoFilters : s.audioFilters;
            auto revision = filters.revision + 1;
            filters = e.filters;
            filters.revision = revision;
        }
        else if (e.name == "brightness" || e.name == "contrast" || e.name == "saturation" ||
                 e.name == "gamma" || e.name == "hue") {
            // SidebarVideoPane.EqualizerView consumes observed values. Do not
            // clamp invalid replies: an invalid/absent observation is unknown.
            auto& value = e.name == "brightness" ? s.videoBrightness :
                          e.name == "contrast" ? s.videoContrast :
                          e.name == "saturation" ? s.videoSaturation :
                          e.name == "gamma" ? s.videoGamma : s.videoHue;
            if (e.error >= 0 && e.integer && *e.integer >= -100 && *e.integer <= 100)
                value = static_cast<int>(*e.integer);
            else value.reset();
        }
        else if (e.name == "deinterlace") {
            s.deinterlace.reset(); s.deinterlaceMode.reset();
            if (e.error >= 0 && (e.text == "yes" || e.text == "no" || e.text == "auto")) {
                s.deinterlaceMode = e.text;
                if (e.text != "auto") s.deinterlace = e.text == "yes";
            }
        }
        else if(e.name=="metadata") {
            s.metadata=e.properties;
            s.metadataTitle=MetadataValue(s.metadata,"title");
            s.metadataAlbum=MetadataValue(s.metadata,"album");
            s.metadataArtist=MetadataValue(s.metadata,"artist");
            ++s.informationRevision;
        }
        else if(e.name=="video-params") { s.videoParameters=e.properties; ++s.informationRevision; }
        else if(e.name=="audio-params") { s.audioParameters=e.properties; ++s.informationRevision; }
        else if(e.name=="audio-device-list") { s.audioDevices=e.audioDevices; ++s.audioDevicesRevision; }
        else if(e.name=="audio-device") s.audioDevice=e.text;
        else if(e.name=="media-title") { s.mediaTitle=e.text; ++s.informationRevision; }
        else if(e.name=="seekable") s.seekable=e.flag.value_or(false);
        else if(e.name=="video-bitrate") { s.videoBitrate=e.number; ++s.informationRevision; }
        else if(e.name=="audio-bitrate") { s.audioBitrate=e.number; ++s.informationRevision; }
        else if(e.name=="file-size") { s.fileSize=e.number; ++s.informationRevision; }
        else if(e.name=="playlist") { s.playlist=e.playlist; ++s.playlistRevision; }
        else if(e.name=="track-list") { s.tracks=e.tracks; ++s.tracksRevision; }
        else if(e.name=="chapter-list") { s.chapters=e.chapters; ++s.chaptersRevision; }
        else if(e.name=="chapter") s.chapterIndex=e.integer;
        else if(e.name=="ab-loop-a") s.abLoopA=e.number && std::isfinite(*e.number) ? e.number : std::nullopt;
        else if(e.name=="ab-loop-b") s.abLoopB=e.number && std::isfinite(*e.number) ? e.number : std::nullopt;
        else if(e.name=="ab-loop-count") s.abLoopCount=e.text;
        else if(e.name=="time-pos") s.position=e.number && std::isfinite(*e.number) ? e.number : std::nullopt;
        else if(e.name=="duration") s.duration=e.number && std::isfinite(*e.number) && *e.number>=0 ? e.number : std::nullopt;
        else if(e.name=="pause" && e.flag) s.paused=*e.flag;
        else if(e.name=="paused-for-cache" && e.flag) s.buffering=*e.flag;
        else if(e.name=="volume" && e.number && std::isfinite(*e.number)) s.volume=*e.number;
        else if(e.name=="speed" && e.number && std::isfinite(*e.number)) s.speed=*e.number;
        else if(e.name=="mute" && e.flag) s.muted=*e.flag;
        else if(e.name=="sub-visibility" && e.flag) s.subtitlesVisible=*e.flag;
        else if(e.name=="volume-max" && e.number && std::isfinite(*e.number) && *e.number>=100 && *e.number<=1000) s.maxVolume=*e.number;
        else if(e.name=="audio-delay" && e.number && std::isfinite(*e.number)) s.audioDelay=*e.number;
        else if(e.name=="sub-delay" && e.number && std::isfinite(*e.number)) s.subtitleDelay=*e.number;
        else if(e.name=="loop-file" || e.name=="loop-playlist") {
            if(e.name=="loop-file") s.loopFile=e.text; else s.loopPlaylist=e.text;
            s.repeatMode=LoopEnabled(s.loopFile) ? RepeatMode::File : LoopEnabled(s.loopPlaylist) ? RepeatMode::Playlist : RepeatMode::Off;
        }
        else if(e.name=="keep-open") s.keepOpen=e.text;
        else if(e.name=="path") { s.path=e.text; ++s.informationRevision; }
        else if(e.name=="mpv-version") s.mpvVersion=e.text;
        else if(e.name=="current-tracks/video/codec-desc") { s.videoCodec=e.text; ++s.informationRevision; }
        else if(e.name=="hwdec-current") { s.hwdecCurrent=e.text; ++s.informationRevision; }
        else if(e.name=="current-vo") s.currentVo=e.text;
        else if(e.name=="vo-configured") s.voConfigured=e.flag.value_or(false);
        break;
    case MpvEvent::Kind::WatchedPropertiesReply:
    case MpvEvent::Kind::PlaylistReply:
        // Correlated GET replies are consumed by PlayerSession's edit cursor,
        // never treated as ordinary property observations by this reducer.
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

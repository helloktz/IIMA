// Transcribed from IINA's pinned mpv constant catalog; GPL v3.
// Copyright IINA contributors. Source/license: references/iina-source.txt,
// references/iina/LICENSE. Regenerate with scripts/generate-mpv-catalog.py.
// Names describe the reference API, not availability in the loaded libmpv.

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace iima::mpv_catalog {
struct MPVProperty final {
    static constexpr std::string_view audioSpeedCorrection = "audio-speed-correction";
    static constexpr std::string_view videoSpeedCorrection = "video-speed-correction";
    static constexpr std::string_view displaySyncActive = "display-sync-active";
    static constexpr std::string_view filename = "filename";
    static constexpr std::string_view filenameNoExt = "filename/no-ext";
    static constexpr std::string_view fileSize = "file-size";
    static constexpr std::string_view estimatedFrameCount = "estimated-frame-count";
    static constexpr std::string_view estimatedFrameNumber = "estimated-frame-number";
    static constexpr std::string_view pid = "pid";
    static constexpr std::string_view path = "path";
    static constexpr std::string_view streamOpenFilename = "stream-open-filename";
    static constexpr std::string_view mediaTitle = "media-title";
    static constexpr std::string_view fileFormat = "file-format";
    static constexpr std::string_view currentDemuxer = "current-demuxer";
    static constexpr std::string_view streamPath = "stream-path";
    static constexpr std::string_view streamPos = "stream-pos";
    static constexpr std::string_view streamEnd = "stream-end";
    static constexpr std::string_view duration = "duration";
    static constexpr std::string_view durationFull = "duration/full";
    static constexpr std::string_view avsync = "avsync";
    static constexpr std::string_view totalAvsyncChange = "total-avsync-change";
    static constexpr std::string_view decoderFrameDropCount = "decoder-frame-drop-count";
    static constexpr std::string_view frameDropCount = "frame-drop-count";
    static constexpr std::string_view mistimedFrameCount = "mistimed-frame-count";
    static constexpr std::string_view vsyncRatio = "vsync-ratio";
    static constexpr std::string_view voDelayedFrameCount = "vo-delayed-frame-count";
    static constexpr std::string_view percentPos = "percent-pos";
    static constexpr std::string_view timePos = "time-pos";
    static constexpr std::string_view timePosFull = "time-pos/full";
    static constexpr std::string_view timeStart = "time-start";
    static constexpr std::string_view timeRemaining = "time-remaining";
    static constexpr std::string_view timeRemainingFull = "time-remaining/full";
    static constexpr std::string_view audioPts = "audio-pts";
    static constexpr std::string_view audioPtsFull = "audio-pts/full";
    static constexpr std::string_view playtimeRemaining = "playtime-remaining";
    static constexpr std::string_view playtimeRemainingFull = "playtime-remaining/full";
    static constexpr std::string_view playbackTime = "playback-time";
    static constexpr std::string_view playbackTimeFull = "playback-time/full";
    static constexpr std::string_view remainingFileLoops = "remaining-file-loops";
    static constexpr std::string_view remainingAbLoops = "remaining-ab-loops";
    static constexpr std::string_view chapter = "chapter";
    static constexpr std::string_view edition = "edition";
    static constexpr std::string_view currentEdition = "current-edition";
    static constexpr std::string_view chapters = "chapters";
    static constexpr std::string_view editions = "editions";
    static constexpr std::string_view editionList = "edition-list";
    static constexpr std::string_view editionListCount = "edition-list/count";
    static std::string editionListNId(std::int64_t index) { return "edition-list/" + std::to_string(index) + "/id"; }
    static std::string editionListNDefault(std::int64_t index) { return "edition-list/" + std::to_string(index) + "/default"; }
    static std::string editionListNTitle(std::int64_t index) { return "edition-list/" + std::to_string(index) + "/title"; }
    static constexpr std::string_view metadata = "metadata";
    static constexpr std::string_view metadataListCount = "metadata/list/count";
    static std::string metadataListNKey(std::int64_t index) { return "metadata/list/" + std::to_string(index) + "/key"; }
    static std::string metadataListNValue(std::int64_t index) { return "metadata/list/" + std::to_string(index) + "/value"; }
    static constexpr std::string_view filteredMetadata = "filtered-metadata";
    static constexpr std::string_view chapterMetadata = "chapter-metadata";
    static constexpr std::string_view deinterlaceActive = "deinterlace-active";
    static constexpr std::string_view idleActive = "idle-active";
    static constexpr std::string_view coreIdle = "core-idle";
    static constexpr std::string_view cacheSpeed = "cache-speed";
    static constexpr std::string_view demuxerCacheDuration = "demuxer-cache-duration";
    static constexpr std::string_view demuxerCacheTime = "demuxer-cache-time";
    static constexpr std::string_view demuxerCacheIdle = "demuxer-cache-idle";
    static constexpr std::string_view demuxerCacheState = "demuxer-cache-state";
    static constexpr std::string_view demuxerViaNetwork = "demuxer-via-network";
    static constexpr std::string_view demuxerStartTime = "demuxer-start-time";
    static constexpr std::string_view pausedForCache = "paused-for-cache";
    static constexpr std::string_view cacheBufferingState = "cache-buffering-state";
    static constexpr std::string_view eofReached = "eof-reached";
    static constexpr std::string_view seeking = "seeking";
    static constexpr std::string_view mixerActive = "mixer-active";
    static constexpr std::string_view aoVolume = "ao-volume";
    static constexpr std::string_view aoMute = "ao-mute";
    static constexpr std::string_view audioParams = "audio-params";
    static constexpr std::string_view audioParamsFormat = "audio-params/format";
    static constexpr std::string_view audioParamsSamplerate = "audio-params/samplerate";
    static constexpr std::string_view audioParamsChannels = "audio-params/channels";
    static constexpr std::string_view audioParamsHrChannels = "audio-params/hr-channels";
    static constexpr std::string_view audioParamsChannelCount = "audio-params/channel-count";
    static constexpr std::string_view audioOutParams = "audio-out-params";
    static constexpr std::string_view colormatrix = "colormatrix";
    static constexpr std::string_view colormatrixInputRange = "colormatrix-input-range";
    static constexpr std::string_view colormatrixPrimaries = "colormatrix-primaries";
    static constexpr std::string_view hwdec = "hwdec";
    static constexpr std::string_view hwdecCurrent = "hwdec-current";
    static constexpr std::string_view hwdecInterop = "hwdec-interop";
    static constexpr std::string_view width = "width";
    static constexpr std::string_view height = "height";
    static constexpr std::string_view videoParams = "video-params";
    static constexpr std::string_view videoParamsPixelformat = "video-params/pixelformat";
    static constexpr std::string_view videoParamsHwPixelformat = "video-params/hw-pixelformat";
    static constexpr std::string_view videoParamsAverageBpp = "video-params/average-bpp";
    static constexpr std::string_view videoParamsW = "video-params/w";
    static constexpr std::string_view videoParamsH = "video-params/h";
    static constexpr std::string_view videoParamsDw = "video-params/dw";
    static constexpr std::string_view videoParamsDh = "video-params/dh";
    static constexpr std::string_view videoParamsCropX = "video-params/crop-x";
    static constexpr std::string_view videoParamsCropY = "video-params/crop-y";
    static constexpr std::string_view videoParamsCropW = "video-params/crop-w";
    static constexpr std::string_view videoParamsCropH = "video-params/crop-h";
    static constexpr std::string_view videoParamsAspect = "video-params/aspect";
    static constexpr std::string_view videoParamsAspectName = "video-params/aspect-name";
    static constexpr std::string_view videoParamsPar = "video-params/par";
    static constexpr std::string_view videoParamsSar = "video-params/sar";
    static constexpr std::string_view videoParamsSarName = "video-params/sar-name";
    static constexpr std::string_view videoParamsColormatrix = "video-params/colormatrix";
    static constexpr std::string_view videoParamsColorlevels = "video-params/colorlevels";
    static constexpr std::string_view videoParamsPrimaries = "video-params/primaries";
    static constexpr std::string_view videoParamsGamma = "video-params/gamma";
    static constexpr std::string_view videoParamsSigPeak = "video-params/sig-peak";
    static constexpr std::string_view videoParamsLight = "video-params/light";
    static constexpr std::string_view videoParamsChromaLocation = "video-params/chroma-location";
    static constexpr std::string_view videoParamsRotate = "video-params/rotate";
    static constexpr std::string_view videoParamsStereoIn = "video-params/stereo-in";
    static constexpr std::string_view videoParamsAlpha = "video-params/alpha";
    static constexpr std::string_view videoParamsMinLuma = "video-params/min-luma";
    static constexpr std::string_view videoParamsMaxLuma = "video-params/max-luma";
    static constexpr std::string_view videoParamsMaxCll = "video-params/max-cll";
    static constexpr std::string_view videoParamsMaxFall = "video-params/max-fall";
    static constexpr std::string_view videoParamsSceneMaxR = "video-params/scene-max-r";
    static constexpr std::string_view videoParamsSceneMaxG = "video-params/scene-max-g";
    static constexpr std::string_view videoParamsSceneMaxB = "video-params/scene-max-b";
    static constexpr std::string_view videoParamsMaxPqY = "video-params/max-pq-y";
    static constexpr std::string_view videoParamsAvgPqY = "video-params/avg-pq-y";
    static constexpr std::string_view videoParamsPrimRedX = "video-params/prim-red-x";
    static constexpr std::string_view videoParamsPrimRedY = "video-params/prim-red-y";
    static constexpr std::string_view videoParamsPrimGreenX = "video-params/prim-green-x";
    static constexpr std::string_view videoParamsPrimGreenY = "video-params/prim-green-y";
    static constexpr std::string_view videoParamsPrimBlueX = "video-params/prim-blue-x";
    static constexpr std::string_view videoParamsPrimBlueY = "video-params/prim-blue-y";
    static constexpr std::string_view videoParamsPrimWhiteX = "video-params/prim-white-x";
    static constexpr std::string_view videoParamsPrimWhiteY = "video-params/prim-white-y";
    static constexpr std::string_view dwidth = "dwidth";
    static constexpr std::string_view dheight = "dheight";
    static constexpr std::string_view videoDecParams = "video-dec-params";
    static constexpr std::string_view videoOutParams = "video-out-params";
    static constexpr std::string_view videoTargetParams = "video-target-params";
    static constexpr std::string_view videoFrameInfo = "video-frame-info";
    static constexpr std::string_view videoFrameInfoPictureType = "video-frame-info/picture-type";
    static constexpr std::string_view videoFrameInfoInterlaced = "video-frame-info/interlaced";
    static constexpr std::string_view videoFrameInfoTff = "video-frame-info/tff";
    static constexpr std::string_view videoFrameInfoRepeat = "video-frame-info/repeat";
    static constexpr std::string_view videoFrameInfoGopTimecode = "video-frame-info/gop-timecode";
    static constexpr std::string_view videoFrameInfoSmpteTimecode = "video-frame-info/smpte-timecode";
    static constexpr std::string_view videoFrameInfoEstimatedSmpteTimecode = "video-frame-info/estimated-smpte-timecode";
    static constexpr std::string_view containerFps = "container-fps";
    static constexpr std::string_view estimatedVfFps = "estimated-vf-fps";
    static constexpr std::string_view currentWindowScale = "current-window-scale";
    static constexpr std::string_view focused = "focused";
    static constexpr std::string_view ambientLight = "ambient-light";
    static constexpr std::string_view displayNames = "display-names";
    static constexpr std::string_view displayFps = "display-fps";
    static constexpr std::string_view estimatedDisplayFps = "estimated-display-fps";
    static constexpr std::string_view vsyncJitter = "vsync-jitter";
    static constexpr std::string_view displayWidth = "display-width";
    static constexpr std::string_view displayHeight = "display-height";
    static constexpr std::string_view displayHidpiScale = "display-hidpi-scale";
    static constexpr std::string_view osdWidth = "osd-width";
    static constexpr std::string_view osdHeight = "osd-height";
    static constexpr std::string_view osdPar = "osd-par";
    static constexpr std::string_view osdDimensions = "osd-dimensions";
    static constexpr std::string_view osdDimensionsW = "osd-dimensions/w";
    static constexpr std::string_view osdDimensionsH = "osd-dimensions/h";
    static constexpr std::string_view osdDimensionsPar = "osd-dimensions/par";
    static constexpr std::string_view osdDimensionsAspect = "osd-dimensions/aspect";
    static constexpr std::string_view osdDimensionsMt = "osd-dimensions/mt";
    static constexpr std::string_view osdDimensionsMb = "osd-dimensions/mb";
    static constexpr std::string_view osdDimensionsMl = "osd-dimensions/ml";
    static constexpr std::string_view osdDimensionsMr = "osd-dimensions/mr";
    static constexpr std::string_view termSize = "term-size";
    static constexpr std::string_view termSizeW = "term-size/w";
    static constexpr std::string_view termSizeH = "term-size/h";
    static constexpr std::string_view windowId = "window-id";
    static constexpr std::string_view displaySwapchain = "display-swapchain";
    static constexpr std::string_view mousePos = "mouse-pos";
    static constexpr std::string_view mousePosX = "mouse-pos/x";
    static constexpr std::string_view mousePosY = "mouse-pos/y";
    static constexpr std::string_view mousePosHover = "mouse-pos/hover";
    static constexpr std::string_view touchPos = "touch-pos";
    static constexpr std::string_view touchPosCount = "touch-pos/count";
    static std::string touchPosNX(std::int64_t index) { return "touch-pos/" + std::to_string(index) + "/x"; }
    static std::string touchPosNY(std::int64_t index) { return "touch-pos/" + std::to_string(index) + "/y"; }
    static std::string touchPosNId(std::int64_t index) { return "touch-pos/" + std::to_string(index) + "/id"; }
    static constexpr std::string_view tabletPos = "tablet-pos";
    static constexpr std::string_view tabletPosX = "tablet-pos/x";
    static constexpr std::string_view tabletPosY = "tablet-pos/y";
    static constexpr std::string_view tabletPosToolInProximity = "tablet-pos/tool-in-proximity";
    static constexpr std::string_view tabletPosToolTip = "tablet-pos/tool-tip";
    static constexpr std::string_view tabletPosToolStylusBtn1 = "tablet-pos/tool-stylus-btn1";
    static constexpr std::string_view tabletPosToolStylusBtn2 = "tablet-pos/tool-stylus-btn2";
    static constexpr std::string_view tabletPosToolStylusBtn3 = "tablet-pos/tool-stylus-btn3";
    static constexpr std::string_view tabletPosPadFocus = "tablet-pos/pad-focus";
    static std::string tabletPosPadBtnsN(std::int64_t index) { return "tablet-pos/pad-btns/" + std::to_string(index) + ""; }
    static constexpr std::string_view subAssExtradata = "sub-ass-extradata";
    static constexpr std::string_view subText = "sub-text";
    static constexpr std::string_view subTextAss = "sub-text/ass";
    static constexpr std::string_view subTextAssFull = "sub-text/ass-full";
    static constexpr std::string_view subTextAss1 = "subTextAss1";
    static constexpr std::string_view secondarySubText = "secondary-sub-text";
    static constexpr std::string_view subStart = "sub-start";
    static constexpr std::string_view subStartFull = "sub-start/full";
    static constexpr std::string_view secondarySubStart = "secondary-sub-start";
    static constexpr std::string_view subEnd = "sub-end";
    static constexpr std::string_view subEndFull = "sub-end/full";
    static constexpr std::string_view secondarySubEnd = "secondary-sub-end";
    static constexpr std::string_view playlistPos = "playlist-pos";
    static constexpr std::string_view playlistPos1 = "playlist-pos-1";
    static constexpr std::string_view playlistCurrentPos = "playlist-current-pos";
    static constexpr std::string_view playlistPlayingPos = "playlist-playing-pos";
    static constexpr std::string_view playlistCount = "playlist-count";
    static constexpr std::string_view playlistPath = "playlist-path";
    static constexpr std::string_view playlist = "playlist";
    static constexpr std::string_view playlistCount1 = "playlistCount1";
    static std::string playlistNFilename(std::int64_t index) { return "playlist/" + std::to_string(index) + "/filename"; }
    static std::string playlistNPlaying(std::int64_t index) { return "playlist/" + std::to_string(index) + "/playing"; }
    static std::string playlistNCurrent(std::int64_t index) { return "playlist/" + std::to_string(index) + "/current"; }
    static std::string playlistNTitle(std::int64_t index) { return "playlist/" + std::to_string(index) + "/title"; }
    static std::string playlistNId(std::int64_t index) { return "playlist/" + std::to_string(index) + "/id"; }
    static std::string playlistNPlaylistPath(std::int64_t index) { return "playlist/" + std::to_string(index) + "/playlist-path"; }
    static constexpr std::string_view trackList = "track-list";
    static constexpr std::string_view trackListCount = "track-list/count";
    static constexpr std::string_view trackListVideo = "track-list/video";
    static constexpr std::string_view trackListAudio = "track-list/audio";
    static constexpr std::string_view trackListSub = "track-list/sub";
    static std::string trackListNId(std::int64_t index) { return "track-list/" + std::to_string(index) + "/id"; }
    static std::string trackListNType(std::int64_t index) { return "track-list/" + std::to_string(index) + "/type"; }
    static std::string trackListNSrcId(std::int64_t index) { return "track-list/" + std::to_string(index) + "/src-id"; }
    static std::string trackListNTitle(std::int64_t index) { return "track-list/" + std::to_string(index) + "/title"; }
    static std::string trackListNLang(std::int64_t index) { return "track-list/" + std::to_string(index) + "/lang"; }
    static std::string trackListNImage(std::int64_t index) { return "track-list/" + std::to_string(index) + "/image"; }
    static std::string trackListNAlbumart(std::int64_t index) { return "track-list/" + std::to_string(index) + "/albumart"; }
    static std::string trackListNDefault(std::int64_t index) { return "track-list/" + std::to_string(index) + "/default"; }
    static std::string trackListNForced(std::int64_t index) { return "track-list/" + std::to_string(index) + "/forced"; }
    static std::string trackListNDependent(std::int64_t index) { return "track-list/" + std::to_string(index) + "/dependent"; }
    static std::string trackListNVisualImpaired(std::int64_t index) { return "track-list/" + std::to_string(index) + "/visual-impaired"; }
    static std::string trackListNHearingImpaired(std::int64_t index) { return "track-list/" + std::to_string(index) + "/hearing-impaired"; }
    static std::string trackListNHlsBitrate(std::int64_t index) { return "track-list/" + std::to_string(index) + "/hls-bitrate"; }
    static std::string trackListNProgramId(std::int64_t index) { return "track-list/" + std::to_string(index) + "/program-id"; }
    static std::string trackListNCodec(std::int64_t index) { return "track-list/" + std::to_string(index) + "/codec"; }
    static std::string trackListNCodecDesc(std::int64_t index) { return "track-list/" + std::to_string(index) + "/codec-desc"; }
    static std::string trackListNCodecProfile(std::int64_t index) { return "track-list/" + std::to_string(index) + "/codec-profile"; }
    static std::string trackListNExternal(std::int64_t index) { return "track-list/" + std::to_string(index) + "/external"; }
    static std::string trackListNExternalFilename(std::int64_t index) { return "track-list/" + std::to_string(index) + "/external-filename"; }
    static std::string trackListNSelected(std::int64_t index) { return "track-list/" + std::to_string(index) + "/selected"; }
    static std::string trackListNMainSelection(std::int64_t index) { return "track-list/" + std::to_string(index) + "/main-selection"; }
    static std::string trackListNFfIndex(std::int64_t index) { return "track-list/" + std::to_string(index) + "/ff-index"; }
    static std::string trackListNDecoder(std::int64_t index) { return "track-list/" + std::to_string(index) + "/decoder"; }
    static std::string trackListNDecoderDesc(std::int64_t index) { return "track-list/" + std::to_string(index) + "/decoder-desc"; }
    static std::string trackListNDemuxW(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-w"; }
    static std::string trackListNDemuxH(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-h"; }
    static std::string trackListNDemuxCropX(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-crop-x"; }
    static std::string trackListNDemuxCropY(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-crop-y"; }
    static std::string trackListNDemuxCropW(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-crop-w"; }
    static std::string trackListNDemuxCropH(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-crop-h"; }
    static std::string trackListNDemuxChannelCount(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-channel-count"; }
    static std::string trackListNDemuxChannels(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-channels"; }
    static std::string trackListNDemuxSamplerate(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-samplerate"; }
    static std::string trackListNDemuxFps(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-fps"; }
    static std::string trackListNDemuxBitrate(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-bitrate"; }
    static std::string trackListNDemuxRotation(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-rotation"; }
    static std::string trackListNDemuxPar(std::int64_t index) { return "track-list/" + std::to_string(index) + "/demux-par"; }
    static std::string trackListNFormatName(std::int64_t index) { return "track-list/" + std::to_string(index) + "/format-name"; }
    static std::string trackListNAudioChannels(std::int64_t index) { return "track-list/" + std::to_string(index) + "/audio-channels"; }
    static std::string trackListNReplaygainTrackPeak(std::int64_t index) { return "track-list/" + std::to_string(index) + "/replaygain-track-peak"; }
    static std::string trackListNReplaygainTrackGain(std::int64_t index) { return "track-list/" + std::to_string(index) + "/replaygain-track-gain"; }
    static std::string trackListNReplaygainAlbumPeak(std::int64_t index) { return "track-list/" + std::to_string(index) + "/replaygain-album-peak"; }
    static std::string trackListNReplaygainAlbumGain(std::int64_t index) { return "track-list/" + std::to_string(index) + "/replaygain-album-gain"; }
    static std::string trackListNDolbyVisionProfile(std::int64_t index) { return "track-list/" + std::to_string(index) + "/dolby-vision-profile"; }
    static std::string trackListNDolbyVisionLevel(std::int64_t index) { return "track-list/" + std::to_string(index) + "/dolby-vision-level"; }
    static std::string trackListNMetadata(std::int64_t index) { return "track-list/" + std::to_string(index) + "/metadata"; }
    static constexpr std::string_view currentTracksAudioId = "current-tracks/audio/id";
    static constexpr std::string_view currentTracksVideoId = "current-tracks/video/id";
    static constexpr std::string_view currentTracksSubId = "current-tracks/sub/id";
    static constexpr std::string_view currentTracksSub2Id = "current-tracks/sub2/id";
    static constexpr std::string_view currentTracksAudioType = "current-tracks/audio/type";
    static constexpr std::string_view currentTracksVideoType = "current-tracks/video/type";
    static constexpr std::string_view currentTracksSubType = "current-tracks/sub/type";
    static constexpr std::string_view currentTracksSub2Type = "current-tracks/sub2/type";
    static constexpr std::string_view currentTracksAudioSrcId = "current-tracks/audio/src-id";
    static constexpr std::string_view currentTracksVideoSrcId = "current-tracks/video/src-id";
    static constexpr std::string_view currentTracksSubSrcId = "current-tracks/sub/src-id";
    static constexpr std::string_view currentTracksSub2SrcId = "current-tracks/sub2/src-id";
    static constexpr std::string_view currentTracksAudioTitle = "current-tracks/audio/title";
    static constexpr std::string_view currentTracksVideoTitle = "current-tracks/video/title";
    static constexpr std::string_view currentTracksSubTitle = "current-tracks/sub/title";
    static constexpr std::string_view currentTracksSub2Title = "current-tracks/sub2/title";
    static constexpr std::string_view currentTracksAudioLang = "current-tracks/audio/lang";
    static constexpr std::string_view currentTracksVideoLang = "current-tracks/video/lang";
    static constexpr std::string_view currentTracksSubLang = "current-tracks/sub/lang";
    static constexpr std::string_view currentTracksSub2Lang = "current-tracks/sub2/lang";
    static constexpr std::string_view currentTracksAudioImage = "current-tracks/audio/image";
    static constexpr std::string_view currentTracksVideoImage = "current-tracks/video/image";
    static constexpr std::string_view currentTracksSubImage = "current-tracks/sub/image";
    static constexpr std::string_view currentTracksSub2Image = "current-tracks/sub2/image";
    static constexpr std::string_view currentTracksAudioAlbumart = "current-tracks/audio/albumart";
    static constexpr std::string_view currentTracksVideoAlbumart = "current-tracks/video/albumart";
    static constexpr std::string_view currentTracksSubAlbumart = "current-tracks/sub/albumart";
    static constexpr std::string_view currentTracksSub2Albumart = "current-tracks/sub2/albumart";
    static constexpr std::string_view currentTracksAudioDefault = "current-tracks/audio/default";
    static constexpr std::string_view currentTracksVideoDefault = "current-tracks/video/default";
    static constexpr std::string_view currentTracksSubDefault = "current-tracks/sub/default";
    static constexpr std::string_view currentTracksSub2Default = "current-tracks/sub2/default";
    static constexpr std::string_view currentTracksAudioForced = "current-tracks/audio/forced";
    static constexpr std::string_view currentTracksVideoForced = "current-tracks/video/forced";
    static constexpr std::string_view currentTracksSubForced = "current-tracks/sub/forced";
    static constexpr std::string_view currentTracksSub2Forced = "current-tracks/sub2/forced";
    static constexpr std::string_view currentTracksAudioDependent = "current-tracks/audio/dependent";
    static constexpr std::string_view currentTracksVideoDependent = "current-tracks/video/dependent";
    static constexpr std::string_view currentTracksSubDependent = "current-tracks/sub/dependent";
    static constexpr std::string_view currentTracksSub2Dependent = "current-tracks/sub2/dependent";
    static constexpr std::string_view currentTracksAudioVisualImpaired = "current-tracks/audio/visual-impaired";
    static constexpr std::string_view currentTracksVideoVisualImpaired = "current-tracks/video/visual-impaired";
    static constexpr std::string_view currentTracksSubVisualImpaired = "current-tracks/sub/visual-impaired";
    static constexpr std::string_view currentTracksSub2VisualImpaired = "current-tracks/sub2/visual-impaired";
    static constexpr std::string_view currentTracksAudioHearingImpaired = "current-tracks/audio/hearing-impaired";
    static constexpr std::string_view currentTracksVideoHearingImpaired = "current-tracks/video/hearing-impaired";
    static constexpr std::string_view currentTracksSubHearingImpaired = "current-tracks/sub/hearing-impaired";
    static constexpr std::string_view currentTracksSub2HearingImpaired = "current-tracks/sub2/hearing-impaired";
    static constexpr std::string_view currentTracksAudioHlsBitrate = "current-tracks/audio/hls-bitrate";
    static constexpr std::string_view currentTracksVideoHlsBitrate = "current-tracks/video/hls-bitrate";
    static constexpr std::string_view currentTracksSubHlsBitrate = "current-tracks/sub/hls-bitrate";
    static constexpr std::string_view currentTracksSub2HlsBitrate = "current-tracks/sub2/hls-bitrate";
    static constexpr std::string_view currentTracksAudioProgramId = "current-tracks/audio/program-id";
    static constexpr std::string_view currentTracksVideoProgramId = "current-tracks/video/program-id";
    static constexpr std::string_view currentTracksSubProgramId = "current-tracks/sub/program-id";
    static constexpr std::string_view currentTracksSub2ProgramId = "current-tracks/sub2/program-id";
    static constexpr std::string_view currentTracksAudioCodec = "current-tracks/audio/codec";
    static constexpr std::string_view currentTracksVideoCodec = "current-tracks/video/codec";
    static constexpr std::string_view currentTracksSubCodec = "current-tracks/sub/codec";
    static constexpr std::string_view currentTracksSub2Codec = "current-tracks/sub2/codec";
    static constexpr std::string_view currentTracksAudioCodecDesc = "current-tracks/audio/codec-desc";
    static constexpr std::string_view currentTracksVideoCodecDesc = "current-tracks/video/codec-desc";
    static constexpr std::string_view currentTracksSubCodecDesc = "current-tracks/sub/codec-desc";
    static constexpr std::string_view currentTracksSub2CodecDesc = "current-tracks/sub2/codec-desc";
    static constexpr std::string_view currentTracksAudioCodecProfile = "current-tracks/audio/codec-profile";
    static constexpr std::string_view currentTracksVideoCodecProfile = "current-tracks/video/codec-profile";
    static constexpr std::string_view currentTracksSubCodecProfile = "current-tracks/sub/codec-profile";
    static constexpr std::string_view currentTracksSub2CodecProfile = "current-tracks/sub2/codec-profile";
    static constexpr std::string_view currentTracksAudioExternal = "current-tracks/audio/external";
    static constexpr std::string_view currentTracksVideoExternal = "current-tracks/video/external";
    static constexpr std::string_view currentTracksSubExternal = "current-tracks/sub/external";
    static constexpr std::string_view currentTracksSub2External = "current-tracks/sub2/external";
    static constexpr std::string_view currentTracksAudioExternalFilename = "current-tracks/audio/external-filename";
    static constexpr std::string_view currentTracksVideoExternalFilename = "current-tracks/video/external-filename";
    static constexpr std::string_view currentTracksSubExternalFilename = "current-tracks/sub/external-filename";
    static constexpr std::string_view currentTracksSub2ExternalFilename = "current-tracks/sub2/external-filename";
    static constexpr std::string_view currentTracksAudioSelected = "current-tracks/audio/selected";
    static constexpr std::string_view currentTracksVideoSelected = "current-tracks/video/selected";
    static constexpr std::string_view currentTracksSubSelected = "current-tracks/sub/selected";
    static constexpr std::string_view currentTracksSub2Selected = "current-tracks/sub2/selected";
    static constexpr std::string_view currentTracksAudioMainSelection = "current-tracks/audio/main-selection";
    static constexpr std::string_view currentTracksVideoMainSelection = "current-tracks/video/main-selection";
    static constexpr std::string_view currentTracksSubMainSelection = "current-tracks/sub/main-selection";
    static constexpr std::string_view currentTracksSub2MainSelection = "current-tracks/sub2/main-selection";
    static constexpr std::string_view currentTracksAudioFfIndex = "current-tracks/audio/ff-index";
    static constexpr std::string_view currentTracksVideoFfIndex = "current-tracks/video/ff-index";
    static constexpr std::string_view currentTracksSubFfIndex = "current-tracks/sub/ff-index";
    static constexpr std::string_view currentTracksSub2FfIndex = "current-tracks/sub2/ff-index";
    static constexpr std::string_view currentTracksAudioDecoder = "current-tracks/audio/decoder";
    static constexpr std::string_view currentTracksVideoDecoder = "current-tracks/video/decoder";
    static constexpr std::string_view currentTracksSubDecoder = "current-tracks/sub/decoder";
    static constexpr std::string_view currentTracksSub2Decoder = "current-tracks/sub2/decoder";
    static constexpr std::string_view currentTracksAudioDecoderDesc = "current-tracks/audio/decoder-desc";
    static constexpr std::string_view currentTracksVideoDecoderDesc = "current-tracks/video/decoder-desc";
    static constexpr std::string_view currentTracksSubDecoderDesc = "current-tracks/sub/decoder-desc";
    static constexpr std::string_view currentTracksSub2DecoderDesc = "current-tracks/sub2/decoder-desc";
    static constexpr std::string_view currentTracksAudioDemuxW = "current-tracks/audio/demux-w";
    static constexpr std::string_view currentTracksVideoDemuxW = "current-tracks/video/demux-w";
    static constexpr std::string_view currentTracksSubDemuxW = "current-tracks/sub/demux-w";
    static constexpr std::string_view currentTracksSub2DemuxW = "current-tracks/sub2/demux-w";
    static constexpr std::string_view currentTracksAudioDemuxH = "current-tracks/audio/demux-h";
    static constexpr std::string_view currentTracksVideoDemuxH = "current-tracks/video/demux-h";
    static constexpr std::string_view currentTracksSubDemuxH = "current-tracks/sub/demux-h";
    static constexpr std::string_view currentTracksSub2DemuxH = "current-tracks/sub2/demux-h";
    static constexpr std::string_view currentTracksAudioDemuxCropX = "current-tracks/audio/demux-crop-x";
    static constexpr std::string_view currentTracksVideoDemuxCropX = "current-tracks/video/demux-crop-x";
    static constexpr std::string_view currentTracksSubDemuxCropX = "current-tracks/sub/demux-crop-x";
    static constexpr std::string_view currentTracksSub2DemuxCropX = "current-tracks/sub2/demux-crop-x";
    static constexpr std::string_view currentTracksAudioDemuxCropY = "current-tracks/audio/demux-crop-y";
    static constexpr std::string_view currentTracksVideoDemuxCropY = "current-tracks/video/demux-crop-y";
    static constexpr std::string_view currentTracksSubDemuxCropY = "current-tracks/sub/demux-crop-y";
    static constexpr std::string_view currentTracksSub2DemuxCropY = "current-tracks/sub2/demux-crop-y";
    static constexpr std::string_view currentTracksAudioDemuxCropW = "current-tracks/audio/demux-crop-w";
    static constexpr std::string_view currentTracksVideoDemuxCropW = "current-tracks/video/demux-crop-w";
    static constexpr std::string_view currentTracksSubDemuxCropW = "current-tracks/sub/demux-crop-w";
    static constexpr std::string_view currentTracksSub2DemuxCropW = "current-tracks/sub2/demux-crop-w";
    static constexpr std::string_view currentTracksAudioDemuxCropH = "current-tracks/audio/demux-crop-h";
    static constexpr std::string_view currentTracksVideoDemuxCropH = "current-tracks/video/demux-crop-h";
    static constexpr std::string_view currentTracksSubDemuxCropH = "current-tracks/sub/demux-crop-h";
    static constexpr std::string_view currentTracksSub2DemuxCropH = "current-tracks/sub2/demux-crop-h";
    static constexpr std::string_view currentTracksAudioDemuxChannelCount = "current-tracks/audio/demux-channel-count";
    static constexpr std::string_view currentTracksVideoDemuxChannelCount = "current-tracks/video/demux-channel-count";
    static constexpr std::string_view currentTracksSubDemuxChannelCount = "current-tracks/sub/demux-channel-count";
    static constexpr std::string_view currentTracksSub2DemuxChannelCount = "current-tracks/sub2/demux-channel-count";
    static constexpr std::string_view currentTracksAudioDemuxChannels = "current-tracks/audio/demux-channels";
    static constexpr std::string_view currentTracksVideoDemuxChannels = "current-tracks/video/demux-channels";
    static constexpr std::string_view currentTracksSubDemuxChannels = "current-tracks/sub/demux-channels";
    static constexpr std::string_view currentTracksSub2DemuxChannels = "current-tracks/sub2/demux-channels";
    static constexpr std::string_view currentTracksAudioDemuxSamplerate = "current-tracks/audio/demux-samplerate";
    static constexpr std::string_view currentTracksVideoDemuxSamplerate = "current-tracks/video/demux-samplerate";
    static constexpr std::string_view currentTracksSubDemuxSamplerate = "current-tracks/sub/demux-samplerate";
    static constexpr std::string_view currentTracksSub2DemuxSamplerate = "current-tracks/sub2/demux-samplerate";
    static constexpr std::string_view currentTracksAudioDemuxFps = "current-tracks/audio/demux-fps";
    static constexpr std::string_view currentTracksVideoDemuxFps = "current-tracks/video/demux-fps";
    static constexpr std::string_view currentTracksSubDemuxFps = "current-tracks/sub/demux-fps";
    static constexpr std::string_view currentTracksSub2DemuxFps = "current-tracks/sub2/demux-fps";
    static constexpr std::string_view currentTracksAudioDemuxBitrate = "current-tracks/audio/demux-bitrate";
    static constexpr std::string_view currentTracksVideoDemuxBitrate = "current-tracks/video/demux-bitrate";
    static constexpr std::string_view currentTracksSubDemuxBitrate = "current-tracks/sub/demux-bitrate";
    static constexpr std::string_view currentTracksSub2DemuxBitrate = "current-tracks/sub2/demux-bitrate";
    static constexpr std::string_view currentTracksAudioDemuxRotation = "current-tracks/audio/demux-rotation";
    static constexpr std::string_view currentTracksVideoDemuxRotation = "current-tracks/video/demux-rotation";
    static constexpr std::string_view currentTracksSubDemuxRotation = "current-tracks/sub/demux-rotation";
    static constexpr std::string_view currentTracksSub2DemuxRotation = "current-tracks/sub2/demux-rotation";
    static constexpr std::string_view currentTracksAudioDemuxPar = "current-tracks/audio/demux-par";
    static constexpr std::string_view currentTracksVideoDemuxPar = "current-tracks/video/demux-par";
    static constexpr std::string_view currentTracksSubDemuxPar = "current-tracks/sub/demux-par";
    static constexpr std::string_view currentTracksSub2DemuxPar = "current-tracks/sub2/demux-par";
    static constexpr std::string_view currentTracksAudioFormatName = "current-tracks/audio/format-name";
    static constexpr std::string_view currentTracksVideoFormatName = "current-tracks/video/format-name";
    static constexpr std::string_view currentTracksSubFormatName = "current-tracks/sub/format-name";
    static constexpr std::string_view currentTracksSub2FormatName = "current-tracks/sub2/format-name";
    static constexpr std::string_view currentTracksAudioAudioChannels = "current-tracks/audio/audio-channels";
    static constexpr std::string_view currentTracksVideoAudioChannels = "current-tracks/video/audio-channels";
    static constexpr std::string_view currentTracksSubAudioChannels = "current-tracks/sub/audio-channels";
    static constexpr std::string_view currentTracksSub2AudioChannels = "current-tracks/sub2/audio-channels";
    static constexpr std::string_view currentTracksAudioReplaygainTrackPeak = "current-tracks/audio/replaygain-track-peak";
    static constexpr std::string_view currentTracksVideoReplaygainTrackPeak = "current-tracks/video/replaygain-track-peak";
    static constexpr std::string_view currentTracksSubReplaygainTrackPeak = "current-tracks/sub/replaygain-track-peak";
    static constexpr std::string_view currentTracksSub2ReplaygainTrackPeak = "current-tracks/sub2/replaygain-track-peak";
    static constexpr std::string_view currentTracksAudioReplaygainTrackGain = "current-tracks/audio/replaygain-track-gain";
    static constexpr std::string_view currentTracksVideoReplaygainTrackGain = "current-tracks/video/replaygain-track-gain";
    static constexpr std::string_view currentTracksSubReplaygainTrackGain = "current-tracks/sub/replaygain-track-gain";
    static constexpr std::string_view currentTracksSub2ReplaygainTrackGain = "current-tracks/sub2/replaygain-track-gain";
    static constexpr std::string_view currentTracksAudioReplaygainAlbumPeak = "current-tracks/audio/replaygain-album-peak";
    static constexpr std::string_view currentTracksVideoReplaygainAlbumPeak = "current-tracks/video/replaygain-album-peak";
    static constexpr std::string_view currentTracksSubReplaygainAlbumPeak = "current-tracks/sub/replaygain-album-peak";
    static constexpr std::string_view currentTracksSub2ReplaygainAlbumPeak = "current-tracks/sub2/replaygain-album-peak";
    static constexpr std::string_view currentTracksAudioReplaygainAlbumGain = "current-tracks/audio/replaygain-album-gain";
    static constexpr std::string_view currentTracksVideoReplaygainAlbumGain = "current-tracks/video/replaygain-album-gain";
    static constexpr std::string_view currentTracksSubReplaygainAlbumGain = "current-tracks/sub/replaygain-album-gain";
    static constexpr std::string_view currentTracksSub2ReplaygainAlbumGain = "current-tracks/sub2/replaygain-album-gain";
    static constexpr std::string_view currentTracksAudioDolbyVisionProfile = "current-tracks/audio/dolby-vision-profile";
    static constexpr std::string_view currentTracksVideoDolbyVisionProfile = "current-tracks/video/dolby-vision-profile";
    static constexpr std::string_view currentTracksSubDolbyVisionProfile = "current-tracks/sub/dolby-vision-profile";
    static constexpr std::string_view currentTracksSub2DolbyVisionProfile = "current-tracks/sub2/dolby-vision-profile";
    static constexpr std::string_view currentTracksAudioDolbyVisionLevel = "current-tracks/audio/dolby-vision-level";
    static constexpr std::string_view currentTracksVideoDolbyVisionLevel = "current-tracks/video/dolby-vision-level";
    static constexpr std::string_view currentTracksSubDolbyVisionLevel = "current-tracks/sub/dolby-vision-level";
    static constexpr std::string_view currentTracksSub2DolbyVisionLevel = "current-tracks/sub2/dolby-vision-level";
    static constexpr std::string_view currentTracksAudioMetadata = "current-tracks/audio/metadata";
    static constexpr std::string_view currentTracksVideoMetadata = "current-tracks/video/metadata";
    static constexpr std::string_view currentTracksSubMetadata = "current-tracks/sub/metadata";
    static constexpr std::string_view currentTracksSub2Metadata = "current-tracks/sub2/metadata";
    static constexpr std::string_view chapterList = "chapter-list";
    static constexpr std::string_view chapterListCount = "chapter-list/count";
    static std::string chapterListNTitle(std::int64_t index) { return "chapter-list/" + std::to_string(index) + "/title"; }
    static std::string chapterListNTime(std::int64_t index) { return "chapter-list/" + std::to_string(index) + "/time"; }
    static constexpr std::string_view af = "af";
    static constexpr std::string_view vf = "vf";
    static constexpr std::string_view seekable = "seekable";
    static constexpr std::string_view partiallySeekable = "partially-seekable";
    static constexpr std::string_view playbackAbort = "playback-abort";
    static constexpr std::string_view cursorAutohide = "cursor-autohide";
    static constexpr std::string_view termClipCc = "term-clip-cc";
    static constexpr std::string_view osdSymCc = "osd-sym-cc";
    static constexpr std::string_view osdAssCc = "osd-ass-cc";
    static constexpr std::string_view voConfigured = "vo-configured";
    static constexpr std::string_view voPasses = "vo-passes";
    static constexpr std::string_view voPassesTYPECount = "vo-passes/TYPE/count";
    static std::string voPassesTYPENDesc(std::int64_t index) { return "vo-passes/TYPE/" + std::to_string(index) + "/desc"; }
    static std::string voPassesTYPENLast(std::int64_t index) { return "vo-passes/TYPE/" + std::to_string(index) + "/last"; }
    static std::string voPassesTYPENAvg(std::int64_t index) { return "vo-passes/TYPE/" + std::to_string(index) + "/avg"; }
    static std::string voPassesTYPENPeak(std::int64_t index) { return "vo-passes/TYPE/" + std::to_string(index) + "/peak"; }
    static std::string voPassesTYPENCount(std::int64_t index) { return "vo-passes/TYPE/" + std::to_string(index) + "/count"; }
    static std::string voPassesTYPENSamplesM(std::int64_t index) { return "vo-passes/TYPE/" + std::to_string(index) + "/samples/M"; }
    static constexpr std::string_view perfInfo = "perf-info";
    static constexpr std::string_view videoBitrate = "video-bitrate";
    static constexpr std::string_view audioBitrate = "audio-bitrate";
    static constexpr std::string_view subBitrate = "sub-bitrate";
    static constexpr std::string_view audioDeviceList = "audio-device-list";
    static constexpr std::string_view audioDevice = "audio-device";
    static constexpr std::string_view currentVo = "current-vo";
    static constexpr std::string_view currentGpuContext = "current-gpu-context";
    static constexpr std::string_view currentAo = "current-ao";
    static constexpr std::string_view userData = "user-data";
    static constexpr std::string_view userDataOscMargins = "user-data/osc/margins";
    static constexpr std::string_view userDataMpvYtdl = "user-data/mpv/ytdl";
    static constexpr std::string_view userDataMpvYtdlPath = "user-data/mpv/ytdl/path";
    static constexpr std::string_view userDataMpvYtdlJsonSubprocessResult = "user-data/mpv/ytdl/json-subprocess-result";
    static constexpr std::string_view userDataMpvConsoleOpen = "user-data/mpv/console/open";
    static constexpr std::string_view menuData = "menu-data";
    static constexpr std::string_view workingDirectory = "working-directory";
    static constexpr std::string_view currentWatchLaterDir = "current-watch-later-dir";
    static constexpr std::string_view protocolList = "protocol-list";
    static constexpr std::string_view decoderList = "decoder-list";
    static constexpr std::string_view encoderList = "encoder-list";
    static constexpr std::string_view demuxerLavfList = "demuxer-lavf-list";
    static constexpr std::string_view inputKeyList = "input-key-list";
    static constexpr std::string_view mpvVersion = "mpv-version";
    static constexpr std::string_view mpvConfiguration = "mpv-configuration";
    static constexpr std::string_view ffmpegVersion = "ffmpeg-version";
    static constexpr std::string_view libassVersion = "libass-version";
    static constexpr std::string_view platform = "platform";
    static std::string options(std::string_view name) { return "options/" + std::string(name) + ""; }
    static std::string fileLocalOptions(std::string_view name) { return "file-local-options/" + std::string(name) + ""; }
    static std::string optionInfo(std::string_view name) { return "option-info/" + std::string(name) + ""; }
    static std::string optionInfoName(std::string_view name) { return "option-info/" + std::string(name) + "/name"; }
    static std::string optionInfoType(std::string_view name) { return "option-info/" + std::string(name) + "/type"; }
    static std::string optionInfoSetFromCommandline(std::string_view name) { return "option-info/" + std::string(name) + "/set-from-commandline"; }
    static std::string optionInfoSetLocally(std::string_view name) { return "option-info/" + std::string(name) + "/set-locally"; }
    static std::string optionInfoExpectsFile(std::string_view name) { return "option-info/" + std::string(name) + "/expects-file"; }
    static std::string optionInfoDefaultValue(std::string_view name) { return "option-info/" + std::string(name) + "/default-value"; }
    static std::string optionInfoMin(std::string_view name) { return "option-info/" + std::string(name) + "/min"; }
    static std::string optionInfoMax(std::string_view name) { return "option-info/" + std::string(name) + "/max"; }
    static std::string optionInfoChoices(std::string_view name) { return "option-info/" + std::string(name) + "/choices"; }
    static constexpr std::string_view propertyList = "property-list";
    static constexpr std::string_view profileList = "profile-list";
    static constexpr std::string_view commandList = "command-list";
    static constexpr std::string_view inputBindings = "input-bindings";
    static constexpr std::string_view clipboard = "clipboard";
    static constexpr std::string_view clipboardText = "clipboard/text";
    static constexpr std::string_view clipboardTextPrimary = "clipboard/text-primary";
    static constexpr std::string_view currentClipboardBackend = "current-clipboard-backend";
    static constexpr std::string_view clock = "clock";
};
}

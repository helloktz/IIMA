// Adapted from IINA Preference.swift / MPVController.mpvInit and setUserOption.
// Copyright © 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "PreferenceSchema.h"
#include "ControlBarToolbar.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace iima::settings {
namespace {
constexpr EnumChoice subtitleAutoLoad[]{{0,"disabled"},{1,"filename-contains"},{2,"iina"}};
constexpr EnumChoice hardware[]{{0,"no"},{1,"auto"},{2,"auto-copy"}};
constexpr EnumChoice replay[]{{0,"no"},{1,"track"},{2,"album"}};
constexpr EnumChoice gapless[]{{0,"no"},{1,"weak"},{2,"yes"}};
constexpr EnumChoice subOverride[]{{0,"yes"},{1,"force"},{2,"strip"},{3,"scale"},{4,"no"}};
constexpr EnumChoice border[]{{1,"outline-and-shadow"},{3,"opaque-box"},{4,"background-box"}};
constexpr EnumChoice alignX[]{{0,"left"},{1,"center"},{2,"right"}};
constexpr EnumChoice alignY[]{{0,"top"},{1,"center"},{2,"bottom"}};
constexpr EnumChoice oscPositions[]{{0,"floating"},{1,"top"},{2,"bottom"}};
constexpr EnumChoice resizeTiming[]{{0,"always"},{1,"onlyWhenOpen"},{2,"never"}};
constexpr EnumChoice resizeSize[]{{0,"fit-screen"},{1,"0.5"},{2,"1"},{3,"1.5"},{4,"2"}};
constexpr EnumChoice clickActions[]{{0,"none"},{1,"fullscreen"},{2,"pause"},{3,"hide-controls"},
    {4,"picture-in-picture"},{5,"ab-loop"},{6,"reset-speed"}};
constexpr EnumChoice screenshotFormats[]{{0,"png"},{1,"jpg"},{2,"jpeg"},{3,"webp"},{4,"jxl"},{5,"avif"}};
constexpr EnumChoice seekModes[]{{0,"relative"},{1,"exact"},{2,"auto"}};
constexpr EnumChoice scrollActions[]{{0,"volume"},{1,"seek"},{2,"none"},{4,"playback-speed"}};
constexpr EnumChoice rtsp[]{{0,"lavf"},{1,"tcp"},{2,"udp"},{3,"http"}};
using Area = PreferenceArea;
using Timing = ApplyTiming;
PreferenceDescriptor B(std::string_view key, Area area, bool value,
    std::string_view property={}, Timing timing=Timing::Application) {
    return {key,area,value,timing,{},{},{},property};
}
PreferenceDescriptor I(std::string_view key, Area area, std::int64_t value,
    double low, double high, std::string_view property={}, Timing timing=Timing::Application) {
    return {key,area,value,timing,low,high,{},property};
}
PreferenceDescriptor D(std::string_view key, Area area, double value,
    double low, double high, std::string_view property={}, Timing timing=Timing::Application) {
    return {key,area,value,timing,low,high,{},property};
}
PreferenceDescriptor S(std::string_view key, Area area, std::string value,
    std::string_view property={}, Timing timing=Timing::Application) {
    return {key,area,std::move(value),timing,{},{},{},property};
}
PreferenceDescriptor E(std::string_view key, Area area, std::int64_t value,
    std::span<EnumChoice const> choices, std::string_view property={}, Timing timing=Timing::Application) {
    return {key,area,value,timing,{},{},choices,property};
}
std::string NumberString(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return out.str();
}
std::string Serialize(PreferenceDescriptor const& d, PreferenceValue const& value) {
    if (!d.choices.empty()) {
        auto number=std::get<std::int64_t>(value);
        auto found=std::find_if(d.choices.begin(),d.choices.end(),[&](auto const& c){return c.value==number;});
        if(found==d.choices.end()) throw std::logic_error("Invalid enum escaped preference validation");
        return std::string(found->token);
    }
    return std::visit([](auto const& v)->std::string {
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,bool>) return v?"yes":"no";
        else if constexpr(std::is_same_v<T,std::int64_t>) return std::to_string(v);
        else if constexpr(std::is_same_v<T,double>) return NumberString(v);
        else return v;
    },value);
}
}
std::span<PreferenceDescriptor const> PreferenceSchema() {
    // Bounds are Windows-port input constraints, not claims that upstream had
    // identical validators. Defaults/enum ordinals are from the pinned snapshot.
    static const std::vector<PreferenceDescriptor> schema{
        I("videoThreads",Area::Video,0,0,1024,"vd-lavc-threads",Timing::NextMedia),
        E("hardwareDecoder",Area::Video,1,hardware,"hwdec",Timing::Immediate),
        I("audioThreads",Area::Audio,0,0,1024,"ad-lavc-threads",Timing::NextMedia),
        S("audioLanguage",Area::Audio,"","alang",Timing::NextMedia),
        S("audioDevice",Area::Audio,"auto","audio-device",Timing::Immediate),
        B("spdifAC3",Area::Audio,false,{},Timing::NextMedia),
        B("spdifDTS",Area::Audio,false,{},Timing::NextMedia),
        B("spdifDTSHD",Area::Audio,false,{},Timing::NextMedia),
        B("spdifEAC3",Area::Audio,false,{},Timing::NextMedia),
        B("spdifTRUEHD",Area::Audio,false,{},Timing::NextMedia),
        E("replayGain",Area::Audio,0,replay,"replaygain",Timing::Immediate),
        D("replayGainPreamp",Area::Audio,0,-150,150,"replaygain-preamp",Timing::Immediate),
        B("replayGainClip",Area::Audio,false,"replaygain-clip",Timing::Immediate),
        D("replayGainFallback",Area::Audio,0,-150,150,"replaygain-fallback",Timing::Immediate),
        E("gaplessAudio",Area::Audio,1,gapless,"gapless-audio",Timing::NextMedia),
        B("enableInitialVolume",Area::Audio,false,{},Timing::NextPlayer),
        D("initialVolume",Area::Audio,100,0,1000,{},Timing::NextPlayer),
        E("subOverrideLevel",Area::Subtitles,3,subOverride,"sub-ass-override",Timing::Immediate),
        E("secondarySubOverrideLevel",Area::Subtitles,3,subOverride,"secondary-sub-ass-override",Timing::Immediate),
        E("subBorderStyle",Area::Subtitles,1,border,"sub-border-style",Timing::Immediate),
        E("subAlignX",Area::Subtitles,1,alignX,"sub-align-x",Timing::Immediate),
        E("subAlignY",Area::Subtitles,2,alignY,"sub-align-y",Timing::Immediate),
        D("subBlur",Area::Subtitles,0,0,20,"sub-blur",Timing::Immediate),
        D("subSpacing",Area::Subtitles,0,-10,10,"sub-spacing",Timing::Immediate),
        I("subMarginX",Area::Subtitles,25,0,1000,"sub-margin-x",Timing::Immediate),
        I("subMarginY",Area::Subtitles,22,0,1000,"sub-margin-y",Timing::Immediate),
        S("subLang",Area::Subtitles,"","slang",Timing::NextMedia),
        S("defaultEncoding",Area::Subtitles,"auto","sub-codepage",Timing::NextMedia),
        B("displayInLetterBox",Area::Subtitles,true,"sub-use-margins",Timing::Immediate),
        B("subScaleWithWindow",Area::Subtitles,true,"sub-scale-by-window",Timing::Immediate),
        B("playlistAutoAdd",Area::Interface,true),
        E("subAutoLoadIINA",Area::Subtitles,2,subtitleAutoLoad),
        S("subAutoLoadPriorityString",Area::Subtitles,""),
        S("subAutoLoadSearchPath",Area::Subtitles,"./*"),
        B("alwaysOpenInNewWindow",Area::Window,true),
        B("groupSimultaneousOpensInPlaylist",Area::Window,false),
        B("allowDuplicatePlayers",Area::Window,false),
        B("fullScreenWhenOpen",Area::Window,false),
        B("displayTimeAndBatteryInFullScreen",Area::Window,false),
        B("autoSwitchToMusicMode",Area::Window,true),
        B("musicModeShowPlaylist",Area::Window,false),
        B("musicModeShowAlbumArt",Area::Window,true),
        B("alwaysFloatOnTop",Area::Window,false),
        B("pauseWhenPip",Area::Window,false),
        B("pauseWhenMinimized",Area::Window,false),
        B("pauseWhenInactive",Area::Window,false),
        B("pauseWhenGoesToSleep",Area::Window,true),
        B("playWhenEnteringFullScreen",Area::Window,false),
        B("pauseWhenLeavingFullScreen",Area::Window,false),
        B("preventScreenSaver",Area::Window,true),
        B("allowScreenSaverForAudio",Area::Window,false),
        B("usePhysicalResolution",Area::Window,true),
        B("unlockWindowAspectRatio",Area::Window,false),
        S("initialWindowSizePosition",Area::Window,"",{},Timing::NextPlayer),
        E("resizeWindowTiming",Area::Window,1,resizeTiming),
        E("resizeWindowOption",Area::Window,2,resizeSize),
        E("singleClickAction",Area::Interface,3,clickActions),
        E("doubleClickAction",Area::Interface,1,clickActions),
        E("rightClickAction",Area::Interface,2,clickActions),
        E("verticalScrollAction",Area::Interface,0,scrollActions),
        E("horizontalScrollAction",Area::Interface,1,scrollActions),
        I("relativeSeekAmount",Area::Interface,3,1,4),
        I("playbackSpeedScrollAmount",Area::Interface,3,1,4),
        B("wheelExactSeek",Area::Interface,false),
        E("useExactSeek",Area::Interface,0,seekModes),
        B("showBufferingThrobber",Area::Interface,true),
        B("showSeekingThrobber",Area::Interface,true),
        B("disableAnimations",Area::Interface,false),
        B("enableOSD",Area::Interface,true),
        B("disableOSDFileStartMsg",Area::Interface,false),
        B("disableOSDPauseResumeMsgs",Area::Interface,false),
        B("disableOSDSeekMsg",Area::Interface,false),
        B("disableOSDSpeedMsg",Area::Interface,false),
        D("osdAutoHideTimeout",Area::Interface,1,0,60),
        D("osdTextSize",Area::Interface,20,8,96),
        E("oscPosition",Area::Interface,0,oscPositions),
        D("controlBarPositionHorizontal",Area::Interface,0.5,0,1),
        D("controlBarPositionVertical",Area::Interface,0.1,0,1),
        B("controlBarStickToCenter",Area::Interface,true),
        B("disablePlaySliderScrolling",Area::Interface,false),
        B("disableVolumeSliderScrolling",Area::Interface,false),
        B("enableControlBarAutoHide",Area::Interface,true),
        D("controlBarAutoHideTimeout",Area::Interface,2.5,0,60),
        S("controlBarToolbarButtons",Area::Interface,"plugins,pip,playlist,settings"),
        B("showRemainingTime",Area::Interface,false),
        B("scaleRemainingTime",Area::Interface,false),
        I("timeDisplayPrecision",Area::Interface,0,0,3),
        S("screenShotFolder",Area::Interface,""), // No automatic destination before explicit selection.
        S("screenShotTemplate",Area::Interface,"%F-%n"),
        B("screenshotSaveToFile",Area::Interface,true),
        B("screenshotCopyToClipboard",Area::Interface,false),
        E("screenShotFormat",Area::Interface,0,screenshotFormats),
        B("screenshotShowPreview",Area::Interface,true),
        B("enableThumbnailPreview",Area::Interface,true),
        B("enableThumbnailForRemoteFiles",Area::Interface,false),
        I("maxThumbnailPreviewCacheSize",Area::Interface,500,0,102400),
        I("thumbnailWidth",Area::Interface,120,32,2048),
        B("enableCache",Area::Network,true,"cache",Timing::NextMedia),
        I("defaultCacheSize",Area::Network,153600,0,10485760,{},Timing::NextMedia),
        B("cachePauseInitial",Area::Network,false,"cache-pause-initial",Timing::NextMedia),
        D("cachePauseWait",Area::Network,1,0,3600,"cache-pause-wait",Timing::NextMedia),
        I("secPrefech",Area::Network,36000,0,86400,"cache-secs",Timing::NextMedia),
        E("transportRTSPThrough",Area::Network,1,rtsp,"rtsp-transport",Timing::NextMedia)
    };
    return schema;
}
std::span<std::string_view const> SessionPreferenceKeys() {
    static const std::vector<std::string_view> keys=[] {
        std::vector<std::string_view> result;
        for(auto const& descriptor:PreferenceSchema()) {
            if(!descriptor.mpvProperty.empty() || descriptor.key.starts_with("spdif") ||
                descriptor.key=="defaultCacheSize") result.push_back(descriptor.key);
        }
        return result;
    }();
    return keys;
}
PreferenceDescriptor const* FindPreference(std::string_view key) {
    auto schema=PreferenceSchema();
    auto found=std::find_if(schema.begin(),schema.end(),[&](auto const& d){return d.key==key;});
    return found==schema.end()?nullptr:&*found;
}
std::optional<std::string> ValidatePreference(PreferenceDescriptor const& d, PreferenceValue const& value) {
    if(value.index()!=d.initial.index()) return "Value has the wrong type";
    std::optional<double> numeric;
    if(auto p=std::get_if<double>(&value)) {
        if(!std::isfinite(*p)) return "Number must be finite";
        numeric=*p;
    }
    if(auto p=std::get_if<std::int64_t>(&value)) numeric=static_cast<double>(*p);
    if(numeric && ((d.minimum && *numeric<*d.minimum)||(d.maximum && *numeric>*d.maximum)))
        return "Number is outside the allowed range";
    if(!d.choices.empty()) {
        auto number=std::get<std::int64_t>(value);
        if(std::none_of(d.choices.begin(),d.choices.end(),[&](auto const& c){return c.value==number;}))
            return "Unknown enumeration value";
    }
    if(auto p=std::get_if<std::string>(&value)) {
        if(p->size()>4096) return "Text exceeds the 4096-byte limit";
        if(std::any_of(p->begin(),p->end(),[](unsigned char c){return c<32 || c==127;}))
            return "Text must not contain control characters";
        if(d.key=="controlBarToolbarButtons" && !ParseToolbarActions(*p))
            return "Toolbar must contain at most five unique supported action tokens";
        if(d.key=="audioDevice" && p->empty()) return "Audio device must not be empty";
    }
    return {};
}
PreferenceValue const& PreferenceStore::Get(std::string_view key) const {
    auto descriptor=FindPreference(key);
    if(!descriptor) throw std::invalid_argument("Unknown preference");
    auto found=overrides_.find(key);
    return found==overrides_.end()?descriptor->initial:found->second;
}
bool PreferenceStore::Set(std::string_view key, PreferenceValue value, std::string& error) {
    auto descriptor=FindPreference(key);
    if(!descriptor) { error="Unknown preference"; return false; }
    if(auto issue=ValidatePreference(*descriptor,value)) { error=*issue; return false; }
    error.clear();
    if(value==descriptor->initial) overrides_.erase(std::string(key));
    else overrides_.insert_or_assign(std::string(key),std::move(value));
    return true;
}
void PreferenceStore::Reset(std::string_view key) { overrides_.erase(std::string(key)); }
void PreferenceStore::ResetAll() { overrides_.clear(); }
std::vector<PreferenceIssue> PreferenceStore::Restore(PreferenceValues const& persisted) {
    PreferenceStore replacement;
    std::vector<PreferenceIssue> issues;
    for(auto const& [key,value]:persisted) {
        std::string error;
        if(!replacement.Set(key,value,error)) issues.push_back({key,std::move(error)});
    }
    overrides_=std::move(replacement.overrides_);
    return issues;
}
bool PreferenceStore::Boolean(std::string_view key) const { return std::get<bool>(Get(key)); }
std::int64_t PreferenceStore::Integer(std::string_view key) const { return std::get<std::int64_t>(Get(key)); }
double PreferenceStore::Number(std::string_view key) const {
    auto const& value=Get(key);
    if(auto p=std::get_if<std::int64_t>(&value)) return static_cast<double>(*p);
    return std::get<double>(value);
}
std::string const& PreferenceStore::String(std::string_view key) const { return std::get<std::string>(Get(key)); }
double InitialPlayerVolume(PreferenceStore const& preferences, double rememberedVolume, double maxVolume) {
    // Persisted UserSettings normally validates these bounds. Retain a safe
    // fallback here rather than sending a non-finite value into an audio backend.
    const double ceiling = std::isfinite(maxVolume) ? std::clamp(maxVolume, 0.0, 1000.0) : 100.0;
    double requested = preferences.Boolean("enableInitialVolume") ?
        preferences.Number("initialVolume") : rememberedVolume;
    if (!std::isfinite(requested)) requested = 100.0;
    return std::clamp(requested, 0.0, ceiling);
}
std::vector<PreferenceOption> MpvPreferenceOptions(PreferenceStore const& settings) {
    std::vector<PreferenceOption> out;
    for(auto const& d:PreferenceSchema()) if(!d.mpvProperty.empty())
        out.push_back({std::string(d.key),std::string(d.mpvProperty),Serialize(d,settings.Get(d.key)),d.timing});
    std::string spdif;
    for(auto [key,token]:{std::pair{"spdifAC3","ac3"},{"spdifDTS","dts"},{"spdifDTSHD","dts-hd"},
                         {"spdifEAC3","eac3"},{"spdifTRUEHD","truehd"}}) {
        if(settings.Boolean(key)) { if(!spdif.empty()) spdif+=','; spdif+=token; }
    }
    out.push_back({"spdifAC3","audio-spdif",std::move(spdif),Timing::NextMedia});
    out.push_back({"defaultCacheSize","demuxer-max-bytes",std::to_string(settings.Integer("defaultCacheSize"))+"KiB",Timing::NextMedia});
    out.push_back({"displayInLetterBox","sub-ass-force-margins",settings.Boolean("displayInLetterBox")?"yes":"no",Timing::Immediate});
    return out;
}
std::vector<PreferenceOption> ChangedMpvPreferences(PreferenceStore const& before, PreferenceStore const& after) {
    auto previous=MpvPreferenceOptions(before);
    auto next=MpvPreferenceOptions(after);
    std::erase_if(next,[&](auto const& entry){
        return std::any_of(previous.begin(),previous.end(),[&](auto const& old){return old.property==entry.property && old.value==entry.value;});
    });
    return next;
}
}

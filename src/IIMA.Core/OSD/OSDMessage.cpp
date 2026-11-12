// Static port of IINA OSDMessage.swift and OSDView.swift.
// Copyright (c) 2016-2026 lhc and IINA contributors.
// Source version: references/iina-source.txt; license: references/iina/LICENSE (GPL v3).
#include "OSDMessage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace iima {
namespace {

std::string Number(double value, unsigned precision, const OSDFormatting& format) {
    if (format.formatNumber) return format.formatNumber(value, precision);
    if (!std::isfinite(value)) return "--";
    std::ostringstream result;
    result.imbue(std::locale::classic());
    result << std::fixed << std::setprecision(static_cast<int>(precision)) << value;
    auto text = result.str();
    if (precision != 0) {
        while (text.ends_with('0')) text.pop_back();
        if (text.ends_with('.')) text.pop_back();
    }
    return text;
}

std::string Localized(const OSDFormatting& format, std::string_view key,
                      std::string_view fallback, std::span<const std::string> arguments = {}) {
    if (format.localize) {
        if (auto localized = format.localize(key, arguments)) return *localized;
    }
    std::string result;
    for (std::size_t index = 0; index < fallback.size();) {
        if (index + 2 < fallback.size() && fallback[index] == '{' &&
            fallback[index + 2] == '}' && fallback[index + 1] >= '0' && fallback[index + 1] <= '9') {
            const auto argument = static_cast<std::size_t>(fallback[index + 1] - '0');
            if (argument < arguments.size()) result += arguments[argument];
            index += 3;
        } else result += fallback[index++];
    }
    return result;
}

std::string Expand(std::string_view text, const OSDPlaybackContext& context) {
    const auto time = [&](const std::optional<VideoTime>& value, unsigned precision) {
        return value ? value->StringRepresentation(precision) : context.timePlaceholder;
    };
    const std::array<std::pair<std::string_view, std::string>, 6> values{{
        {"duration", time(context.duration, 0)},
        {"preciseDuration", time(context.duration, 3)},
        {"position", time(context.position, 0)},
        {"precisePosition", time(context.position, 3)},
        {"currChapter", context.currentChapter == std::numeric_limits<std::int64_t>::max()
            ? std::to_string(context.currentChapter) : std::to_string(context.currentChapter + 1)},
        {"chapterCount", std::to_string(context.chapterCount)}
    }};
    std::string result;
    while (!text.empty()) {
        const auto open = text.find("{{");
        if (open == std::string_view::npos) { result += text; break; }
        result += text.substr(0, open);
        const auto close = text.find("}}", open + 2);
        if (close == std::string_view::npos) { result += text.substr(open); break; }
        auto key = text.substr(open + 2, close - open - 2);
        while (key.starts_with(' ')) key.remove_prefix(1);
        while (key.ends_with(' ')) key.remove_suffix(1);
        const auto found = std::find_if(values.begin(), values.end(),
            [key](const auto& entry) { return entry.first == key; });
        if (found != values.end()) result += found->second;
        // Unknown variables resolve to empty, as the upstream Mustache context.
        // percentPos is documented upstream but absent from OSDView's context.
        text.remove_prefix(close + 2);
    }
    return result;
}

double ToPercent(double value, double bound) noexcept {
    if (!std::isfinite(value)) return 0.5;
    return std::clamp(value + bound, 0.0, bound * 2.0) / (bound * 2.0);
}

bool HasStyledColon(OSDKind kind) noexcept {
    switch (kind) {
    case OSDKind::Volume: case OSDKind::Speed: case OSDKind::Aspect:
    case OSDKind::Crop: case OSDKind::Rotate: case OSDKind::Deinterlace:
    case OSDKind::AudioDelay: case OSDKind::SubtitlePosition: case OSDKind::SubtitleDelay:
    case OSDKind::SecondarySubtitlePosition: case OSDKind::SecondarySubtitleDelay:
    case OSDKind::Brightness: case OSDKind::Contrast: case OSDKind::Saturation:
    case OSDKind::Gamma: case OSDKind::Hue: case OSDKind::Track: case OSDKind::Chapter:
    case OSDKind::HardwareDecode: case OSDKind::ABLoop: case OSDKind::ABLoopUpdate:
        return true;
    default: return false;
    }
}
} // namespace

bool OSDMessage::IsDisabled(const OSDPreferences& preferences) const noexcept {
    switch (kind) {
    case OSDKind::FileStart: return preferences.disableFileStart;
    case OSDKind::Pause: case OSDKind::Resume: return preferences.disablePauseResume;
    case OSDKind::Seek: return preferences.disableSeek;
    case OSDKind::Speed: return preferences.disableSpeed;
    default: return false;
    }
}

bool OSDMessage::AlwaysEnabled() const noexcept {
    switch (kind) {
    case OSDKind::Canceled: case OSDKind::CannotConnect: case OSDKind::CannotLogin:
    case OSDKind::DownloadedSubtitle: case OSDKind::FileError: case OSDKind::FoundSubtitle:
    case OSDKind::NetworkError: case OSDKind::SubtitleQuotaExceeded: case OSDKind::SavedSubtitle:
    case OSDKind::StartFindingSubtitle: case OSDKind::TimedOut: return true;
    default: return false;
    }
}

bool OSDMessage::ShouldShow(const OSDPreferences& preferences) const noexcept {
    return AlwaysEnabled() || (preferences.enabled && !preferences.useMpvOSD && !IsDisabled(preferences));
}

bool OSDMessage::ShouldDispatch(const OSDPreferences& preferences,
    const OSDDispatchContext& context) const noexcept {
    if (!ShouldShow(preferences) || context.miniPlayer || !context.windowLoaded ||
        !context.playbackActive || context.persistentMessageShowing) return false;
    return !context.suppressWhileLoading || context.external || kind == OSDKind::FileStart;
}

std::optional<OSDPresentation> OSDMessage::Present(const OSDPreferences& preferences,
    const OSDPlaybackContext& context, const OSDFormatting& format) const {
    OSDPresentation result;
    const auto plain = [&](std::string_view key, std::string_view fallback) {
        result.title = Localized(format, key, fallback);
    };
    const auto argument = [&](std::string_view key, std::string_view fallback, std::string arg) {
        const std::array args{std::move(arg)};
        result.title = Localized(format, key, fallback, args);
    };
    const auto supplemental = [&](OSDType type, std::string detailText) {
        result.type = type;
        result.detail = std::move(detailText);
    };
    const auto progress = [&](double fraction, OSDType type = OSDType::WithProgress) {
        result.type = type;
        result.progress = std::isfinite(fraction) ? fraction : 0.0;
    };
    const auto delay = [&](std::string_view key, std::string_view description) {
        const auto base = std::string(key);
        if (value == 0) plain(base + ".nodelay", std::string(description) + ": No Delay");
        else argument(base + (value > 0 ? ".later" : ".earlier"),
            std::string(description) + (value > 0 ? ": {0}s Later" : ": {0}s Earlier"),
            Number(std::abs(value), 6, format));
        progress(ToPercent(value, 10));
    };
    switch (kind) {
    case OSDKind::FileStart: case OSDKind::Custom: result.title = text; break;
    case OSDKind::Pause: case OSDKind::Resume:
        plain(kind == OSDKind::Pause ? "osd.pause" : "osd.resume",
              kind == OSDKind::Pause ? "Pause" : "Resume");
        supplemental(OSDType::WithLeftToRightText, "{{position}} / {{duration}}"); break;
    case OSDKind::Seek:
        result.title = text + " / " + detail;
        progress(value, OSDType::WithPosition); break;
    case OSDKind::ShowTime:
        result.title = "{{precisePosition}} / {{preciseDuration}}";
        progress(value, OSDType::WithPosition); break;
    case OSDKind::Volume:
        argument("osd.volume", "Volume: {0}", Number(value, 0, format));
        progress(preferences.maximumVolume > 0 ? value / preferences.maximumVolume : 0); break;
    case OSDKind::Speed: argument("osd.speed", "Speed: {0}x", Number(value, 6, format)); break;
    case OSDKind::Aspect:
        argument("osd.aspect", "Aspect Ratio: {0}", text == "Default"
            ? Localized(format, "quicksetting.item_default", "Default") : text); break;
    case OSDKind::Crop:
        argument("osd.crop", "Crop: {0}", text == "None"
            ? Localized(format, "quicksetting.item_none", "None") : text); break;
    case OSDKind::Rotate: argument("osd.rotate", "Rotate: {0}\xC2\xB0", std::to_string(integer)); break;
    case OSDKind::Deinterlace: case OSDKind::HardwareDecode:
        argument(kind == OSDKind::Deinterlace ? "osd.deinterlace" : "osd.hwdec",
            kind == OSDKind::Deinterlace ? "Deinterlace: {0}" : "Hardware Decoding: {0}",
            Localized(format, enabled ? "general.on" : "general.off", enabled ? "On" : "Off")); break;
    case OSDKind::AudioDelay: delay("osd.audio_delay", "Audio Delay"); break;
    case OSDKind::SubtitleDelay: delay("osd.sub_delay", "Subtitle Delay"); break;
    case OSDKind::SecondarySubtitleDelay: delay("osd.sub_second_delay", "Secondary Subtitle Delay"); break;
    case OSDKind::SubtitlePosition: case OSDKind::SecondarySubtitlePosition:
        argument(kind == OSDKind::SubtitlePosition ? "osd.subtitle_pos" : "osd.sub_second_pos",
            kind == OSDKind::SubtitlePosition ? "Subtitle Position: {0}" : "Secondary Subtitle Position: {0}",
            Number(value, 6, format));
        progress(value / 100); break;
    case OSDKind::SubtitleHidden: plain("osd.sub_hidden", "Subtitles Hidden"); break;
    case OSDKind::SubtitleVisible: plain("osd.sub_visible", "Subtitles Visible"); break;
    case OSDKind::SecondarySubtitleHidden: plain("osd.sub_second_hidden", "Second Subtitles Hidden"); break;
    case OSDKind::SecondarySubtitleVisible: plain("osd.sub_second_visible", "Second Subtitles Visible"); break;
    case OSDKind::Mute: plain("osd.mute", "Mute"); break;
    case OSDKind::Unmute: plain("osd.unmute", "Unmute"); break;
    case OSDKind::Screenshot: plain("osd.screenshot", "Screenshot Captured"); break;
    case OSDKind::ABLoop: case OSDKind::ABLoopUpdate:
        if (loopStatus == OSDLoopStatus::Cleared) {
            if (kind == OSDKind::ABLoopUpdate) return std::nullopt;
            plain("osd.abloop.clear", "AB-Loop: Cleared");
        } else {
            plain(loopStatus == OSDLoopStatus::ASet ? "osd.abloop.a" : "osd.abloop.b",
                loopStatus == OSDLoopStatus::ASet ? "AB-Loop: A" : "AB-Loop: B");
            supplemental(OSDType::WithLeftToRightText,
                (kind == OSDKind::ABLoopUpdate ? text : "{{position}}") + " / {{duration}}");
        }
        break;
    case OSDKind::Stop: plain("osd.stop", "Stop"); break;
    case OSDKind::Chapter:
        argument("osd.chapter", "Chapter: {0}", text);
        supplemental(OSDType::WithLeftToRightText, "({{currChapter}}/{{chapterCount}}) {{position}} / {{duration}}"); break;
    case OSDKind::Track:
        result.title = Localized(format, trackType == OSDTrackType::Video ? "track.video" :
            trackType == OSDTrackType::Audio ? "track.audio" :
            trackType == OSDTrackType::SecondarySubtitle ? "track.secondary_sub" : "track.sub",
            trackType == OSDTrackType::Video ? "Video" : trackType == OSDTrackType::Audio ? "Audio" :
            trackType == OSDTrackType::SecondarySubtitle ? "Secondary Subtitle" : "Subtitle")
            + ": " + text;
        break;
    case OSDKind::SubtitleScale: argument("osd.subtitle_scale", "Subtitle Scale: {0}x", Number(value, 6, format)); break;
    case OSDKind::AddToPlaylist: argument("osd.add_to_playlist", "Added {0} Files to Playlist", std::to_string(integer)); break;
    case OSDKind::ClearPlaylist: plain("osd.clear_playlist", "Cleared Playlist"); break;
    case OSDKind::Contrast: case OSDKind::Hue: case OSDKind::Saturation:
    case OSDKind::Brightness: case OSDKind::Gamma: {
        std::string_view key, label;
        switch (kind) {
        case OSDKind::Contrast: key = "contrast"; label = "Contrast"; break;
        case OSDKind::Hue: key = "hue"; label = "Hue"; break;
        case OSDKind::Saturation: key = "saturation"; label = "Saturation"; break;
        case OSDKind::Brightness: key = "brightness"; label = "Brightness"; break;
        default: key = "gamma"; label = "Gamma"; break;
        }
        argument("osd.video_eq." + std::string(key), std::string(label) + ": {0}", std::to_string(integer));
        progress(ToPercent(static_cast<double>(integer), 100));
        break;
    }
    case OSDKind::AddFilter: argument("osd.filter_added", "Added Filter: {0}", text); break;
    case OSDKind::RemoveFilter: plain("osd.filter_removed", "Removed Filter"); break;
    case OSDKind::StartFindingSubtitle:
        plain("osd.find_online_sub", "Finding online subtitles...");
        supplemental(OSDType::WithText, Localized(format, "osd.find_online_sub.source", "from") + " " + text); break;
    case OSDKind::FoundSubtitle:
        if (integer == 0) plain("osd.sub_not_found", "No subtitles found.");
        else argument("osd.sub_found", "{0} subtitle(s) found.", std::to_string(integer)); break;
    case OSDKind::DownloadingSubtitle:
        argument("osd.sub_downloading", "Downloading {0} subtitles", std::to_string(integer));
        supplemental(OSDType::WithText, Localized(format, "osd.find_online_sub.source", "from") + " " + text); break;
    case OSDKind::DownloadedSubtitle:
        plain("osd.sub_downloaded", "Subtitle downloaded");
        supplemental(OSDType::WithText, text); break;
    case OSDKind::SavedSubtitle: plain("osd.sub_saved", "Subtitle saved"); break;
    case OSDKind::NetworkError: plain("osd.network_error", "Network error"); break;
    case OSDKind::FileError: plain("osd.file_error", "Error reading file"); break;
    case OSDKind::CannotLogin: plain("osd.cannot_login", "Cannot login"); break;
    case OSDKind::Canceled: plain("osd.canceled", "Canceled"); break;
    case OSDKind::CannotConnect: plain("osd.cannot_connect", "Cannot connect"); break;
    case OSDKind::TimedOut: plain("osd.timed_out", "Timed out"); break;
    case OSDKind::SubtitleQuotaExceeded: {
        plain("osd.sub_quota_exceeded", "Subtitle download limit reached");
        std::string secondary;
        if (optionalText && !optionalText->empty()) {
            const std::array args{*optionalText};
            secondary = Localized(format, "osd.sub_quota_exceeded.detail", "Try again after {0}", args);
        } else secondary = Localized(format, "osd.sub_quota_exceeded.detail_unknown", "Try again later");
        supplemental(OSDType::WithText, std::move(secondary)); break;
    }
    case OSDKind::FileLoop: plain("osd.file_loop", "Enable file looping"); break;
    case OSDKind::PlaylistLoop: plain("osd.playlist_loop", "Enable playlist looping"); break;
    case OSDKind::NoLoop: plain("osd.no_loop", "Disable loop"); break;
    case OSDKind::CustomWithDetail:
        result.title = text;
        supplemental(OSDType::WithText, detail); break;
    }
    result.title = Expand(result.title, context);
    result.detail = Expand(result.detail, context);
    result.titleLeftToRight = result.type == OSDType::WithPosition;
    result.detailLeftToRight = result.type == OSDType::WithLeftToRightText;
    if (kind == OSDKind::Seek) {
        result.titleRuns = {{text, false, false}, {" / " + detail, true, true}};
    } else if (HasStyledColon(kind)) {
        // Swift split omits leading/trailing empty parts and drops the colon.
        const auto first = result.title.find_first_not_of(':');
        const auto colon = first == std::string::npos ? std::string::npos : result.title.find(':', first);
        if (colon != std::string::npos && colon + 1 < result.title.size()) {
            result.titleRuns = {{result.title.substr(first, colon - first), true, true},
                                {result.title.substr(colon + 1), false, false}};
        }
    }
    return result;
}

} // namespace iima

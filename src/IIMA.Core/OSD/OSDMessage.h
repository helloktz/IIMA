// Static port of IINA OSDMessage.swift and OSDView.swift.
// Copyright (c) 2016-2026 lhc and IINA contributors.
// Source version: references/iina-source.txt; license: references/iina/LICENSE (GPL v3).
#pragma once

#include "../Media/VideoTime.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iima {

enum class OSDKind {
    FileStart, Pause, Resume, Seek, ShowTime, Volume, Speed, Aspect, Crop, Rotate,
    Deinterlace, HardwareDecode, AudioDelay, SubtitleDelay, SubtitleScale,
    SubtitleHidden, SubtitleVisible, SecondarySubtitleDelay, SecondarySubtitleHidden,
    SecondarySubtitlePosition, SecondarySubtitleVisible, SubtitlePosition, Mute,
    Unmute, Screenshot, ABLoop, ABLoopUpdate, Stop, Chapter, Track, AddToPlaylist,
    ClearPlaylist, Contrast, Hue, Saturation, Brightness, Gamma, AddFilter, RemoveFilter,
    StartFindingSubtitle, FoundSubtitle, DownloadingSubtitle, DownloadedSubtitle,
    SavedSubtitle, CannotLogin, FileError, NetworkError, Canceled, CannotConnect,
    TimedOut, SubtitleQuotaExceeded, FileLoop, PlaylistLoop, NoLoop, Custom, CustomWithDetail
};

enum class OSDLoopStatus { Cleared, ASet, BSet };
enum class OSDTrackType { Video, Audio, Subtitle, SecondarySubtitle };
enum class OSDType { Normal, WithText, WithProgress, WithPosition, WithLeftToRightText };

struct OSDPreferences {
    bool enabled{true};
    bool useMpvOSD{};
    bool disableFileStart{};
    bool disablePauseResume{};
    bool disableSeek{};
    bool disableSpeed{};
    double maximumVolume{100.0};
};

struct OSDDispatchContext {
    bool miniPlayer{};
    bool windowLoaded{};
    bool playbackActive{};
    bool suppressWhileLoading{};
    bool external{};
    bool persistentMessageShowing{};
};

struct OSDPlaybackContext {
    std::optional<VideoTime> position;
    std::optional<VideoTime> duration;
    std::int64_t currentChapter{-1}; // mpv zero-based chapter index
    std::size_t chapterCount{};
    std::string timePlaceholder{"--:--:--"};
};

struct OSDTextRun {
    std::string text;
    bool secondaryColor{};
    bool smallFont{};
};

struct OSDPresentation {
    std::string title;
    OSDType type{OSDType::Normal};
    std::string detail;
    double progress{};
    // Semantic text runs; the WinUI layer selects fonts/colors. No Cocoa types
    // or renderer ownership are retained by the portable message model.
    std::vector<OSDTextRun> titleRuns;
    bool titleLeftToRight{};
    bool detailLeftToRight{};
};

struct OSDFormatting {
    // Receives an upstream localization key and argument strings. Return nullopt
    // to use the built-in English fallback. The adapter owns platform format
    // syntax; the core never passes localized text to printf as a format string.
    std::function<std::optional<std::string>(std::string_view,
        std::span<const std::string>)> localize;
    // Windows adapter can provide grouping/decimal separators from user locale.
    // Precision is 0 for volume, 6 for IINA groupedStringUpTo6Decimals.
    std::function<std::string(double, unsigned)> formatNumber;
};

struct OSDMessage {
    OSDKind kind{OSDKind::Custom};
    // Text for file/name/custom/track-readable-title. Seek uses text=current,
    // detail=total. ABLoopUpdate uses text=position. Subtitle download uses
    // text=source. Quota uses optionalText=reset time. CustomWithDetail uses both.
    std::string text;
    std::string detail;
    std::optional<std::string> optionalText;
    double value{}; // progress, gain, rate, delay, subtitle position/scale
    std::int64_t integer{}; // counts, rotation, video EQ
    bool enabled{}; // deinterlace / hardware decode
    OSDLoopStatus loopStatus{OSDLoopStatus::Cleared};
    OSDTrackType trackType{OSDTrackType::Video};

    [[nodiscard]] bool IsDisabled(const OSDPreferences&) const noexcept;
    [[nodiscard]] bool AlwaysEnabled() const noexcept;
    [[nodiscard]] bool ShouldShow(const OSDPreferences&) const noexcept;
    // PlayerCore.sendOSD and MainWindowController.displayOSD admission guards.
    [[nodiscard]] bool ShouldDispatch(const OSDPreferences&, const OSDDispatchContext&) const noexcept;
    // Invalid ABLoopUpdate(Cleared) returns nullopt instead of terminating the
    // application, which is what upstream Logger.fatal would do.
    [[nodiscard]] std::optional<OSDPresentation> Present(
        const OSDPreferences&, const OSDPlaybackContext&, const OSDFormatting& = {}) const;
};

} // namespace iima

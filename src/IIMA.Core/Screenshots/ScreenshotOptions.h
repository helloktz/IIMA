// Ported screenshot policy from IINA PlayerCore.swift / Preference.swift.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0, references/iina/LICENSE.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace iima {
enum class ScreenshotFormat { Png, Jpg, Jpeg, Webp, Jxl, Avif };
struct ScreenshotOptions {
    bool saveToFile{true};
    bool copyToClipboard{false};
    bool includeSubtitles{true};
    bool showPreview{true};
    ScreenshotFormat format{ScreenshotFormat::Png};
    // This is an mpv template, not a filesystem path. Only mpv evaluates it.
    std::string filenameTemplate{"%F-%n"};
};
std::string_view ScreenshotExtension(ScreenshotFormat format) noexcept;
std::optional<ScreenshotFormat> ParseScreenshotFormat(std::string_view extension) noexcept;
// Options for mpv's managed screenshot command. screenshot-to-file uses its
// explicit filename instead, and must not pretend filenameTemplate was applied.
std::vector<std::pair<std::string, std::string>> ScreenshotMpvOptions(const ScreenshotOptions& options);
struct ScreenshotPreviewSize { double width{}; double height{}; };
ScreenshotPreviewSize FitScreenshotPreview(double width, double height,
    double maximumWidth = 300, double maximumHeight = 200) noexcept;
// Windows-safe suggested leaf name for a SaveFilePicker. This is deliberately
// not an interpreter for arbitrary mpv templates. sequence is caller-owned.
std::string SuggestedScreenshotFilename(std::string_view mediaFilename,
    std::uint64_t sequence, ScreenshotFormat format);

// Only complete the request identified by the actual command reply. Do not
// discover "latest screenshot" by directory timestamp: another player or
// another screenshot can write into that directory concurrently.
struct ScreenshotCompletion {
    std::uint64_t commandId{};
    std::uint64_t generation{};
    ScreenshotOptions options;
    std::string stagingPath;
    std::optional<std::string> destinationPath;
};
class ScreenshotPending final {
public:
    // One active job per owner. Reject rather than silently lose a temporary file.
    bool Begin(ScreenshotCompletion completion);
    std::optional<ScreenshotCompletion> Take(std::uint64_t commandId);
    std::optional<ScreenshotCompletion> Cancel();
    bool Busy() const noexcept;
private:
    std::optional<ScreenshotCompletion> pending_;
};
}

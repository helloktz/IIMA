// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0, references/iina/LICENSE.
#include "ScreenshotOptions.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace iima {
std::string_view ScreenshotExtension(ScreenshotFormat format) noexcept {
    switch (format) {
    case ScreenshotFormat::Png: return "png";
    case ScreenshotFormat::Jpg: return "jpg";
    case ScreenshotFormat::Jpeg: return "jpeg";
    case ScreenshotFormat::Webp: return "webp";
    case ScreenshotFormat::Jxl: return "jxl";
    case ScreenshotFormat::Avif: return "avif";
    }
    return {};
}
std::optional<ScreenshotFormat> ParseScreenshotFormat(std::string_view extension) noexcept {
    if (extension.starts_with('.')) extension.remove_prefix(1);
    for (auto format : {ScreenshotFormat::Png, ScreenshotFormat::Jpg, ScreenshotFormat::Jpeg,
         ScreenshotFormat::Webp, ScreenshotFormat::Jxl, ScreenshotFormat::Avif}) {
        if (ScreenshotExtension(format) == extension) return format;
    }
    return std::nullopt;
}
std::vector<std::pair<std::string, std::string>> ScreenshotMpvOptions(const ScreenshotOptions& options) {
    auto format = ScreenshotExtension(options.format);
    if (format.empty()) throw std::invalid_argument("Unknown screenshot format");
    if (options.filenameTemplate.empty() || options.filenameTemplate.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid screenshot template");
    // MPVController enables software screenshots for JXL to retain upstream's
    // HDR workaround. Encoder availability and HDR output are not asserted.
    return {{"screenshot-format", std::string(format)},
        {"screenshot-sw", options.format == ScreenshotFormat::Jxl ? "yes" : "no"},
        {"screenshot-template", options.filenameTemplate}};
}
ScreenshotPreviewSize FitScreenshotPreview(double width, double height,
    double maximumWidth, double maximumHeight) noexcept {
    if (!std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(maximumWidth) || !std::isfinite(maximumHeight) ||
        width <= 0 || height <= 0 || maximumWidth <= 0 || maximumHeight <= 0) return {};
    const auto scale = std::min({1.0, maximumWidth / width, maximumHeight / height});
    return {width * scale, height * scale};
}
std::string SuggestedScreenshotFilename(std::string_view mediaFilename,
    std::uint64_t sequence, ScreenshotFormat format) {
    auto extension = ScreenshotExtension(format);
    if (extension.empty()) throw std::invalid_argument("Unknown screenshot format");
    auto slash = mediaFilename.find_last_of("/\\");
    if (slash != std::string_view::npos) mediaFilename.remove_prefix(slash + 1);
    auto dot = mediaFilename.find_last_of('.');
    if (dot != std::string_view::npos && dot != 0) mediaFilename = mediaFilename.substr(0, dot);
    std::string name;
    for (unsigned char character : mediaFilename) {
        if (character < 32 || character == '<' || character == '>' || character == ':' ||
            character == '"' || character == '/' || character == '\\' ||
            character == '|' || character == '?' || character == '*') name += '_';
        else name += static_cast<char>(character);
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    // Bound the leaf in UTF-8 bytes without splitting a code point. This leaves
    // ample room for sequence and extension within a Windows path component.
    if (name.size() > 180) {
        std::size_t end = 180;
        while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80) --end;
        name.resize(end);
    }
    if (name.empty()) name = "IIMA-screenshot";
    // A DOS device name remains reserved even with an extension; CON.foo-1
    // would still be invalid, so inspect the portion before the first dot.
    auto stem = name.substr(0, name.find('.'));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    for (auto& c : stem) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    bool device = stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL";
    if (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
        stem[3] >= '1' && stem[3] <= '9') device = true;
    if (device) name.insert(0, "_");
    return name + '-' + std::to_string(sequence) + '.' + std::string(extension);
}
bool ScreenshotPending::Begin(ScreenshotCompletion completion) {
    if (pending_ || !completion.commandId || completion.stagingPath.empty()) return false;
    pending_ = std::move(completion);
    return true;
}
std::optional<ScreenshotCompletion> ScreenshotPending::Take(std::uint64_t commandId) {
    if (!pending_ || pending_->commandId != commandId) return std::nullopt;
    return Cancel();
}
std::optional<ScreenshotCompletion> ScreenshotPending::Cancel() {
    auto result = std::move(pending_);
    pending_.reset();
    return result;
}
bool ScreenshotPending::Busy() const noexcept { return pending_.has_value(); }
}

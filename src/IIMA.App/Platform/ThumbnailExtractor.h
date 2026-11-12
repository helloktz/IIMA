// Windows adaptation of IINA PlayerCore.generateThumbnails and FFmpegController.
// IINA business rules copyright (c) 2017 lhc and contributors, GPL-3.0.
// See references/iina/LICENSE and references/iina-source.txt.
#pragma once
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace iima {
struct ThumbnailRequest {
    std::filesystem::path path;
    double seconds{};
    // Echoed, never inferred from the current player after work completes.
    std::uint64_t generation{};
    std::uint32_t maximumWidth{320};
    std::uint32_t maximumHeight{180};
};
enum class ThumbnailExtractionStatus { Ready, Cancelled, Unsupported, Failed };
struct ThumbnailExtractionResult {
    ThumbnailExtractionStatus status{ThumbnailExtractionStatus::Failed};
    std::uint64_t generation{};
    double requestedSeconds{};
    double actualSeconds{};
    std::uint32_t width{}, height{};
    std::vector<std::uint8_t> jpeg;
    std::wstring error;
};
// Call only on a background thread. Owns COM and MF startup on that thread;
// no COM or borrowed pixels escape. The caller schedules work, cancels the old
// stop_source on media/hover change and checks generation before UI projection.
// Cancellation is cooperative between native calls, not an interrupt of a
// blocked third-party codec. Never synchronously join this call from the UI.
// Local fixed/removable drive files only, no URL, UNC, ADS, or reparse traversal.
// Uses installed Media Foundation codecs, not libmpv/FFmpeg: unsupported formats
// show no preview, they do NOT stop or seek the main player. No audio decoding.
ThumbnailExtractionResult ExtractThumbnail(ThumbnailRequest const& request,
    std::stop_token stop = {});
}

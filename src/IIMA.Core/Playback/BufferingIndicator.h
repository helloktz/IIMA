// IINA BufferIndicatorView.swift / FloatingPointByteCountFormatter.swift.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include "PlaybackState.h"
namespace iima {
enum class BufferingIndicatorKind { Hidden, Opening, Buffering, Seeking };
struct BufferingIndicatorState {
    BufferingIndicatorKind kind{BufferingIndicatorKind::Hidden};
    std::optional<int> percent;
    std::string details;
};
// Cache percent is defined only while paused-for-cache; never apply it to
// seeking progress. Missing/invalid telemetry stays unknown, not fabricated 0.
BufferingIndicatorState ProjectBufferingIndicator(PlaybackSnapshot const&,
    bool showBuffering, bool showSeeking);
std::string FormatCacheBytes(std::int64_t bytes,bool binary);
}

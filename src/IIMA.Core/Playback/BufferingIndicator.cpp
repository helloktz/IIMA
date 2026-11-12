// IINA BufferIndicatorView.update/updateNetworkState and byte formatter.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "BufferingIndicator.h"
#include "../Network/MediaUrl.h"
#include <array>
#include <iomanip>
#include <locale>
#include <sstream>
namespace iima {
std::string FormatCacheBytes(std::int64_t bytes,bool binary) {
    if (bytes<0) return {};
    constexpr std::array<char const*,4> decimal{"B","KB","MB","GB"};
    constexpr std::array<char const*,4> powers{"B","KiB","MiB","GiB"};
    const auto divisor=binary ? 1024.0 : 1000.0;
    double value=static_cast<double>(bytes); std::size_t unit{};
    while (value>=divisor && unit<3) { value/=divisor; ++unit; }
    std::ostringstream stream; stream.imbue(std::locale::classic());
    stream<<std::fixed<<std::setprecision(unit?2:0)<<value;
    auto text=stream.str();
    if (unit) {
        while (!text.empty() && text.back()=='0') text.pop_back();
        if (!text.empty() && text.back()=='.') text.pop_back();
    }
    return text+" "+(binary?powers[unit]:decimal[unit]);
}
BufferingIndicatorState ProjectBufferingIndicator(PlaybackSnapshot const& state,
    bool showBuffering,bool showSeeking) {
    BufferingIndicatorState result;
    if (state.session!=SessionState::Ready ||
        (state.media!=MediaState::Loading && state.media!=MediaState::Loaded)) return result;
    const bool network=state.demuxerViaNetwork.value_or(IsNetworkMediaPath(state.path));
    if (!network) return result;
    if (state.media==MediaState::Loading) {
        if (showBuffering) result.kind=BufferingIndicatorKind::Opening;
        return result;
    }
    if (state.buffering && showBuffering) {
        result.kind=BufferingIndicatorKind::Buffering;
        if (state.cacheBufferingPercent && *state.cacheBufferingPercent>=0 && *state.cacheBufferingPercent<=100) result.percent=static_cast<int>(*state.cacheBufferingPercent);
    } else if (state.seeking && showSeeking) result.kind=BufferingIndicatorKind::Seeking;
    else return result;
    if (state.cacheForwardBytes) result.details=FormatCacheBytes(*state.cacheForwardBytes,true);
    if (state.cacheBytesPerSecond) {
        if (!result.details.empty()) result.details+="  ·  ";
        result.details+=FormatCacheBytes(*state.cacheBytesPerSecond,false)+"/s";
    }
    return result;
}
}

// IINA MainWindowController.setWindowAspectRatio, adapted to Win32 sizing edges.
// Copyright (c) 2016-2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "AspectResize.h"
#include <algorithm>
#include <cmath>

namespace iima {
namespace {
bool Positive(double v) { return std::isfinite(v) && v>0 && v<=1000000; }
bool Rectangle(GeometryRect const& r) {
    return Positive(r.width) && Positive(r.height) && std::isfinite(r.x) && std::isfinite(r.y) &&
        std::abs(r.x)<=1000000000 && std::abs(r.y)<=1000000000;
}
}
std::optional<GeometryRect> ConstrainVideoResize(GeometryRect proposed, GeometryRect work,
    SizingEdge edge, WindowAspectConstraint const& c) noexcept {
    const auto raw=static_cast<int>(edge);
    if (raw<1 || raw>8 || !Rectangle(proposed) || !Rectangle(work) ||
        !Positive(c.video.width) || !Positive(c.video.height) ||
        !Positive(c.minimum.width) || !Positive(c.minimum.height) ||
        !Positive(c.projectedOuter.width) || !Positive(c.projectedOuter.height) ||
        !std::isfinite(c.chrome.width) || !std::isfinite(c.chrome.height) ||
        c.chrome.width<0 || c.chrome.height<0 ||
        c.chrome.width>=c.projectedOuter.width || c.chrome.height>=c.projectedOuter.height ||
        c.dpi<48 || c.dpi>768) return {};
    const bool left=edge==SizingEdge::Left || edge==SizingEdge::TopLeft || edge==SizingEdge::BottomLeft;
    const bool right=edge==SizingEdge::Right || edge==SizingEdge::TopRight || edge==SizingEdge::BottomRight;
    const bool top=edge==SizingEdge::Top || edge==SizingEdge::TopLeft || edge==SizingEdge::TopRight;
    const bool bottom=edge==SizingEdge::Bottom || edge==SizingEdge::BottomLeft || edge==SizingEdge::BottomRight;
    const auto anchorX=left ? proposed.x+proposed.width : right ? proposed.x : proposed.x+proposed.width/2;
    const auto anchorY=top ? proposed.y+proposed.height : bottom ? proposed.y : proposed.y+proposed.height/2;
    double availableWidth=work.width, availableHeight=work.height;
    if (anchorX>=work.x && anchorX<=work.x+work.width) {
        availableWidth=left ? anchorX-work.x : right ? work.x+work.width-anchorX :
            2*std::min(anchorX-work.x,work.x+work.width-anchorX);
    }
    if (anchorY>=work.y && anchorY<=work.y+work.height) {
        availableHeight=top ? anchorY-work.y : bottom ? work.y+work.height-anchorY :
            2*std::min(anchorY-work.y,work.y+work.height-anchorY);
    }
    // If an old anchor cannot fit even a single video pixel, visibility takes
    // priority and the final clamp may translate the window away from it.
    if (availableWidth-c.chrome.width<1) availableWidth=work.width;
    if (availableHeight-c.chrome.height<1) availableHeight=work.height;
    const auto maximum=std::min((availableWidth-c.chrome.width)/c.video.width,
        (availableHeight-c.chrome.height)/c.video.height);
    const auto onePixel=std::max(1/c.video.width,1/c.video.height);
    if (!std::isfinite(maximum) || maximum<onePixel) return {};
    const auto minimum=std::max({onePixel,(c.minimum.width-c.chrome.width)/c.video.width,
        (c.minimum.height-c.chrome.height)/c.video.height});
    const auto width=proposed.width-c.chrome.width, height=proposed.height-c.chrome.height;
    double requested{};
    if (!(top || bottom)) requested=width/c.video.width;
    else if (!(left || right)) requested=height/c.video.height;
    else {
        // Closest point to the requested corner on the video-aspect line;
        // neither horizontal nor vertical corner motion is arbitrarily ignored.
        requested=(width*c.video.width+height*c.video.height)/
            (c.video.width*c.video.width+c.video.height*c.video.height);
    }
    const auto factor=std::min(maximum,std::max(minimum,requested));
    GeometryRect result{0,0,c.video.width*factor+c.chrome.width,c.video.height*factor+c.chrome.height};
    result.width=std::min(result.width,work.width);
    result.height=std::min(result.height,work.height);
    result.x=left ? anchorX-result.width : right ? anchorX : anchorX-result.width/2;
    result.y=top ? anchorY-result.height : bottom ? anchorY : anchorY-result.height/2;
    result.x=std::clamp(result.x,work.x,work.x+work.width-result.width);
    result.y=std::clamp(result.y,work.y,work.y+work.height-result.height);
    return Rectangle(result) ? std::optional{result} : std::nullopt;
}
}

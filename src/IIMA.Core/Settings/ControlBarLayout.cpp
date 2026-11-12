// IINA OSCFloatingView.initPosition/updatePosition/mouseDragged/mouseUp.
// Copyright (c) lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "ControlBarLayout.h"
#include <algorithm>
#include <cmath>
namespace iima::settings {
namespace {
bool Dimension(double value) noexcept { return std::isfinite(value) && value>0 && value<=1000000; }
bool Offset(double value) noexcept { return std::isfinite(value) && std::abs(value)<=1000000; }
std::optional<GeometryRect> Clamp(GeometrySize viewport, GeometrySize bar, double x,double y) noexcept {
    if(!Dimension(viewport.width) || !Dimension(viewport.height) || !Dimension(bar.width) || !Dimension(bar.height) ||
        !Offset(x) || !Offset(y)) return {};
    const double width=std::min(bar.width,viewport.width),height=std::min(bar.height,viewport.height);
    const double spareX=viewport.width-width,spareY=viewport.height-height;
    const double marginX=std::min(10.0,spareX/2),marginTop=std::min(25.0,spareY);
    return GeometryRect{std::clamp(x,marginX,spareX-marginX),std::clamp(y,marginTop,spareY),width,height};
}
}
std::optional<GeometryRect> ProjectFloatingControlBar(GeometrySize viewport,GeometrySize bar,
    FloatingControlBarPosition position) noexcept {
    if(!Dimension(viewport.width) || !Dimension(viewport.height) || !Dimension(bar.width) || !Dimension(bar.height) ||
        !std::isfinite(position.horizontal) || !std::isfinite(position.vertical) ||
        position.horizontal<0 || position.horizontal>1 || position.vertical<0 || position.vertical>1) return {};
    const double width=std::min(bar.width,viewport.width),height=std::min(bar.height,viewport.height);
    return Clamp(viewport,bar,viewport.width*position.horizontal-width/2,
        viewport.height*(1-position.vertical)-height);
}
std::optional<FloatingControlBarPosition> MoveFloatingControlBar(GeometrySize viewport,GeometrySize bar,
    GeometryRect current,GeometrySize delta,bool snapToCenter) noexcept {
    if(!Offset(current.x) || !Offset(current.y) || !Offset(delta.width) || !Offset(delta.height)) return {};
    auto moved=Clamp(viewport,bar,current.x+delta.width,current.y+delta.height);
    if(!moved) return {};
    const double centered=(viewport.width-moved->width)/2;
    if(snapToCenter && std::abs(moved->x-centered)<=5) moved->x=centered;
    return FloatingControlBarPosition{
        std::clamp((moved->x+moved->width/2)/viewport.width,0.0,1.0),
        std::clamp((viewport.height-moved->y-moved->height)/viewport.height,0.0,1.0)};
}
}

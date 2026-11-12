// IINA AppData.availableSpeedValues and MainWindowController arrow button actions.
// Copyright © 2016 lhc. GPL-3.0; references/iina/LICENSE.
#include "SpeedHoldPolicy.h"
#include <cmath>
namespace iima {
std::optional<SpeedHoldPlan> PlanSpeedHold(double observed,int direction) noexcept {
    if(!std::isfinite(observed) || observed<0.01 || observed>100 || (direction!=-1 && direction!=1)) return {};
    constexpr double speeds[]{0.03125,0.0625,0.125,0.25,0.5,1,2,4,8,16,32};
    // Switching direction first resets the ladder to its 1x centre, as upstream.
    const double base=(direction<0 && observed>1) || (direction>0 && observed<1) ? 1 : observed;
    if(direction>0) {
        for(double speed:speeds) if(speed>base) return SpeedHoldPlan{speed,speed==2};
    } else {
        for(int index=10;index>=0;--index) if(speeds[index]<base) return SpeedHoldPlan{speeds[index],speeds[index]==0.5};
    }
    return {}; // At/beyond a ladder endpoint: do not submit a redundant edit.
}
}

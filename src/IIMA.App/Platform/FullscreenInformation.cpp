// IINA AdditionalInfoView.update: localized short time and battery capacity.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "FullscreenInformation.h"
namespace iima {
FullscreenInformation ReadFullscreenInformation() {
    FullscreenInformation result;
    SYSTEMTIME now{}; GetLocalTime(&now);
    const auto size=GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&now,nullptr,nullptr,0);
    if (size>1 && size<1024) {
        std::wstring text(static_cast<std::size_t>(size),L'\0');
        if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&now,nullptr,text.data(),size)) {
            text.resize(static_cast<std::size_t>(size-1)); result.localTime=std::move(text);
        }
    }
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.BatteryFlag!=255 &&
        !(power.BatteryFlag&128) && power.BatteryLifePercent<=100) {
        result.batteryPercent=power.BatteryLifePercent;
        result.charging=(power.BatteryFlag&8)!=0;
    }
    return result;
}
}

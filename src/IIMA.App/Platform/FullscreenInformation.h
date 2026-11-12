// IINA AdditionalInfoView.update and PowerSource Windows counterpart.
// Copyright (c) 2026 lhc and contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <optional>
#include <string>
namespace iima {
struct FullscreenInformation {
    std::wstring localTime;
    std::optional<unsigned> batteryPercent;
    bool charging{};
};
// Read-only local OS information. A desktop without a battery or an unknown
// power status does not produce a fictitious 0%/100% battery indicator.
FullscreenInformation ReadFullscreenInformation();
}

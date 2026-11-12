// IINA DraggingDetect.openFromPasteboard quick lut3d, Copyright (c) lhc.
// GPL-3.0; references/iina/LICENSE. Reserved label ownership is shape-checked.
#pragma once
#include "FilterChain.h"
namespace iima {
enum class QuickLutState { Unknown, Absent, Disabled, Enabled, Conflict };
struct QuickLutInfo {
    QuickLutState state{QuickLutState::Unknown};
    std::size_t index{};
    std::string path;
};
QuickLutInfo FindQuickLutFilter(FilterChainSnapshot const&);
// nullopt removes; set appends or replaces exactly one recognizable owned LUT
// at its existing position and enables it. App validates the regular local file.
FilterEditPlan PlanQuickLut(FilterChainSnapshot const&, std::uint64_t expectedRevision,
    std::optional<std::string> const& path);
}

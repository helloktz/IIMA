// IINA savedFilters and EQProfile.userEQs persistence adapter. GPL-3.0-or-later.
#pragma once
#include "../../IIMA.Core/Filters/SavedFilter.h"
#include "../../IIMA.Core/Audio/Equalizations.h"
#include <winrt/Windows.Data.Json.h>
namespace iima {
std::vector<SavedFilter> ReadSavedFiltersJson(winrt::Windows::Data::Json::JsonArray const&);
winrt::Windows::Data::Json::JsonArray WriteSavedFiltersJson(std::vector<SavedFilter> const&);
UserEQProfiles::Profiles ReadEqualizersJson(winrt::Windows::Data::Json::JsonObject const&);
winrt::Windows::Data::Json::JsonObject WriteEqualizersJson(UserEQProfiles::Profiles const&);
}

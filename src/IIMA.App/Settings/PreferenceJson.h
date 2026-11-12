#pragma once
#include "../../IIMA.Core/Settings/PreferenceSchema.h"
#include <winrt/Windows.Data.Json.h>
namespace iima {
settings::PreferenceStore ReadPreferenceJson(winrt::Windows::Data::Json::JsonObject const& object,
    std::vector<settings::PreferenceIssue>& issues);
winrt::Windows::Data::Json::JsonObject WritePreferenceJson(settings::PreferenceValues const& values);
}

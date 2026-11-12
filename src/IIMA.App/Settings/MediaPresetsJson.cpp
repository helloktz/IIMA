// IINA SavedFilter.toDict and EQProfile.userEQs; copyright lhc and contributors.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "MediaPresetsJson.h"
#include <set>
#include <stdexcept>
namespace iima {
using namespace winrt;
using namespace Windows::Data::Json;
namespace {
void ValidateText(std::string const& text, std::size_t maximum, bool emptyAllowed) {
    if ((!emptyAllowed && text.empty()) || text.size() > maximum || text.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid saved media preset text");
}
void ValidateFilter(SavedFilter const& filter) {
    ValidateText(filter.name, 1024, false);
    ValidateText(filter.filterString, 65536, false);
    ValidateText(filter.shortcutKey, 128, true);
    if (!filter.Filter()) throw std::runtime_error("Invalid saved filter expression");
}
}
std::vector<SavedFilter> ReadSavedFiltersJson(JsonArray const& array) {
    if (array.Size() > 512) throw std::runtime_error("Too many saved filters");
    std::vector<SavedFilter> filters;
    std::set<std::string> names;
    for (auto const& value : array) {
        auto object = value.GetObject();
        MPVFilter::Parameters dictionary;
        for (auto key : {L"name", L"filterString", L"shortcutKey", L"shortcutKeyModifiers"})
            dictionary.emplace(to_string(key), to_string(object.GetNamedString(key)));
        auto filter = SavedFilter::FromDictionary(dictionary);
        if (!filter) throw std::runtime_error("Malformed saved filter");
        ValidateFilter(*filter);
        if (!names.insert(filter->name).second) throw std::runtime_error("Duplicate saved filter name");
        filters.push_back(std::move(*filter));
    }
    return filters;
}
JsonArray WriteSavedFiltersJson(std::vector<SavedFilter> const& filters) {
    if (filters.size() > 512) throw std::runtime_error("Too many saved filters");
    JsonArray array;
    std::set<std::string> names;
    for (auto const& filter : filters) {
        ValidateFilter(filter);
        if (!names.insert(filter.name).second) throw std::runtime_error("Duplicate saved filter name");
        JsonObject object;
        for (auto const& [key, value] : filter.ToDictionary())
            object.Insert(to_hstring(key), JsonValue::CreateStringValue(to_hstring(value)));
        array.Append(object);
    }
    return array;
}
UserEQProfiles::Profiles ReadEqualizersJson(JsonObject const& object) {
    if (object.Size() > 512) throw std::runtime_error("Too many equalizer profiles");
    UserEQProfiles::Profiles result;
    for (auto const& pair : object) {
        auto name = to_string(pair.Key()); ValidateText(name, 1024, false);
        auto array = pair.Value().GetArray();
        if (array.Size() != EQProfile::BandCount) throw std::runtime_error("Invalid equalizer band count");
        EQProfile profile;
        for (std::uint32_t index = 0; index < EQProfile::BandCount; ++index)
            profile.gains[index] = array.GetNumberAt(index);
        if (!profile.IsFinite()) throw std::runtime_error("Invalid equalizer gain");
        result.emplace(std::move(name), profile);
    }
    return result;
}
JsonObject WriteEqualizersJson(UserEQProfiles::Profiles const& profiles) {
    if (profiles.size() > 512) throw std::runtime_error("Too many equalizer profiles");
    JsonObject object;
    for (auto const& [name, profile] : profiles) {
        ValidateText(name, 1024, false);
        if (!profile.IsFinite()) throw std::runtime_error("Invalid equalizer gain");
        JsonArray bands;
        for (auto gain : profile.gains) bands.Append(JsonValue::CreateNumberValue(gain));
        object.Insert(to_hstring(name), bands);
    }
    return object;
}
}

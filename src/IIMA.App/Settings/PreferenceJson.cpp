#include "pch.h"
#include "PreferenceJson.h"
#include <cmath>
#include <limits>
#include <type_traits>
namespace iima {
using namespace winrt;
using namespace Windows::Data::Json;
settings::PreferenceStore ReadPreferenceJson(JsonObject const& object,
    std::vector<settings::PreferenceIssue>& issues) {
    settings::PreferenceStore result;
    for (auto const& pair : object) {
        auto key = to_string(pair.Key());
        auto descriptor = settings::FindPreference(key);
        if (!descriptor) { issues.push_back({key, "Unknown preference ignored"}); continue; }
        try {
            settings::PreferenceValue value;
            auto json = pair.Value();
            if (std::holds_alternative<bool>(descriptor->initial)) value = json.GetBoolean();
            else if (std::holds_alternative<std::string>(descriptor->initial)) value = to_string(json.GetString());
            else if (std::holds_alternative<double>(descriptor->initial)) value = json.GetNumber();
            else {
                auto number = json.GetNumber();
                // JSON numbers must be exactly representable integers; schema
                // ranges are tighter. No unchecked float-to-int conversion.
                if (!std::isfinite(number) || std::trunc(number) != number ||
                    number < -9007199254740991.0 || number > 9007199254740991.0)
                    throw hresult_error(E_INVALIDARG);
                value = static_cast<std::int64_t>(number);
            }
            std::string error;
            if (!result.Set(key, std::move(value), error)) issues.push_back({key, std::move(error)});
        } catch (hresult_error const&) { issues.push_back({key, "Stored type is invalid; default retained"}); }
    }
    return result;
}
JsonObject WritePreferenceJson(settings::PreferenceValues const& values) {
    JsonObject result;
    settings::PreferenceStore validated;
    auto errors = validated.Restore(values);
    if (!errors.empty()) throw hresult_error(E_INVALIDARG, L"Cannot save invalid advanced settings");
    for (auto const& [key, value] : validated.Overrides()) {
        auto json = std::visit([](auto const& current) -> IJsonValue {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, bool>) return JsonValue::CreateBooleanValue(current);
            else if constexpr (std::is_same_v<T, std::string>) return JsonValue::CreateStringValue(to_hstring(current));
            else return JsonValue::CreateNumberValue(static_cast<double>(current));
        }, value);
        result.Insert(to_hstring(key), json);
    }
    return result;
}
}

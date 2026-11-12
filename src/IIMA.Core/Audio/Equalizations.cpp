// Static port of IINA Equalizations.swift, PlayerCore.setAudioEq and SidebarAudioPane.
// Created upstream by Yuze Jiang, 2024/06/29.
// Copyright (c) 2024 lhc and IINA contributors.
// Source: references/iina-source.txt. License: references/iina/LICENSE (GNU GPL v3).
#include "Equalizations.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>
#include <utility>

namespace iima {
namespace {
constexpr std::array<int, EQProfile::BandCount> frequencies{
    32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
};

// Values are copied directly from the fixed upstream Equalizations.swift.
constexpr std::array<PresetEQProfile, 22> presets{{
    {"flat", {{0,0,0,0,0,0,0,0,0,0}}},
    {"acoustic", {{5.0,4.9,3.95,1.05,2.15,1.75,3.5,4.1,3.55,2.15}}},
    {"classical", {{4.75,3.75,3,2.5,-1.5,-1.5,0,2.25,3.25,3.75}}},
    {"dance", {{3.57,6.55,4.99,0,1.92,3.65,5.15,4.54,3.59,0}}},
    {"deep", {{4.95,3.55,1.75,1.00,2.85,2.50,1.45,-2.15,-3.55,-4.60}}},
    {"electronic", {{4.25,3.8,1.2,0,-2.15,2.25,0.85,1.25,3.95,4.8}}},
    {"hip_hop", {{5,4.25,1.5,3,-1,-1,1.5,-0.5,2,3}}},
    {"increase_bass", {{5.5,4.25,3.5,2.5,1.25,0,0,0,0,0}}},
    {"increase_treble", {{0,0,0,0,0,1.25,2.5,3.5,4.25,5.5}}},
    {"increase_vocal", {{-1.50,-3.00,-3.00,1.50,3.75,3.75,3.00,1.50,0,-1.50}}},
    {"jazz", {{4,3,1.5,2.25,-1.5,-1.5,0,1.5,3,3.75}}},
    {"latin", {{4.5,3,0,0,-1.5,-1.5,-1.5,0,3,4.5}}},
    {"loundness", {{6,4,0,0,-2,0,-1,-5,5,1}}},
    {"lounge", {{-3.00,-1.50,-0.50,1.50,4.00,2.50,0,-1.50,2.00,1.00}}},
    {"piano", {{3,2,0,2.5,3,1.5,3.5,4.5,3,3.5}}},
    {"pop", {{-1.5,-1,0,2,4,4,2,0,-1,-1.5}}},
    {"rnb", {{2.62,6.92,5.65,1.33,-2.19,-1.50,2.32,2.65,3.0,3.75}}},
    {"reduce_bass", {{-5.5,-4.25,-3.5,-2.5,-1.25,0,0,0,0,0}}},
    {"reduce_treble", {{0,0,0,0,0,-1.25,-2.5,-3.5,-4.25,-5.5}}},
    {"rock", {{5,4,3,1.5,-0.5,-1,0.5,2.5,3.5,4.5}}},
    {"small_speaker", {{5.5,4.25,3.5,2.5,1.25,0,-1.25,-2.5,-3.5,-4.25}}},
    {"spoken_word", {{-3.46,-0.47,0,0.69,3.46,4.61,4.84,4.28,2.54,0}}},
}};

std::string Number(double value) {
    // Shortest round-trip decimal, independent of the machine's decimal
    // separator. No user-controlled strings enter the lavfi graph.
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return result.ec == std::errc{} ? std::string(buffer.data(), result.ptr) : std::string{};
}

double ParseGain(std::string_view value) noexcept {
    if (value.starts_with('+')) {
        value.remove_prefix(1);
        if (value.starts_with('-')) return 0;
    }
    if (value.empty() || value.front() == '+') return 0;
    double gain{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), gain);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(gain))
        return 0;
    return gain;
}
} // namespace

std::optional<EQProfile> EQProfile::FromGains(std::span<const double> values) noexcept {
    if (values.size() != BandCount) return std::nullopt;
    EQProfile result;
    std::copy(values.begin(), values.end(), result.gains.begin());
    return result.IsFinite() ? std::optional(result) : std::nullopt;
}

bool EQProfile::IsFinite() const noexcept {
    return std::all_of(gains.begin(), gains.end(), [](double gain) { return std::isfinite(gain); });
}

std::string PresetEQProfile::LocalizationKey() const { return "eq.preset." + std::string(key); }
std::span<const PresetEQProfile> Equalizations::Presets() noexcept { return presets; }

const PresetEQProfile* Equalizations::FindPreset(std::string_view name) noexcept {
    constexpr std::string_view prefix = "eq.preset.";
    if (name.starts_with(prefix)) name.remove_prefix(prefix.size());
    const auto found = std::find_if(presets.begin(), presets.end(),
        [name](const auto& preset) { return preset.key == name; });
    return found == presets.end() ? nullptr : &*found;
}

std::span<const int, EQProfile::BandCount> Equalizations::Frequencies() noexcept { return frequencies; }

std::optional<std::string> Equalizations::FilterString(const EQProfile& profile) {
    if (!profile.IsFinite()) return std::nullopt;
    std::string graph = "@" + std::string(FilterLabel) + ":lavfi=[";
    for (std::size_t band = 0; band < frequencies.size(); ++band) {
        if (band != 0) graph += ',';
        graph += "equalizer=f=" + std::to_string(frequencies[band]);
        graph += ":t=h:width=" + Number(static_cast<double>(frequencies[band]) / 1.224744871);
        graph += ":g=" + Number(profile.gains[band]);
    }
    graph += ']';
    return graph;
}

std::optional<std::vector<std::string>> Equalizations::AddFilterCommand(const EQProfile& profile) {
    auto filter = FilterString(profile);
    if (!filter) return std::nullopt;
    return std::vector<std::string>{"af", "add", std::move(*filter)};
}

EQProfile Equalizations::SlidersFromFilter(std::optional<std::string_view> filter,
                                          const EQProfile& current) {
    if (!filter) return {};
    const auto begin = filter->find('[');
    if (begin == std::string_view::npos) return current;
    const auto end = filter->find(']', begin + 1);
    if (end == std::string_view::npos || end == begin + 1) return current;
    auto graph = filter->substr(begin + 1, end - begin - 1);
    auto result = current;
    std::size_t band = 0;
    while (!graph.empty() && band < EQProfile::BandCount) {
        const auto comma = graph.find(',');
        auto component = graph.substr(0, comma);
        if (!component.empty()) {
            // Swift split omits empty trailing components, including '='.
            while (component.ends_with('=')) component.remove_suffix(1);
            const auto equal = component.rfind('=');
            result.gains[band++] = ParseGain(equal == std::string_view::npos
                ? component : component.substr(equal + 1));
        }
        if (comma == std::string_view::npos) break;
        graph.remove_prefix(comma + 1);
    }
    return result;
}

const UserEQProfiles::Profiles& UserEQProfiles::Values() const noexcept { return profiles_; }

const EQProfile* UserEQProfiles::Find(std::string_view name) const noexcept {
    const auto found = profiles_.find(name);
    return found == profiles_.end() ? nullptr : &found->second;
}

UserEQProfiles::EditResult UserEQProfiles::ValidateName(std::string_view name,
    std::span<const std::string> builtinNames) const {
    if (name.empty()) return EditResult::EmptyName;
    if (profiles_.contains(name) || std::find(builtinNames.begin(), builtinNames.end(), name) != builtinNames.end())
        return EditResult::NameAlreadyExists;
    return EditResult::Applied;
}

UserEQProfiles::EditResult UserEQProfiles::Add(std::string name, EQProfile profile,
    std::span<const std::string> builtinNames) {
    const auto validation = ValidateName(name, builtinNames);
    if (validation != EditResult::Applied) return validation;
    if (!profile.IsFinite()) return EditResult::InvalidGains;
    profiles_.emplace(std::move(name), std::move(profile));
    return EditResult::Applied;
}

UserEQProfiles::EditResult UserEQProfiles::Rename(std::string_view oldName, std::string name,
    std::span<const std::string> builtinNames) {
    const auto validation = ValidateName(name, builtinNames);
    if (validation != EditResult::Applied) return validation;
    const auto old = profiles_.find(oldName);
    if (old == profiles_.end()) return EditResult::MissingProfile;
    // Insert before removing so allocation failure leaves the old entry intact.
    profiles_.emplace(std::move(name), old->second);
    profiles_.erase(old);
    return EditResult::Applied;
}

bool UserEQProfiles::Remove(std::string_view name) {
    const auto found = profiles_.find(name);
    if (found == profiles_.end()) return false;
    profiles_.erase(found);
    return true;
}

bool UserEQProfiles::Restore(Profiles profiles) {
    if (!std::all_of(profiles.begin(), profiles.end(), [](const auto& item) { return item.second.IsFinite(); }))
        return false;
    profiles_.swap(profiles);
    return true;
}

} // namespace iima

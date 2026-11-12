// Static port of IINA Equalizations.swift, PlayerCore.setAudioEq and SidebarAudioPane.
// Created upstream by Yuze Jiang, 2024/06/29.
// Copyright (c) 2024 lhc and IINA contributors.
// Source: references/iina-source.txt. License: references/iina/LICENSE (GNU GPL v3).
#pragma once

#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iima {

struct EQProfile {
    static constexpr std::size_t BandCount = 10;
    using Gains = std::array<double, BandCount>;

    // The Windows slider range follows SidebarAudioPane. Stored profiles are
    // not silently clamped, because IINA can load profiles outside this range.
    static constexpr double SliderMinimum = -12.0;
    static constexpr double SliderMaximum = 12.0;
    Gains gains{};

    static std::optional<EQProfile> FromGains(std::span<const double> values) noexcept;
    [[nodiscard]] bool IsFinite() const noexcept;
    friend bool operator==(const EQProfile&, const EQProfile&) = default;
};

struct PresetEQProfile {
    std::string_view key;
    EQProfile profile;
    [[nodiscard]] std::string LocalizationKey() const;
};

class Equalizations final {
public:
    // Source order and spelling are preserved, including upstream "loundness".
    static std::span<const PresetEQProfile> Presets() noexcept;
    static const PresetEQProfile* FindPreset(std::string_view nameOrLocalizationKey) noexcept;
    static std::span<const int, EQProfile::BandCount> Frequencies() noexcept;

    // Retain the IINA label to allow recognizing imported mpv filter settings.
    static constexpr std::string_view FilterLabel = "iina_aeq";
    static std::optional<std::string> FilterString(const EQProfile& profile);
    static std::optional<std::vector<std::string>> AddFilterCommand(const EQProfile& profile);

    // Port of EqualizerView.update: values are taken from the last '=' of each
    // comma-separated component inside the first [...] graph. Invalid numbers
    // map to zero. Short graphs leave remaining slider values unchanged.
    // Absence of a filter clears sliders; malformed brackets leave them alone.
    static EQProfile SlidersFromFilter(std::optional<std::string_view> filter,
                                       const EQProfile& current = {});
};

// Persistence-independent equivalent of EQProfile.userEQs. Windows storage
// supplies/restores this map; the source UserDefaults side effect is not hidden
// in this model. Names are compared exactly, including case and whitespace.
class UserEQProfiles final {
public:
    using Profiles = std::map<std::string, EQProfile, std::less<>>;
    enum class EditResult { Applied, EmptyName, NameAlreadyExists, MissingProfile, InvalidGains };

    [[nodiscard]] const Profiles& Values() const noexcept;
    [[nodiscard]] const EQProfile* Find(std::string_view name) const noexcept;
    EditResult Add(std::string name, EQProfile profile,
                   std::span<const std::string> localizedBuiltinNames = {});
    EditResult Rename(std::string_view oldName, std::string name,
                      std::span<const std::string> localizedBuiltinNames = {});
    bool Remove(std::string_view name);
    // Corresponds to successful dictionary decoding. Invalid profiles reject
    // the entire replacement rather than partly overwriting existing state.
    bool Restore(Profiles profiles);

private:
    EditResult ValidateName(std::string_view name,
                            std::span<const std::string> localizedBuiltinNames) const;
    Profiles profiles_;
};

} // namespace iima

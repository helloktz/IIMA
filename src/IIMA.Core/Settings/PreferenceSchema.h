#pragma once
// Adapted from IINA Preference.swift and MPVController.swift.
// Copyright © 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace iima::settings {
using PreferenceValue = std::variant<bool, std::int64_t, double, std::string>;
using PreferenceValues = std::map<std::string, PreferenceValue, std::less<>>;
enum class PreferenceArea { Video, Audio, Subtitles, Window, Interface, Network };
enum class ApplyTiming { Application, Immediate, NextMedia, NextPlayer };
struct EnumChoice {
    std::int64_t value;
    std::string_view token; // mpv spelling, not translated display text.
};
struct PreferenceDescriptor {
    std::string_view key;
    PreferenceArea area;
    PreferenceValue initial;
    ApplyTiming timing{ApplyTiming::Application};
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::span<EnumChoice const> choices;
    std::string_view mpvProperty;
};
struct PreferenceIssue { std::string key; std::string message; };
// Descriptor storage and all string_view/span members live for the process lifetime.
std::span<PreferenceDescriptor const> PreferenceSchema();
PreferenceDescriptor const* FindPreference(std::string_view key);
// Keys wired through PlayerSession::ApplyAdvancedPreferences. Application-only
// descriptors are intentionally absent until the host connects their behavior.
std::span<std::string_view const> SessionPreferenceKeys();
std::optional<std::string> ValidatePreference(PreferenceDescriptor const&, PreferenceValue const&);

// Strong typing deliberately does not emulate UserDefaults coercion of arbitrary
// strings to numbers/bools. Bad stored values fall back independently to defaults.
class PreferenceStore {
public:
    PreferenceValue const& Get(std::string_view key) const; // throws for unknown key
    bool Set(std::string_view key, PreferenceValue value, std::string& error);
    void Reset(std::string_view key);
    void ResetAll();
    std::vector<PreferenceIssue> Restore(PreferenceValues const& persisted);
    PreferenceValues const& Overrides() const noexcept { return overrides_; }
    bool Boolean(std::string_view key) const;
    std::int64_t Integer(std::string_view key) const;
    double Number(std::string_view key) const;
    std::string const& String(std::string_view key) const;
private:
    PreferenceValues overrides_;
};
// MPVController.mpvInit initialVolume vs softVolume. Evaluate once per new
// player, not once per media. Result is a request, not observed mpv state.
// Invalid persisted scalars are handled defensively; never exceed maxVolume.
double InitialPlayerVolume(PreferenceStore const&, double rememberedVolume, double maxVolume);
struct PreferenceOption {
    std::string sourceKey;
    std::string property;
    std::string value;
    ApplyTiming timing;
};
// Only declared mappings are emitted. No arbitrary userOptions or shell commands.
// Result describes desired settings, not successful application to libmpv.
std::vector<PreferenceOption> MpvPreferenceOptions(PreferenceStore const&);
std::vector<PreferenceOption> ChangedMpvPreferences(PreferenceStore const& before,
                                                   PreferenceStore const& after);
}

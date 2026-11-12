// Information rules adapted from IINA InspectorWindowController.swift and
// PlayerCore.getMusicMetadata. GPL-3.0, references/iina/LICENSE.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace iima {
using MediaProperties = std::map<std::string, std::string, std::less<>>;
struct AudioDevice {
    std::string name;
    std::string description;
    bool operator==(AudioDevice const&) const = default;
};
std::string MetadataValue(MediaProperties const& values, std::string_view key);
// Local UI title only; system integration uses stricter path/URL privacy rules.
std::string MediaDisplayTitle(std::string_view mediaTitle, std::string_view path);
std::string FormatBitrate(std::optional<double> bitsPerSecond);
std::string FormatByteCount(std::optional<double> bytes);
struct InformationField {
    std::string label;
    std::string value;
};
// Display a bounded, already-owned property map. Unknown values remain unknown;
// no decoder capabilities or bitrate are inferred from filename extensions.
std::vector<InformationField> DescribeVideoParameters(MediaProperties const& parameters);
std::vector<InformationField> DescribeAudioParameters(MediaProperties const& parameters);
}

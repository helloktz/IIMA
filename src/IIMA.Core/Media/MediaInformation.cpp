// IINA source mapping: InspectorWindowController GeneralVC/FileVC and
// PlayerCore.getMusicMetadata. Copyright © lhc and contributors, GPL-3.0.
#include "MediaInformation.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
namespace iima {
namespace {
std::string Lower(std::string_view value) {
    std::string result(value);
    for (auto& ch : result) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return result;
}
std::string Decimal(double value, int digits) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(digits) << value; return out.str();
}
std::vector<InformationField> Select(MediaProperties const& values,
    std::initializer_list<std::pair<std::string_view,std::string_view>> fields) {
    std::vector<InformationField> result;
    for (auto [key,label] : fields) {
        auto found = values.find(key);
        result.push_back({std::string(label), found == values.end() ? "—" : found->second});
    }
    return result;
}
}
std::string MetadataValue(MediaProperties const& values, std::string_view key) {
    auto match = Lower(key);
    // Prefer the exact spelling; otherwise stable lexicographic first match.
    if (auto found = values.find(key); found != values.end()) return found->second;
    for (auto const& [name,value] : values) if (Lower(name) == match) return value;
    return {};
}
std::string MediaDisplayTitle(std::string_view mediaTitle, std::string_view path) {
    if (!mediaTitle.empty()) return std::string(mediaTitle);
    auto slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
    return path.empty() ? "IIMA" : std::string(path);
}
std::string FormatBitrate(std::optional<double> value) {
    if (!value || !std::isfinite(*value) || *value < 0) return "—";
    if (*value >= 1000000) return Decimal(*value / 1000000, 2) + " Mbps";
    return Decimal(*value / 1000, 1) + " kbps";
}
std::string FormatByteCount(std::optional<double> value) {
    if (!value || !std::isfinite(*value) || *value < 0) return "—";
    if (*value >= 1073741824) return Decimal(*value / 1073741824, 2) + " GiB";
    if (*value >= 1048576) return Decimal(*value / 1048576, 2) + " MiB";
    if (*value >= 1024) return Decimal(*value / 1024, 2) + " KiB";
    return Decimal(*value, 0) + " B";
}
std::vector<InformationField> DescribeVideoParameters(MediaProperties const& p) {
    return Select(p, {{"w","Width"},{"h","Height"},{"dw","Display width"},{"dh","Display height"},
        {"pixelformat","Pixel format"},{"hw-pixelformat","Hardware format"},{"aspect","Aspect"},
        {"rotate","Rotation"},{"primaries","Primaries"},{"gamma","Transfer"},
        {"colormatrix","Matrix"},{"colorlevels","Range"},{"sig-peak","Signal peak"}});
}
std::vector<InformationField> DescribeAudioParameters(MediaProperties const& p) {
    return Select(p, {{"format","Sample format"},{"samplerate","Sample rate"},
        {"channels","Channels"},{"channel-count","Channel count"},{"hr-channels","Layout"}});
}
}

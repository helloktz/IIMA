// Adapted from IINA SettingsPageKeyBindings.ConfigEditor, KeyMapping and
// PlayerCore.setKeyBindings. Copyright (c) 2016-2026 lhc; GPL-3.0.
// See references/iina-source.txt and references/iina/LICENSE.
#include "BindingConfiguration.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace iima {
namespace {
BindingEditResult Failure(BindingEditError error, std::string message) {
    return {error, std::move(message)};
}
bool HasBreak(std::string_view text) {
    return text.find_first_of("\r\n") != std::string_view::npos ||
           text.find('\0') != std::string_view::npos;
}
std::string FoldAscii(std::string_view text) {
    std::string result(text);
    for (auto& ch : result) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return result;
}
bool Blank(std::string_view value) {
    return value.find_first_not_of(" \t") == std::string_view::npos;
}
std::vector<std::size_t> UniqueRows(std::span<const std::size_t> rows) {
    std::vector<std::size_t> result(rows.begin(), rows.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
bool IsDefaultMarker(const KeyMapping& mapping) {
    return mapping.RawKey() == "default-bindings" && mapping.RawAction() == "start";
}
std::optional<KeyMapping> ActiveMapping(const KeyMapping& mapping) {
    if (IsDefaultMarker(mapping)) return std::nullopt;
    if (auto section = mapping.Section()) {
        if (*section != "default") return std::nullopt;
        // Avoid splitting/rejoining quoted command arguments here. Remove the
        // original section prefix only; retain the remaining command bytes.
        const auto close = mapping.RawAction().find('}');
        auto action = std::string_view(mapping.RawAction()).substr(close + 1);
        const auto start = action.find_first_not_of(" \t");
        action = start == std::string_view::npos ? std::string_view{} : action.substr(start);
        return KeyMapping(mapping.RawKey(), std::string(action),
                          mapping.IsIinaCommand(), mapping.Comment());
    }
    return mapping;
}
}

BindingConfiguration::BindingConfiguration(std::string name, std::vector<KeyMapping> mappings,
                                           bool readOnly)
    : name_(std::move(name)), mappings_(std::move(mappings)), readOnly_(readOnly) {}
const std::string& BindingConfiguration::Name() const noexcept { return name_; }
const std::vector<KeyMapping>& BindingConfiguration::Mappings() const noexcept { return mappings_; }
bool BindingConfiguration::ReadOnly() const noexcept { return readOnly_; }
bool BindingConfiguration::Dirty() const noexcept { return dirty_; }
std::uint64_t BindingConfiguration::Revision() const noexcept { return revision_; }
void BindingConfiguration::Changed() noexcept {
    dirty_ = true;
    ++revision_;
    if (!revision_) ++revision_;
}
BindingEditResult BindingConfiguration::ValidateMapping(const KeyMapping& mapping) {
    const auto& key = mapping.RawKey();
    const auto& action = mapping.RawAction();
    if (key.empty()) return Failure(BindingEditError::EmptyKey, "A key is required");
    if (Blank(action)) return Failure(BindingEditError::EmptyAction, "An action is required");
    if (HasBreak(key) || HasBreak(action) || (mapping.Comment() && HasBreak(*mapping.Comment())))
        return Failure(BindingEditError::LineBreak, "A binding must occupy one input.conf line");
    if (key.size() > MaximumFieldBytes || action.size() > MaximumFieldBytes ||
        (mapping.Comment() && mapping.Comment()->size() > MaximumFieldBytes))
        return Failure(BindingEditError::TooLarge, "The binding field is too large");
    auto escaped = EscapeReservedMpvKey(key);
    if (escaped.empty() || escaped.find_first_of(" \t#") != std::string::npos)
        return Failure(BindingEditError::InvalidKey, "Use SPACE or SHARP for reserved keys");
    // Hash is a comment delimiter in the upstream parser, even inside quotes.
    // Refuse edits that would silently lose part of an action on reload.
    if (action.find('#') != std::string::npos)
        return Failure(BindingEditError::InvalidKey, "A literal # cannot round-trip in an input.conf action");
    return {};
}
BindingEditResult BindingConfiguration::Insert(std::size_t before, KeyMapping mapping) {
    if (readOnly_) return Failure(BindingEditError::ReadOnly, "Duplicate this bundled set before editing");
    if (before > mappings_.size()) return Failure(BindingEditError::InvalidIndex, "Invalid insertion position");
    if (mappings_.size() >= MaximumMappings) return Failure(BindingEditError::TooLarge, "Too many bindings");
    if (auto valid = ValidateMapping(mapping); !valid) return valid;
    mapping.SetRawKey(EscapeReservedMpvKey(mapping.RawKey()));
    mappings_.insert(mappings_.begin() + static_cast<std::ptrdiff_t>(before), std::move(mapping));
    Changed();
    return {};
}
BindingEditResult BindingConfiguration::Replace(std::size_t row, KeyMapping mapping) {
    if (readOnly_) return Failure(BindingEditError::ReadOnly, "Duplicate this bundled set before editing");
    if (row >= mappings_.size()) return Failure(BindingEditError::InvalidIndex, "Invalid binding row");
    if (auto valid = ValidateMapping(mapping); !valid) return valid;
    mapping.SetRawKey(EscapeReservedMpvKey(mapping.RawKey()));
    mappings_[row] = std::move(mapping);
    Changed();
    return {};
}
BindingEditResult BindingConfiguration::Remove(std::span<const std::size_t> rows) {
    if (readOnly_) return Failure(BindingEditError::ReadOnly, "Bundled sets are read-only");
    auto selected = UniqueRows(rows);
    if (selected.empty()) return {};
    if (selected.back() >= mappings_.size()) return Failure(BindingEditError::InvalidIndex, "Invalid binding row");
    for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        mappings_.erase(mappings_.begin() + static_cast<std::ptrdiff_t>(*it));
    Changed();
    return {};
}
BindingEditResult BindingConfiguration::Move(std::span<const std::size_t> rows, std::size_t destination) {
    if (readOnly_) return Failure(BindingEditError::ReadOnly, "Bundled sets are read-only");
    auto selected = UniqueRows(rows);
    if (destination > mappings_.size() || (!selected.empty() && selected.back() >= mappings_.size()))
        return Failure(BindingEditError::InvalidIndex, "Invalid move position");
    if (selected.empty()) return {};
    std::vector<KeyMapping> moving;
    moving.reserve(selected.size());
    for (auto row : selected) moving.push_back(mappings_[row]);
    const auto removedBefore = std::lower_bound(selected.begin(), selected.end(), destination) - selected.begin();
    auto next = mappings_;
    for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        next.erase(next.begin() + static_cast<std::ptrdiff_t>(*it));
    next.insert(next.begin() + static_cast<std::ptrdiff_t>(destination) - removedBefore,
                moving.begin(), moving.end());
    mappings_ = std::move(next);
    Changed();
    return {};
}
std::vector<std::size_t> BindingConfiguration::Search(std::string_view query) const {
    std::vector<std::size_t> result;
    auto folded = FoldAscii(query);
    for (std::size_t row = 0; row < mappings_.size(); ++row) {
        const auto& value = mappings_[row];
        if (folded.empty() || FoldAscii(value.RawKey()).find(folded) != std::string::npos ||
            FoldAscii(value.ReadableAction()).find(folded) != std::string::npos ||
            (value.Comment() && FoldAscii(*value.Comment()).find(folded) != std::string::npos)) result.push_back(row);
    }
    return result;
}
std::vector<BindingConflict> BindingConfiguration::Conflicts() const {
    std::map<std::string, std::vector<std::size_t>> rows;
    for (std::size_t row = 0; row < mappings_.size(); ++row)
        if (auto value = ActiveMapping(mappings_[row])) rows[value->NormalizedMpvKey()].push_back(row);
    std::vector<BindingConflict> result;
    for (auto& [key, indices] : rows)
        if (indices.size() > 1) result.push_back({key, indices, indices.back()});
    return result;
}
KeyBindingSet BindingConfiguration::ActiveBindings() const { return KeyBindingSet(mappings_); }
std::string BindingConfiguration::Serialize() const { return GenerateInputConf(mappings_); }
bool BindingConfiguration::MarkSaved(std::uint64_t savedRevision) noexcept {
    if (savedRevision != revision_) return false;
    dirty_ = false;
    return true;
}
BindingConfiguration BindingConfiguration::Duplicate(std::string newName) const {
    BindingConfiguration result(std::move(newName), mappings_, false);
    result.dirty_ = true;
    return result;
}

BindingConfigurationCatalog::BindingConfigurationCatalog(std::vector<BindingConfigurationEntry> entries,
                                                         std::string fallbackName)
    : entries_(std::move(entries)), fallbackName_(std::move(fallbackName)) {
    // Bundled order is supplied by the resource adapter; user names follow in
    // deterministic ordinal order instead of Foundation localized sorting.
    std::stable_sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
        if (a.bundled != b.bundled) return a.bundled;
        return !a.bundled && a.name < b.name;
    });
}
const std::vector<BindingConfigurationEntry>& BindingConfigurationCatalog::Entries() const noexcept { return entries_; }
const BindingConfigurationEntry* BindingConfigurationCatalog::Find(std::string_view name) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e.name == name; });
    return found == entries_.end() ? nullptr : &*found;
}
BindingEditResult BindingConfigurationCatalog::ValidateNewName(std::string_view name) const {
    if (Blank(name) || name.size() > 120 || name.front() == ' ' || name.back() == ' ' || name.back() == '.' ||
        name.find_first_of("/\\:*?\"<>|\r\n") != std::string_view::npos || name.find('\0') != std::string_view::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32; }))
        return Failure(BindingEditError::InvalidName, "Choose a Windows-compatible configuration name");
    auto stem = FoldAscii(name.substr(0, name.find('.')));
    static constexpr std::string_view reserved[] = {"con", "prn", "aux", "nul", "conin$", "conout$"};
    for (auto item : reserved) if (stem == item) return Failure(BindingEditError::InvalidName, "Reserved device name");
    if (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9')
        return Failure(BindingEditError::InvalidName, "Reserved device name");
    const auto folded = FoldAscii(name);
    for (const auto& entry : entries_)
        if (FoldAscii(entry.name) == folded) return Failure(BindingEditError::DuplicateName, "Configuration already exists");
    // The platform must also check actual filesystem collisions (including
    // Windows Unicode case folding) when atomically creating the file.
    return {};
}
BindingSelection BindingConfigurationCatalog::Select(std::string_view requested,
    const std::function<InputConfResult(const std::filesystem::path&)>& loader) const {
    BindingSelection result;
    if (!loader) {
        result.result = Failure(BindingEditError::UnreadableConfiguration, "No configuration loader");
        return result;
    }
    auto tryLoad = [&](std::string_view name) {
        const auto entry = Find(name);
        if (!entry) return false;
        auto parsed = loader(entry->path);
        if (!parsed || parsed.mappings.size() > BindingConfiguration::MaximumMappings) return false;
        result.issues = std::move(parsed.issues);
        result.configuration.emplace(entry->name, std::move(parsed.mappings), entry->bundled);
        return true;
    };
    if (tryLoad(requested)) return result;
    result.usedFallback = requested != fallbackName_;
    if (result.usedFallback && tryLoad(fallbackName_)) return result;
    result.result = Failure(BindingEditError::UnreadableConfiguration, "Selected and fallback binding sets are unavailable");
    return result;
}

BindingAction ClassifyBindingAction(const KeyMapping& mapping) {
    auto active = ActiveMapping(mapping);
    if (!active) return {BindingActionKind::InactiveSection, {}, {}, "Not an active default-section binding"};
    if (active->RawAction().empty() || HasBreak(active->RawAction()))
        return {BindingActionKind::Invalid, {}, {}, "Empty or multiline command"};
    if (active->IsIinaCommand()) {
        const auto command = ApplicationCommandForBinding(*active);
        if (!command) return {BindingActionKind::Invalid, {}, {}, "Unknown IINA command"};
        return {BindingActionKind::Application, command, {}, {}};
    }
    // Exact comparison prevents an action such as 'quit; run ...' from being
    // partly consumed as a safe application command. mpv parsing stays in mpv.
    if (active->RawAction() == "ignore") return {BindingActionKind::Ignore, {}, {}, {}};
    if (active->RawAction() == "ab-loop") return {BindingActionKind::AbLoop, {}, {}, {}};
    if (active->RawAction() == "quit") return {BindingActionKind::Quit, {}, {}, {}};
    if (active->RawAction() == "screenshot") return {BindingActionKind::Screenshot, {}, {}, {}};
    const auto& raw = active->RawAction();
    if (raw.find_first_of(";\\\"'`$#{}") != std::string::npos)
        return {BindingActionKind::Unsupported, {}, {}, "Command chains, expansion and quoting are not supported"};
    std::vector<std::string> tokens;
    for (std::size_t at = 0; at < raw.size();) {
        const auto start = raw.find_first_not_of(" \t", at);
        if (start == std::string::npos) break;
        const auto end = raw.find_first_of(" \t", start);
        tokens.emplace_back(raw.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        at = end;
    }
    if (tokens.empty() || tokens.size() > 5)
        return {BindingActionKind::Unsupported, {}, {}, "Unsupported command arguments"};
    const auto& command = tokens.front();
    const bool supported = command == "seek" || command == "cycle" || command == "set" ||
        command == "add" || command == "frame-step" || command == "frame-back-step" ||
        command == "playlist-next" || command == "playlist-prev" || command == "stop" ||
        command == "screenshot" || command == "sub-step" || command == "sub-seek";
    if (!supported)
        return {BindingActionKind::Unsupported, {}, {}, "Command is not supported by the typed keyboard dispatcher"};
    if (command == "cycle" || command == "set" || command == "add") {
        if (tokens.size() < 2)
            return {BindingActionKind::Invalid, {}, {}, "Missing property name"};
        const auto& property = tokens[1];
        const bool allowed = property == "pause" || property == "mute" || property == "sub-visibility" ||
            property == "volume" || property == "speed" || property == "fullscreen" ||
            (command == "cycle" && (property == "video" || property == "vid" ||
                property == "audio" || property == "aid" || property == "sub" || property == "sid")) ||
            (command == "add" && property == "chapter");
        if (!allowed)
            return {BindingActionKind::Unsupported, {}, {}, "Property is not supported by the typed keyboard dispatcher"};
    }
    return {BindingActionKind::MpvCommandTokens, {}, {}, {}, std::move(tokens)};
}
} // namespace iima

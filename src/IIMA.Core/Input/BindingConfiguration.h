// Adapted from IINA SettingsPageKeyBindings.ConfigEditor, KeyMapping and
// PlayerCore.setKeyBindings. Copyright (c) 2016-2026 lhc; GPL-3.0.
// See references/iina-source.txt and references/iina/LICENSE.
#pragma once
#include "KeyMapping.h"
#include "../Commands/ApplicationCommand.h"
#include <cstdint>
#include <functional>

namespace iima {

enum class BindingEditError {
    None, ReadOnly, InvalidIndex, EmptyKey, EmptyAction, InvalidKey,
    LineBreak, TooLarge, DuplicateName, InvalidName, MissingConfiguration,
    UnreadableConfiguration, StaleRevision
};
struct BindingEditResult {
    BindingEditError error = BindingEditError::None;
    std::string message;
    explicit operator bool() const noexcept { return error == BindingEditError::None; }
};
struct BindingConflict {
    std::string normalizedKey;
    std::vector<std::size_t> rows;
    std::size_t winningRow = 0;
};

// In-memory editor, not a file store. Edits are explicit and do not write files.
// UI filtering retains original row indices; saving always serializes all rows.
// Bundled sets cannot be changed; duplicate one first, as IINA requires.
class BindingConfiguration final {
public:
    static constexpr std::size_t MaximumMappings = 10000;
    static constexpr std::size_t MaximumFieldBytes = 16384;
    BindingConfiguration(std::string name, std::vector<KeyMapping> mappings,
                         bool readOnly = false);
    const std::string& Name() const noexcept;
    const std::vector<KeyMapping>& Mappings() const noexcept;
    bool ReadOnly() const noexcept;
    bool Dirty() const noexcept;
    std::uint64_t Revision() const noexcept;
    BindingEditResult Insert(std::size_t before, KeyMapping mapping);
    BindingEditResult Replace(std::size_t row, KeyMapping mapping);
    BindingEditResult Remove(std::span<const std::size_t> rows);
    // destination is an insertion boundary in the ORIGINAL list, including end.
    // Relative order of the moved rows is retained regardless of selection order.
    BindingEditResult Move(std::span<const std::size_t> rows, std::size_t destination);
    std::vector<std::size_t> Search(std::string_view query) const;
    std::vector<BindingConflict> Conflicts() const;
    KeyBindingSet ActiveBindings() const;
    std::string Serialize() const;
    // Only after the platform atomic write succeeds. A late write completion
    // must not clear Dirty for newer edits. Return false when revision changed.
    bool MarkSaved(std::uint64_t savedRevision) noexcept;
    BindingConfiguration Duplicate(std::string newName) const;
    static BindingEditResult ValidateMapping(const KeyMapping& mapping);
private:
    void Changed() noexcept;
    std::string name_;
    std::vector<KeyMapping> mappings_;
    bool readOnly_ = false;
    bool dirty_ = false;
    std::uint64_t revision_ = 1;
};

struct BindingConfigurationEntry {
    std::string name;
    std::filesystem::path path;
    bool bundled = false;
};
struct BindingSelection {
    std::optional<BindingConfiguration> configuration;
    std::vector<InputConfIssue> issues;
    bool usedFallback = false;
    BindingEditResult result;
};
// Catalog is built by the Windows directory/resource adapter; this class does
// not enumerate a filesystem or follow a selected path itself.
class BindingConfigurationCatalog final {
public:
    explicit BindingConfigurationCatalog(std::vector<BindingConfigurationEntry> entries,
                                         std::string fallbackName);
    const std::vector<BindingConfigurationEntry>& Entries() const noexcept;
    const BindingConfigurationEntry* Find(std::string_view name) const;
    BindingEditResult ValidateNewName(std::string_view name) const;
    // Loader is supplied by platform storage. Fallback is attempted at most
    // once; an unreadable fallback returns an error rather than recursing.
    BindingSelection Select(std::string_view requested,
        const std::function<InputConfResult(const std::filesystem::path&)>& loader) const;
private:
    std::vector<BindingConfigurationEntry> entries_;
    std::string fallbackName_;
};

enum class BindingActionKind {
    Invalid, InactiveSection, Ignore, Application, AbLoop, Quit, Screenshot,
    MpvCommandTokens, Unsupported
};
struct BindingAction {
    BindingActionKind kind = BindingActionKind::Invalid;
    std::optional<ApplicationCommand> application;
    std::string rawCommand;
    std::string reason;
    std::vector<std::string> tokens;
};
// Classification never executes a command or passes an arbitrary command string.
// Only a deliberately small player-control vocabulary with unquoted tokens is
// accepted. The application must validate argument counts/ranges and dispatch
// these tokens to typed session methods. Unsupported commands stay editable and
// serializable but cannot execute through this dispatcher.
BindingAction ClassifyBindingAction(const KeyMapping& mapping);

} // namespace iima

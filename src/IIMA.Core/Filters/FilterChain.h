// Derived from IINA FilterWindowController.swift and MPVController.swift,
// copyright (c) 2016 lhc. GNU GPL v3; see references/iina/LICENSE.
#pragma once
#include "MPVFilter.h"
#include "../Mpv/include/mpv/client.h"
#include <cstdint>
#include <memory>

namespace iima {
// Owned protocol tree: unknown fields and duplicate map keys are not silently
// discarded when the user removes or reorders another, known filter.
struct FilterNode {
    mpv_format format{MPV_FORMAT_NONE};
    std::string text;
    int flag{};
    std::int64_t integer{};
    double number{};
    std::vector<unsigned char> bytes;
    std::vector<std::string> keys;
    std::vector<FilterNode> children;
    bool operator==(FilterNode const&) const = default;
};
struct FilterChainEntry {
    std::size_t index{};
    std::optional<MPVFilter> filter;
    bool enabled{true};
    std::string display;
    std::string error;
};
struct FilterChainSnapshot {
    std::uint64_t revision{};
    FilterNode root;
    std::vector<FilterChainEntry> entries;
    bool complete{};
    std::string error;
};
// Call before next mpv event. Limits: 4096 rows, 32768 nodes, 1 MiB total
// string/byte payload, depth 8. A partial copy is never eligible for writeback.
FilterChainSnapshot ParseFilterChain(mpv_node const&, std::uint64_t revision = 0);
struct FilterEditPlan {
    FilterNode before;
    FilterNode after;
    bool valid{};
    std::string error;
    // Call against a newly retrieved list immediately before writing. mpv has
    // no compare-and-swap API: external scripts can still race the final write.
    // Arrays are ordered; maps are compared by key, ignoring enumeration order.
    // Duplicate keys are ambiguous and conservatively prevent all editing.
    bool FreshMatches(FilterChainSnapshot const&) const;
};
// IINA PlayerCore.setFlip/setMirror and reloadSavedIINAfilters, using the same
// reserved labels. A custom/duplicate label must never be silently overwritten.
enum class OrientationFilterState { Unknown, Absent, Disabled, Enabled, Conflict };
struct OrientationFilter {
    OrientationFilterState state{OrientationFilterState::Unknown};
    std::size_t index{};
};
OrientationFilter FindOrientationFilter(FilterChainSnapshot const&, bool mirror);
class FilterChainEditor final {
public:
    // Create one structured filter only against a complete, empty observed chain.
    // Append only a structured node, preserving all existing unknown entries.
    // Final chain is revalidated against observation resource bounds.
    static FilterEditPlan AppendStructured(FilterChainSnapshot const&, std::uint64_t expectedRevision, MPVFilter const&);
    static FilterEditPlan CreateSingle(FilterChainSnapshot const&, MPVFilter const&);
    static FilterEditPlan Remove(FilterChainSnapshot const&, std::uint64_t expectedRevision,
                                 std::size_t index);
    // Destination is the desired final index, not a pre-removal insertion slot.
    static FilterEditPlan Move(FilterChainSnapshot const&, std::uint64_t expectedRevision,
                               std::size_t index, std::size_t destination);
    static FilterEditPlan SetEnabled(FilterChainSnapshot const&, std::uint64_t expectedRevision,
                                     std::size_t index, bool enabled);
    // A raw-parameter-only MPVFilter cannot be safely reconstructed as a node
    // map; this operation rejects it. The existing typed preset forms work.
    static FilterEditPlan Replace(FilterChainSnapshot const&, std::uint64_t expectedRevision,
                                  std::size_t index, MPVFilter const& replacement);
};
// Stable backing storage for a synchronous property set on the mpv worker.
// mpv_set_property copies the tree; it must not retain this object's pointer.
// Never pass this tree to mpv_free_node_contents (the C++ owner frees it).
class FilterNodeView final {
public:
    explicit FilterNodeView(FilterNode const&);
    ~FilterNodeView();
    FilterNodeView(FilterNodeView const&) = delete;
    FilterNodeView& operator=(FilterNodeView const&) = delete;
    mpv_node const& Get() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}

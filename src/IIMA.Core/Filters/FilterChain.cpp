// Derived from IINA FilterWindowController.swift / MPVController.removeFilter.
// Copyright (c) 2016 lhc; GNU GPL v3. See references/iina/LICENSE.
#include "FilterChain.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace iima {
namespace {
constexpr std::size_t MaxPayload = 1024 * 1024;
bool ValidName(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
struct Budget { std::size_t payload{}, nodes{}; };
bool CopyText(char const* input, std::string& output, Budget& budget) {
    if (!input) return false;
    auto available = MaxPayload - budget.payload;
    std::size_t size{};
    while (size <= available && input[size]) ++size;
    if (size > available) return false;
    budget.payload += size;
    output.assign(input, size);
    return true;
}
bool CopyNode(mpv_node const& input, FilterNode& output, Budget& budget, unsigned depth) {
    if (depth > 8 || ++budget.nodes > 32768) return false;
    output.format = input.format;
    switch (input.format) {
    case MPV_FORMAT_NONE: return true;
    case MPV_FORMAT_STRING: return CopyText(input.u.string, output.text, budget);
    case MPV_FORMAT_FLAG: output.flag = input.u.flag; return true;
    case MPV_FORMAT_INT64: output.integer = input.u.int64; return true;
    case MPV_FORMAT_DOUBLE:
        output.number = input.u.double_;
        return std::isfinite(output.number);
    case MPV_FORMAT_BYTE_ARRAY:
        if (!input.u.ba || input.u.ba->size > MaxPayload - budget.payload ||
            (input.u.ba->size && !input.u.ba->data)) return false;
        budget.payload += input.u.ba->size;
        if (input.u.ba->size) {
            auto data = static_cast<unsigned char const*>(input.u.ba->data);
            output.bytes.assign(data, data + input.u.ba->size);
        }
        return true;
    case MPV_FORMAT_NODE_ARRAY:
    case MPV_FORMAT_NODE_MAP: {
        auto list = input.u.list;
        if (!list || list->num < 0 || list->num > 32768 ||
            (list->num && !list->values) ||
            (input.format == MPV_FORMAT_NODE_MAP && list->num && !list->keys)) return false;
        output.children.resize(static_cast<std::size_t>(list->num));
        if (input.format == MPV_FORMAT_NODE_MAP) output.keys.resize(output.children.size());
        for (std::size_t i = 0; i < output.children.size(); ++i) {
            if (!output.keys.empty() && !CopyText(list->keys[i], output.keys[i], budget)) return false;
            if (!CopyNode(list->values[i], output.children[i], budget, depth + 1)) return false;
        }
        return true;
    }
    default: return false;
    }
}
FilterNode const* Field(FilterNode const& node, std::string_view name, bool& duplicate) {
    FilterNode const* found{};
    for (std::size_t i = 0; i < node.keys.size(); ++i) {
        if (node.keys[i] != name) continue;
        if (found) duplicate = true;
        found = &node.children[i];
    }
    return found;
}
FilterChainEntry Describe(FilterNode const& node, std::size_t index) {
    FilterChainEntry row;
    row.index = index;
    row.display = "Unrecognized filter entry #" + std::to_string(index);
    if (node.format != MPV_FORMAT_NODE_MAP) {
        row.error = "Filter entry is not a map";
        return row;
    }
    bool duplicate{};
    auto name = Field(node, "name", duplicate);
    auto label = Field(node, "label", duplicate);
    auto enabled = Field(node, "enabled", duplicate);
    auto params = Field(node, "params", duplicate);
    if (enabled && enabled->format == MPV_FORMAT_FLAG) row.enabled = enabled->flag != 0;
    if (duplicate || !name || name->format != MPV_FORMAT_STRING ||
        (label && label->format != MPV_FORMAT_STRING) ||
        (enabled && enabled->format != MPV_FORMAT_FLAG) ||
        (params && params->format != MPV_FORMAT_NODE_MAP)) {
        row.error = "Missing, duplicate or invalid filter field";
        return row;
    }
    std::optional<MPVFilter::Parameters> arguments;
    if (params) {
        arguments.emplace();
        for (std::size_t i = 0; i < params->keys.size(); ++i) {
            if (params->children[i].format != MPV_FORMAT_STRING ||
                !arguments->emplace(params->keys[i], params->children[i].text).second) {
                row.error = "Invalid or duplicate filter parameter";
                return row;
            }
        }
    }
    auto prefix = label ? "@" + label->text + ":" : std::string{};
    if (!ValidName(name->text) || (label && !ValidName(label->text))) {
        row.display = prefix + name->text;
        row.error = "Unsupported filter name or label syntax";
        return row;
    }
    row.filter.emplace(name->text, label ? std::optional(label->text) : std::nullopt,
                       std::move(arguments));
    row.display = row.filter->StringFormat();
    return row;
}
// mpv node-map iteration order is unspecified (notably filter params).
// Compare maps by unique key, preserving array order and all unknown fields.
// Duplicate keys have no unambiguous dictionary semantics and are read-only.
bool NodesEqual(FilterNode const& left, FilterNode const& right,
                std::size_t& visited, unsigned depth = 0) {
    if (depth > 8 || ++visited > 32768 || left.format != right.format) return false;
    switch (left.format) {
    case MPV_FORMAT_NONE: return true;
    case MPV_FORMAT_STRING: return left.text == right.text;
    case MPV_FORMAT_FLAG: return (left.flag != 0) == (right.flag != 0);
    case MPV_FORMAT_INT64: return left.integer == right.integer;
    case MPV_FORMAT_DOUBLE: return std::isfinite(left.number) && left.number == right.number;
    case MPV_FORMAT_BYTE_ARRAY: return left.bytes == right.bytes;
    case MPV_FORMAT_NODE_ARRAY:
        if (left.children.size() != right.children.size()) return false;
        for (std::size_t i = 0; i < left.children.size(); ++i)
            if (!NodesEqual(left.children[i], right.children[i], visited, depth + 1)) return false;
        return true;
    case MPV_FORMAT_NODE_MAP: {
        auto size = left.keys.size();
        if (size > 32768 || size != right.keys.size() ||
            size != left.children.size() || size != right.children.size()) return false;
        std::vector<std::size_t> leftOrder(size), rightOrder(size);
        std::iota(leftOrder.begin(), leftOrder.end(), std::size_t{});
        std::iota(rightOrder.begin(), rightOrder.end(), std::size_t{});
        std::sort(leftOrder.begin(), leftOrder.end(), [&](auto a, auto b) { return left.keys[a] < left.keys[b]; });
        std::sort(rightOrder.begin(), rightOrder.end(), [&](auto a, auto b) { return right.keys[a] < right.keys[b]; });
        for (std::size_t i = 0; i < size; ++i) {
            auto a = leftOrder[i], b = rightOrder[i];
            if (left.keys[a] != right.keys[b] ||
                (i && (left.keys[a] == left.keys[leftOrder[i - 1]] ||
                       right.keys[b] == right.keys[rightOrder[i - 1]]))) return false;
            if (!NodesEqual(left.children[a], right.children[b], visited, depth + 1)) return false;
        }
        return true;
    }
    default: return false;
    }
}
FilterEditPlan Begin(FilterChainSnapshot const& snapshot, std::uint64_t revision,
                     std::size_t index) {
    FilterEditPlan plan;
    if (!snapshot.complete || snapshot.root.format != MPV_FORMAT_NODE_ARRAY)
        plan.error = "Filter list is unavailable or incomplete";
    else if (snapshot.revision != revision) plan.error = "Filter selection is stale";
    else if (index >= snapshot.entries.size() || index >= snapshot.root.children.size())
        plan.error = "Filter index is out of range";
    else {
        std::size_t visited{};
        if (!NodesEqual(snapshot.root, snapshot.root, visited)) {
            plan.error = "Ambiguous duplicate filter fields or invalid node structure";
            return plan;
        }
        plan.before = snapshot.root;
        plan.after = snapshot.root;
        plan.valid = true;
    }
    return plan;
}
FilterNode Text(std::string value) {
    FilterNode node; node.format = MPV_FORMAT_STRING; node.text = std::move(value); return node;
}
void Put(FilterNode& node, std::string key, FilterNode value) {
    for (std::size_t i = 0; i < node.keys.size(); ++i) {
        if (node.keys[i] != key) continue;
        node.children[i] = std::move(value); return;
    }
    node.keys.push_back(std::move(key));
    node.children.push_back(std::move(value));
}
}
FilterChainSnapshot ParseFilterChain(mpv_node const& node, std::uint64_t revision) {
    FilterChainSnapshot result;
    result.revision = revision;
    if (node.format != MPV_FORMAT_NODE_ARRAY || !node.u.list ||
        node.u.list->num < 0 || node.u.list->num > 4096) {
        result.error = "Invalid or oversized filter list";
        return result;
    }
    Budget budget;
    result.complete = CopyNode(node, result.root, budget, 0);
    if (!result.complete) {
        result.error = "Filter list cannot be copied losslessly within resource limits";
        // Do not expose a partial tree as an editable snapshot.
        result.root = {};
        return result;
    }
    result.entries.reserve(result.root.children.size());
    for (std::size_t i = 0; i < result.root.children.size(); ++i)
        result.entries.push_back(Describe(result.root.children[i], i));
    return result;
}
bool FilterEditPlan::FreshMatches(FilterChainSnapshot const& snapshot) const {
    if (!valid || !snapshot.complete) return false;
    std::size_t visited{};
    return NodesEqual(snapshot.root, before, visited);
}
FilterEditPlan FilterChainEditor::CreateSingle(FilterChainSnapshot const& snapshot,
    MPVFilter const& filter) {
    FilterEditPlan empty{snapshot.root,{},true,{}};
    if (!empty.FreshMatches(snapshot) || snapshot.root.format != MPV_FORMAT_NODE_ARRAY ||
        !snapshot.root.children.empty() || !snapshot.entries.empty())
        return {{},{},false,"A complete empty filter chain is required"};
    // Reuse the structured parameter validation and resource bounds in Replace.
    auto candidate = snapshot;
    FilterNode row; row.format = MPV_FORMAT_NODE_MAP;
    candidate.root.children.push_back(std::move(row));
    candidate.entries.push_back({});
    auto plan = Replace(candidate,candidate.revision,0,filter);
    if (plan.valid) plan.before = snapshot.root;
    return plan;
}
FilterEditPlan FilterChainEditor::AppendStructured(FilterChainSnapshot const& snapshot,
    std::uint64_t revision,MPVFilter const& filter) {
    FilterEditPlan initial{snapshot.root,{},true,{}};
    if (snapshot.revision!=revision || !initial.FreshMatches(snapshot) ||
        snapshot.root.format!=MPV_FORMAT_NODE_ARRAY || snapshot.root.children.size()>=4096 ||
        snapshot.entries.size()!=snapshot.root.children.size())
        return {{},{},false,"Stale, ambiguous or oversized filter chain"};
    auto candidate=snapshot;
    FilterNode row; row.format=MPV_FORMAT_NODE_MAP;
    candidate.root.children.push_back(std::move(row)); candidate.entries.push_back({});
    auto plan=Replace(candidate,revision,candidate.entries.size()-1,filter);
    if (plan.valid) {
        FilterNodeView view(plan.after);
        if (!ParseFilterChain(view.Get()).complete) return {{},{},false,"Result exceeds filter-chain resource limits"};
        plan.before=snapshot.root;
    }
    return plan;
}
FilterEditPlan FilterChainEditor::Remove(FilterChainSnapshot const& snapshot,
    std::uint64_t revision, std::size_t index) {
    auto plan = Begin(snapshot, revision, index);
    if (plan.valid) plan.after.children.erase(plan.after.children.begin() + index);
    return plan;
}
FilterEditPlan FilterChainEditor::Move(FilterChainSnapshot const& snapshot,
    std::uint64_t revision, std::size_t index, std::size_t destination) {
    auto plan = Begin(snapshot, revision, index);
    if (!plan.valid) return plan;
    if (destination >= plan.after.children.size()) {
        plan.valid = false; plan.error = "Filter destination is out of range"; return plan;
    }
    auto entry = std::move(plan.after.children[index]);
    plan.after.children.erase(plan.after.children.begin() + index);
    plan.after.children.insert(plan.after.children.begin() + destination, std::move(entry));
    return plan;
}
FilterEditPlan FilterChainEditor::SetEnabled(FilterChainSnapshot const& snapshot,
    std::uint64_t revision, std::size_t index, bool enabled) {
    auto plan = Begin(snapshot, revision, index);
    if (!plan.valid) return plan;
    if (!snapshot.entries[index].filter) {
        plan.valid = false; plan.error = "Malformed filter may be removed but not toggled"; return plan;
    }
    FilterNode flag; flag.format = MPV_FORMAT_FLAG; flag.flag = enabled ? 1 : 0;
    Put(plan.after.children[index], "enabled", std::move(flag));
    return plan;
}
FilterEditPlan FilterChainEditor::Replace(FilterChainSnapshot const& snapshot,
    std::uint64_t revision, std::size_t index, MPVFilter const& replacement) {
    auto plan = Begin(snapshot, revision, index);
    if (!plan.valid) return plan;
    if (!ValidName(replacement.Name()) ||
        (replacement.Label() && !ValidName(*replacement.Label())) || replacement.RawParameters()) {
        plan.valid = false; plan.error = "Replacement requires structured filter parameters"; return plan;
    }
    std::size_t payload = replacement.Name().size() +
        (replacement.Label() ? replacement.Label()->size() : 0);
    if (payload > MaxPayload || (replacement.Params() && replacement.Params()->size() > 4096)) {
        plan.valid = false; plan.error = "Replacement exceeds resource limits"; return plan;
    }
    FilterNode node; node.format = MPV_FORMAT_NODE_MAP;
    Put(node, "name", Text(replacement.Name()));
    if (replacement.Label()) Put(node, "label", Text(*replacement.Label()));
    FilterNode enabled; enabled.format = MPV_FORMAT_FLAG;
    enabled.flag = snapshot.entries[index].enabled ? 1 : 0;
    Put(node, "enabled", std::move(enabled));
    if (replacement.Params()) {
        FilterNode params; params.format = MPV_FORMAT_NODE_MAP;
        for (auto const& [key, value] : *replacement.Params()) {
            if (key.empty() || key.find('\0') != std::string::npos || value.find('\0') != std::string::npos) {
                plan.valid = false; plan.error = "Invalid filter parameter"; return plan;
            }
            if (key.size() > MaxPayload - payload) {
                plan.valid = false; plan.error = "Replacement exceeds resource limits"; return plan;
            }
            payload += key.size();
            if (value.size() > MaxPayload - payload) {
                plan.valid = false; plan.error = "Replacement exceeds resource limits"; return plan;
            }
            payload += value.size();
            Put(params, key, Text(value));
        }
        Put(node, "params", std::move(params));
    }
    plan.after.children[index] = std::move(node);
    return plan;
}

struct FilterNodeView::Storage {
    mpv_node node{};
    mpv_node_list list{};
    mpv_byte_array bytes{};
    std::string text;
    std::vector<unsigned char> byteStorage;
    std::vector<std::string> keyStorage;
    std::vector<char*> keys;
    std::vector<mpv_node> values;
    std::vector<std::unique_ptr<Storage>> children;
    explicit Storage(FilterNode const& source) {
        node.format = source.format;
        switch (source.format) {
        case MPV_FORMAT_NONE: break;
        case MPV_FORMAT_STRING: text = source.text; node.u.string = text.data(); break;
        case MPV_FORMAT_FLAG: node.u.flag = source.flag; break;
        case MPV_FORMAT_INT64: node.u.int64 = source.integer; break;
        case MPV_FORMAT_DOUBLE: node.u.double_ = source.number; break;
        case MPV_FORMAT_BYTE_ARRAY:
            byteStorage = source.bytes;
            bytes.data = byteStorage.data(); bytes.size = byteStorage.size(); node.u.ba = &bytes; break;
        case MPV_FORMAT_NODE_MAP:
        case MPV_FORMAT_NODE_ARRAY:
            if (source.children.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                (source.format == MPV_FORMAT_NODE_MAP && source.keys.size() != source.children.size()))
                throw std::invalid_argument("Invalid owned filter node");
            keyStorage = source.keys;
            for (auto& key : keyStorage) keys.push_back(key.data());
            for (auto const& child : source.children) {
                children.push_back(std::make_unique<Storage>(child));
                values.push_back(children.back()->node);
            }
            list.num = static_cast<int>(values.size()); list.values = values.data();
            list.keys = source.format == MPV_FORMAT_NODE_MAP ? keys.data() : nullptr;
            node.u.list = &list; break;
        default: throw std::invalid_argument("Unsupported owned filter node type");
        }
    }
};
FilterNodeView::FilterNodeView(FilterNode const& node) : storage_(std::make_unique<Storage>(node)) {}
FilterNodeView::~FilterNodeView() = default;
mpv_node const& FilterNodeView::Get() const noexcept { return storage_->node; }
OrientationFilter FindOrientationFilter(FilterChainSnapshot const& snapshot, bool mirror) {
    if (!snapshot.complete) return {};
    const auto label = mirror ? "iina_mirror" : "iina_flip";
    const auto name = mirror ? "hflip" : "vflip";
    OrientationFilter result{OrientationFilterState::Absent, 0};
    for (std::size_t index=0; index<snapshot.entries.size(); ++index) {
        auto const& row=snapshot.entries[index];
        // A malformed row can conceal a reserved label. Refuse a write rather
        // than guessing whether adding would replace someone else's filter.
        if (!row.filter) return {};
        if (row.filter->Label() != label) continue;
        if (result.state != OrientationFilterState::Absent || row.filter->Name() != name)
            return {OrientationFilterState::Conflict, index};
        result={row.enabled ? OrientationFilterState::Enabled : OrientationFilterState::Disabled,index};
    }
    return result;
}

}

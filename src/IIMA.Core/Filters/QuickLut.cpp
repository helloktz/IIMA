// IINA DraggingDetect.openFromPasteboard quick lut3d, Copyright (c) lhc.
// GPL-3.0; references/iina/LICENSE. mpv vf add replaces an existing label;
// never use blind add for this reserved-label operation.
#include "QuickLut.h"
#include "FilterPresets.h"
#include <algorithm>
namespace iima {
namespace {
constexpr auto Label="iina_quickl3d";
bool ValidPath(std::string_view path) {
    if (path.empty() || path.size()>16384 || std::any_of(path.begin(),path.end(),[](unsigned char c){return c<32 || c==127;})) return false;
    std::string normalized(path);
    std::replace(normalized.begin(),normalized.end(),'/',static_cast<char>(92));
    if (normalized.starts_with(R"(\\?\)") || normalized.starts_with(R"(\\.\)") || normalized.find(':',2)!=normalized.npos) return false;
    const bool drive=normalized.size()>3 && ((normalized[0]>='A' && normalized[0]<='Z') ||
        (normalized[0]>='a' && normalized[0]<='z')) && normalized[1]==':' && normalized[2]==92;
    const auto share=normalized.find(static_cast<char>(92),2);
    const auto file=share==normalized.npos ? normalized.npos : normalized.find(static_cast<char>(92),share+1);
    const bool unc=normalized.starts_with(R"(\\)") && share!=normalized.npos && share>2 &&
        file!=normalized.npos && file>share+1 && file+1<normalized.size();
    if (!drive && !unc) return false;
    auto dot=normalized.find_last_of('.');
    if (dot==normalized.npos) return false;
    auto extension=normalized.substr(dot);
    for (auto& c:extension) if(c>='A' && c<='Z') c+='a'-'A';
    return extension==".cube" || extension==".3dl" || extension==".dat" || extension==".m3d";
}
std::optional<std::string> Unescape(std::string_view value) {
    std::string result;
    for (std::size_t i=0;i<value.size();++i) {
        if (value[i]==92 && ++i==value.size()) return {};
        result+=value[i];
    }
    return result;
}
std::optional<std::string> OwnedPath(FilterChainEntry const& entry,FilterNode const& node) {
    if (!entry.filter || entry.filter->Name()!="lavfi" || !entry.filter->Params() ||
        entry.filter->Params()->size()!=1 || !entry.filter->Params()->contains("graph")) return {};
    // Unknown fields on the target itself must not be discarded by replacement.
    for (auto const& key:node.keys)
        if (key!="name" && key!="label" && key!="enabled" && key!="params") return {};
    auto const& graph=entry.filter->Params()->at("graph");
    constexpr std::string_view prefix="lut3d=file=", suffix=":interp=nearest";
    if (!graph.starts_with(prefix) || !graph.ends_with(suffix) || graph.size()<prefix.size()+suffix.size()) return {};
    auto option=Unescape(std::string_view(graph).substr(prefix.size(),graph.size()-prefix.size()-suffix.size()));
    if (!option) return {};
    auto path=Unescape(*option);
    if (!path || !ValidPath(*path)) return {};
    auto canonical=BuildLut3dGraph(*path,"nearest");
    if (!canonical || *canonical!=graph) return {};
    return path;
}
}
QuickLutInfo FindQuickLutFilter(FilterChainSnapshot const& snapshot) {
    FilterEditPlan identity{snapshot.root,{},true,{}};
    if (!snapshot.complete || snapshot.root.format!=MPV_FORMAT_NODE_ARRAY ||
        snapshot.root.children.size()!=snapshot.entries.size() || !identity.FreshMatches(snapshot)) return {};
    QuickLutInfo result{QuickLutState::Absent};
    for (std::size_t i=0;i<snapshot.root.children.size();++i) {
        auto const& node=snapshot.root.children[i];
        if (node.format!=MPV_FORMAT_NODE_MAP) return {};
        auto label=std::find(node.keys.begin(),node.keys.end(),"label");
        if (label==node.keys.end()) continue; // Preserve unrelated unknown nodes.
        auto const& value=node.children[static_cast<std::size_t>(label-node.keys.begin())];
        if (value.format!=MPV_FORMAT_STRING) return {};
        if (value.text!=Label) continue;
        if (result.state!=QuickLutState::Absent) return {QuickLutState::Conflict,i,{}};
        auto path=OwnedPath(snapshot.entries[i],node);
        if (!path) return {QuickLutState::Conflict,i,{}};
        result={snapshot.entries[i].enabled ? QuickLutState::Enabled : QuickLutState::Disabled,i,std::move(*path)};
    }
    return result;
}
FilterEditPlan PlanQuickLut(FilterChainSnapshot const& snapshot,std::uint64_t revision,
    std::optional<std::string> const& path) {
    auto reject=[](std::string error) { return FilterEditPlan{{},{},false,std::move(error)}; };
    if (snapshot.revision!=revision) return reject("Quick LUT selection is stale");
    auto found=FindQuickLutFilter(snapshot);
    if (found.state==QuickLutState::Unknown || found.state==QuickLutState::Conflict)
        return reject("Quick LUT label is ambiguous, unavailable or owned by another filter shape");
    FilterEditPlan plan;
    if (!path) {
        if (found.state==QuickLutState::Absent) return reject("No quick LUT is installed");
        plan=FilterChainEditor::Remove(snapshot,revision,found.index);
    } else {
        if (!ValidPath(*path)) return reject("Quick LUT requires an absolute supported local file path");
        auto graph=BuildLut3dGraph(*path,"nearest");
        if (!graph) return reject("Invalid quick LUT graph");
        MPVFilter replacement("lavfi",Label,MPVFilter::Parameters{{"graph",std::move(*graph)}});
        plan=found.state==QuickLutState::Absent ? FilterChainEditor::AppendStructured(snapshot,revision,replacement) :
            FilterChainEditor::Replace(snapshot,revision,found.index,replacement);
        if (plan.valid && found.state==QuickLutState::Disabled) {
            auto& node=plan.after.children[found.index];
            auto enabled=std::find(node.keys.begin(),node.keys.end(),"enabled");
            node.children[static_cast<std::size_t>(enabled-node.keys.begin())].flag=1;
        }
    }
    if (plan.valid) {
        FilterNodeView view(plan.after);
        if (!ParseFilterChain(view.Get()).complete) return reject("Quick LUT result exceeds filter-chain resource limits");
    }
    return plan;
}
}

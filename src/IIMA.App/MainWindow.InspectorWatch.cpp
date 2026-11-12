// InspectorWindowController.swift watchProperties / updateInfo / add/removeWatchAction.
// Copyright (c) 2016 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <chrono>
#include <sstream>
#include <set>
using namespace winrt;
namespace winrt::IIMA::implementation {
void MainWindow::WireInspectorWatch() {
    inspectorTimer_ = DispatcherQueue().CreateTimer();
    inspectorTimer_.Interval(std::chrono::seconds(1));
    inspectorTimer_.IsRepeating(true);
    auto weak = get_weak();
    inspectorTimer_.Tick([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->RefreshInspectorWatch();
    });
    InspectorApplyButton().Click([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->ApplyInspectorWatch();
    });
    MediaInformationExpander().Expanding([weak](auto const&, auto const&) {
        if (auto self = weak.get(); self && !self->closing_) {
            self->inspectorTimer_.Start(); // First tick sees IsExpanded == true.
        }
    });
    MediaInformationExpander().Collapsed([weak](auto const&, auto const&) {
        if (auto self = weak.get()) self->InvalidateInspectorWatch();
    });
}
void MainWindow::LoadInspectorWatch() {
    std::string names;
    for (auto const& name : userData_.settings.watchProperties) names += name + "\n";
    InspectorNamesBox().Text(to_hstring(names));
}
void MainWindow::InvalidateInspectorWatch() {
    ++inspectorGeneration_;
    if (session_) session_->CancelWatchedProperties();
    inspectorBusy_ = false;
    if (inspectorTimer_) inspectorTimer_.Stop();
}
void MainWindow::ApplyInspectorWatch() {
    if (closing_ || !userDataLoaded_) return;
    std::vector<std::string> names;
    std::set<std::string> seen;
    std::istringstream lines(to_string(InspectorNamesBox().Text()));
    std::string name;
    while (std::getline(lines, name)) {
        auto begin = name.find_first_not_of(" \t\r");
        if (begin == std::string::npos) continue;
        name = name.substr(begin, name.find_last_not_of(" \t\r") - begin + 1);
        bool valid = !name.empty() && name.size() <= 128;
        for (unsigned char c : name) if (c <= 32 || c >= 127) valid = false;
        if (!valid) { InspectorStatusText().Text(L"属性名必须是 1–128 个非空白 ASCII 字符。"); return; }
        if (seen.insert(name).second) names.push_back(name);
        if (names.size() > 32) { InspectorStatusText().Text(L"最多监视 32 个属性。"); return; }
    }
    InvalidateInspectorWatch();
    userData_.settings.watchProperties = std::move(names);
    MarkUserDataDirty();
    LoadInspectorWatch();
    InspectorValuesText().Text(L"");
    InspectorStatusText().Text(L"属性列表已应用；保存结果由设置保存状态显示。");
    if (MediaInformationExpander().IsExpanded()) {
        inspectorTimer_.Start();
        RefreshInspectorWatch();
    }
}
void MainWindow::RefreshInspectorWatch() {
    if (closing_ || !userDataLoaded_ || !MediaInformationExpander().IsExpanded() || inspectorBusy_) return;
    auto const& state = session_->Snapshot();
    if (userData_.settings.watchProperties.empty()) {
        InspectorStatusText().Text(L"输入属性名后点击“保存并监视”。");
        InspectorValuesText().Text(L""); return;
    }
    if (state.media != iima::MediaState::Loaded) {
        InspectorStatusText().Text(L"等待媒体加载。"); InspectorValuesText().Text(L""); return;
    }
    auto weak = get_weak();
    auto generation = inspectorGeneration_;
    auto path = state.path;
    auto entry = state.entryId;
    inspectorBusy_ = true;
    bool accepted = session_->ReadWatchedProperties(userData_.settings.watchProperties,
        [weak, generation, path, entry](auto values, std::string error) {
            auto self = weak.get();
            if (!self || self->closing_ || generation != self->inspectorGeneration_) return;
            self->inspectorBusy_ = false;
            if (!self->MediaInformationExpander().IsExpanded()) return;
            auto const& current = self->session_->Snapshot();
            if (current.media != iima::MediaState::Loaded || current.entryId != entry || current.path != path) return;
            if (!error.empty()) { self->InspectorValuesText().Text(L""); self->InspectorStatusText().Text(to_hstring(error)); return; }
            std::string text;
            for (auto const& item : values) {
                text += item.name + ": " + (item.error.empty() ? item.value : "[" + item.error + "]");
                if (item.truncated) text += " [truncated]";
                text += "\n";
            }
            self->InspectorValuesText().Text(to_hstring(text));
            self->InspectorStatusText().Text(L"每秒刷新；只读，不将属性值保存到日志或设置。");
        });
    if (!accepted) {
        inspectorBusy_ = false;
        InspectorStatusText().Text(L"暂时无法读取，稍后重试。");
    }
}
}

// IINA JavascriptAPIInput.getAllKeyBindings and PlayerWindowController key events.
// Copyright (c) 2023 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.Data.Json.h>
#include <stdexcept>

namespace winrt::IIMA::implementation {
std::string MainWindow::PluginKeyBindingsJson() const {
    using namespace Windows::Data::Json;
    JsonObject result;
    if (!keyBindings_) return "{}";
    auto const& bindings = keyBindings_->Ordered();
    if (bindings.size() > 1024) throw std::length_error("Plugin binding snapshot exceeds 1024 entries.");
    std::size_t bytes{};
    for (auto const& binding : bindings) {
        auto const& key = binding.NormalizedMpvKey();
        auto const& action = binding.RawAction();
        bytes += key.size() + action.size();
        if (key.empty() || key.size() > 256 || action.size() > 16384 || bytes > 256 * 1024)
            throw std::length_error("Plugin binding snapshot exceeds its text limit.");
        JsonObject entry;
        entry.Insert(L"key", JsonValue::CreateStringValue(to_hstring(key)));
        entry.Insert(L"action", JsonValue::CreateStringValue(to_hstring(action)));
        entry.Insert(L"isIINACommand", JsonValue::CreateBooleanValue(binding.IsIinaCommand()));
        result.Insert(to_hstring(key), entry);
    }
    auto json = to_string(result.Stringify());
    if (json.size() > 256 * 1024) throw std::length_error("Plugin binding JSON exceeds 256 KiB.");
    return json;
}
void MainWindow::UpdatePluginKeyBindings() {
    if (!pluginRuntime_ || closing_) return;
    try {
        if (!pluginRuntime_->UpdateKeyBindings(PluginKeyBindingsJson()))
            throw std::runtime_error("Plugin input snapshot could not be delivered.");
    } catch (...) {
        StopLocalPlugin();
        PluginRuntimeStatus().Text(L"无法更新插件快捷键快照，插件已停止。快捷键配置本身仍已应用。");
    }
}
void MainWindow::PublishPluginKey(std::string const& key, bool down, bool repeat) {
    if (!pluginRuntime_ || closing_ || pickerOpen_ || !KeyboardPlaybackAllowed()) return;
    // No process-wide hook: only player-owned routed keys outside editable or
    // modal controls. The worker is asynchronous and cannot block default input.
    try {
        if (!pluginRuntime_->PublishKeyInput(key, down, repeat))
            throw std::runtime_error("Plugin input event queue is full or stopped.");
    } catch (...) {
        StopLocalPlugin();
        PluginRuntimeStatus().Text(L"插件键盘事件队列不可用，已停止插件以避免静默丢失事件。");
    }
}
}

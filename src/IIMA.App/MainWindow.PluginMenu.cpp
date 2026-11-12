// IINA JavascriptAPIMenu / JavascriptPluginMenuItem / MenuController.updatePluginMenu.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace winrt;
using namespace Windows::Data::Json;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

struct MenuNode {
    std::string id;
    hstring title;
    bool enabled{}, selected{}, separator{};
    std::vector<MenuNode> children;
};
struct MenuSnapshot {
    std::string revision;
    std::vector<MenuNode> items;
};
void Require(bool condition, char const* message) {
    if (!condition) throw std::invalid_argument(message);
}
std::string Identity(JsonObject const& object, wchar_t const* key) {
    auto text = object.GetNamedString(key);
    Require(!text.empty() && text.size() <= 128, "Invalid plugin menu identity.");
    for (wchar_t c : text) Require(c >= 0x20 && c != 0x7f, "Invalid plugin menu identity.");
    auto bytes = to_string(text);
    Require(bytes.size() <= 128, "Plugin menu identity exceeds its byte limit.");
    return bytes;
}
MenuSnapshot ParseMenu(std::string const& json) {
    Require(!json.empty() && json.size() <= 64 * 1024 && json.find('\0') == json.npos,
        "Plugin menu JSON exceeds its size limit.");
    // Menu depth is eight; JSON adds one array and one object at every level.
    // Bound parser recursion before parsing, then check the semantic tree below.
    unsigned nesting{};
    bool quoted{}, escape{};
    for (char c : json) {
        if (quoted) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '[' || c == '{') Require(++nesting <= 20, "Plugin menu JSON is too deeply nested.");
        else if (c == ']' || c == '}') { Require(nesting != 0, "Malformed plugin menu JSON."); --nesting; }
    }
    Require(!quoted && nesting == 0, "Incomplete plugin menu JSON.");
    auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, json.data(), static_cast<int>(json.size()), nullptr, 0);
    if (!length) throw_last_error();
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, json.data(), static_cast<int>(json.size()), wide.data(), length))
        throw_last_error();
    auto root = JsonObject::Parse(wide);
    Require(root.Size() == 2 && root.HasKey(L"revision") && root.HasKey(L"items"), "Unexpected plugin menu root fields.");
    MenuSnapshot snapshot;
    snapshot.revision = Identity(root, L"revision");
    std::set<std::string> identities;
    std::size_t count{};
    std::function<std::vector<MenuNode>(JsonArray const&, unsigned)> read;
    read = [&](JsonArray const& values, unsigned depth) {
        Require(depth <= 8 || values.Size() == 0, "Plugin menu depth limit exceeded.");
        std::vector<MenuNode> nodes;
        for (auto const& value : values) {
            Require(++count <= 128, "Plugin menu node limit exceeded.");
            auto object = value.GetObject();
            Require(object.Size() == 7 && object.HasKey(L"id") && object.HasKey(L"title") &&
                object.HasKey(L"enabled") && object.HasKey(L"selected") && object.HasKey(L"separator") &&
                object.HasKey(L"keyBinding") && object.HasKey(L"items"), "Unexpected plugin menu item fields.");
            MenuNode node;
            node.id = Identity(object, L"id");
            Require(identities.insert(node.id).second, "Repeated plugin menu identity.");
            node.title = object.GetNamedString(L"title");
            Require(node.title.size() <= 4096 && std::wstring_view(node.title).find(L'\0') == std::wstring_view::npos,
                "Invalid plugin menu title.");
            node.enabled = object.GetNamedBoolean(L"enabled");
            node.selected = object.GetNamedBoolean(L"selected");
            node.separator = object.GetNamedBoolean(L"separator");
            auto key = object.GetNamedValue(L"keyBinding");
            Require(key.ValueType() == JsonValueType::Null ||
                (key.ValueType() == JsonValueType::String && key.GetString().empty()),
                "Plugin menu keyboard bindings are not supported by this adapter.");
            node.children = read(object.GetNamedArray(L"items"), depth + 1);
            Require(!node.separator || node.children.empty(), "Plugin menu separator cannot own a submenu.");
            nodes.push_back(std::move(node));
        }
        return nodes;
    };
    snapshot.items = read(root.GetNamedArray(L"items"), 1);
    return snapshot;
}
}

namespace winrt::IIMA::implementation {
void MainWindow::ClearPluginMenu() {
    // Stop/close may run while a flyout is open. Release the entire previous tree
    // and its handlers; weak captures and generation checks guard late clicks.
    try {
        auto button = PluginMenuButton();
        if (auto flyout = button.Flyout()) flyout.Hide();
        button.Flyout(nullptr);
        button.IsEnabled(false);
    } catch (...) {
        // A disconnected XAML tree must never prevent cancellation of the worker.
    }
}
void MainWindow::ApplyPluginMenu(std::string const& json) {
    if (closing_ || !pluginRuntime_) return;
    try {
        auto snapshot = ParseMenu(json);
        auto generation = pluginGeneration_;
        auto weak = get_weak();
        auto invoke = [weak, generation, revision = snapshot.revision](std::string const& id) {
            if (auto self = weak.get(); self && !self->closing_ && self->pluginRuntime_ &&
                self->pluginGeneration_ == generation) {
                try {
                    if (!self->pluginRuntime_->InvokeMenu(id, revision))
                        self->PluginRuntimeStatus().Text(L"插件菜单已变化或任务队列不可用；本次操作未提交。");
                } catch (...) {
                    self->StopLocalPlugin();
                    self->PluginRuntimeStatus().Text(L"无法提交插件菜单操作，插件已停止。");
                }
            }
        };
        std::function<MenuFlyoutItemBase(MenuNode const&, bool)> build;
        build = [&](MenuNode const& node, bool ancestorEnabled) -> MenuFlyoutItemBase {
            if (node.separator) return MenuFlyoutSeparator{};
            auto enabled = ancestorEnabled && node.enabled;
            if (!node.children.empty()) {
                MenuFlyoutSubItem item;
                item.Text(node.title); item.IsEnabled(enabled);
                if (node.selected) {
                    FontIcon icon; icon.Glyph(L"\uE73E"); item.Icon(icon);
                    Automation::AutomationProperties::ItemStatus(item, L"已选中");
                }
                for (auto const& child : node.children) item.Items().Append(build(child, enabled));
                // A submenu opens its children; its own JS action is not invoked.
                return item;
            }
            ToggleMenuFlyoutItem item;
            item.Text(node.title); item.IsEnabled(enabled); item.IsChecked(node.selected);
            item.Click([invoke, id = node.id, selected = node.selected, enabled](auto const& sender, auto const&) {
                // Native toggling is not authoritative. IINA lets the JS action
                // change `selected`; restore the snapshot until it publishes one.
                try { sender.template as<ToggleMenuFlyoutItem>().IsChecked(selected); }
                catch (...) { return; }
                if (enabled) invoke(id);
            });
            return item;
        };
        MenuFlyout replacement;
        for (auto const& item : snapshot.items) replacement.Items().Append(build(item, true));
        // Validate and construct everything before replacing the visible flyout.
        // No half-built tree is exposed if validation or allocation throws.
        auto button = PluginMenuButton();
        if (auto previous = button.Flyout()) previous.Hide();
        button.Flyout(replacement);
        button.IsEnabled(!snapshot.items.empty());
    } catch (...) {
        StopLocalPlugin();
        if (!closing_) PluginRuntimeStatus().Text(L"插件菜单数据无效或无法显示，插件已停止。");
    }
}
}

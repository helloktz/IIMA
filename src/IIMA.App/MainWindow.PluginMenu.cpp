// IINA JavascriptAPIMenu / JavascriptPluginMenuItem / MenuController.updatePluginMenu.
// Copyright (c) 2018 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "../IIMA.Core/Input/KeyMapping.h"
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
    std::string keyBinding;
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
            Require(key.ValueType() == JsonValueType::Null || key.ValueType() == JsonValueType::String,
                "Plugin menu key must be a string or null.");
            if (key.ValueType() == JsonValueType::String) {
                auto raw = to_string(key.GetString());
                Require(raw.size() <= 256, "Plugin menu key exceeds its byte budget.");
                for (unsigned char c : raw) Require(c > 32 && c != 127, "Invalid plugin menu key character.");
                node.keyBinding = iima::NormalizeMpvKey(raw);
                Require(node.keyBinding.size() <= 256, "Normalized plugin menu key is too long.");
            }
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
bool MainWindow::PluginMenuVisible() const noexcept { return pluginMenuVisibleCount_ != 0; }
void MainWindow::WireNativePluginMenu(Controls::MenuBarItem const& menu) {
    if (nativePluginMenu_) return;
    nativePluginMenu_ = Controls::MenuFlyoutSubItem{};
    nativePluginMenu_.Text(L"当前插件命令");
    nativePluginMenu_.IsEnabled(false);
    auto weak = get_weak();
    auto visible = std::make_shared<bool>(false);
    // This fixed submenu row stays alive while its top-level popup is visible.
    // Replacing its children never replaces the tracker or an open target.
    nativePluginMenu_.Loaded([weak, visible](auto const&, auto const&) {
        if (auto self = weak.get(); self && !*visible) {
            *visible = true; ++self->pluginMenuVisibleCount_;
        }
    });
    nativePluginMenu_.Unloaded([weak, visible](auto const&, auto const&) {
        if (auto self = weak.get(); self && *visible) {
            *visible = false;
            if (self->pluginMenuVisibleCount_) --self->pluginMenuVisibleCount_;
            self->QueuePluginMenuRefresh();
        }
    });
    menu.Items().Append(nativePluginMenu_);
    pluginMenuClearPending_ = true;
    FlushPluginMenuRefresh();
}
void MainWindow::QueuePluginMenuRefresh() {
    if (closing_ || pluginMenuRefreshQueued_) return;
    pluginMenuRefreshQueued_ = true;
    auto weak = get_weak();
    if (!DispatcherQueue().TryEnqueue([weak] {
        if (auto self = weak.get()) {
            self->pluginMenuRefreshQueued_ = false;
            if (self->closing_) return;
            if (self->PluginMenuVisible()) { self->RefreshPlayerMenus(); return; }
            self->FlushPluginMenuRefresh();
            self->RefreshPlayerMenus();
        }
    })) pluginMenuRefreshQueued_ = false;
}
void MainWindow::FlushPluginMenuRefresh() {
    if (PluginMenuVisible()) return;
    if (pluginMenuClearPending_) {
        pluginMenuClearPending_ = false;
        try {
            if (nativePluginMenu_) {
                nativePluginMenu_.Items().Clear();
                Controls::MenuFlyoutItem empty;
                empty.Text(L"暂无插件命令"); empty.IsEnabled(false);
                nativePluginMenu_.Items().Append(empty);
                nativePluginMenu_.IsEnabled(false);
            }
            PluginMenuButton().Flyout(nullptr);
            PluginMenuButton().IsEnabled(false);
        } catch (...) { /* Detached XAML cannot prevent runtime cancellation. */ }
    }
    if (closing_ || !pluginRuntime_ || !pendingPluginMenuJson_) return;
    auto json = std::move(*pendingPluginMenuJson_);
    pendingPluginMenuJson_.reset();
    if (pendingPluginMenuGeneration_ == pluginGeneration_) ApplyPluginMenu(json);
}
void MainWindow::ClearPluginMenu() {
    pluginMenuShortcuts_.clear();
    pendingPluginMenuJson_.reset();
    pluginMenuClearPending_ = true;
    // Stop/close invalidates callback generations immediately. Keep an open
    // native tree inert until it unloads instead of moving targets under a click.
    try {
        if (nativePluginMenu_) nativePluginMenu_.IsEnabled(false);
        auto button = PluginMenuButton();
        button.IsEnabled(false);
        if (auto flyout = button.Flyout()) flyout.Hide();
    } catch (...) { /* A disconnected tree must not prevent stopping the worker. */ }
    FlushPluginMenuRefresh();
    QueuePluginMenuRefresh(); // Stop finishes invalidating runtime before this tick.
}
void MainWindow::ApplyPluginMenu(std::string const& json) {
    if (closing_ || !pluginRuntime_) return;
    try {
        auto snapshot = ParseMenu(json);
        if (PluginMenuVisible()) {
            // Validate now, but publish both surfaces and the shortcut map only
            // once closed. Bounded single latest snapshot, no update backlog.
            pendingPluginMenuJson_ = json;
            pendingPluginMenuGeneration_ = pluginGeneration_;
            return;
        }
        auto generation = pluginGeneration_;
        auto weak = get_weak();
        auto invoke = [weak, generation, revision = snapshot.revision](std::string const& id) {
            if (auto self = weak.get(); self && !self->closing_ && !self->pickerOpen_ &&
                !self->pluginStopPending_ && self->userDataLoaded_ && !self->interactiveCrop_.Active() &&
                !self->cropOriginal_ && self->pluginRuntime_ && self->pluginGeneration_ == generation) {
                try {
                    if (!self->pluginRuntime_->InvokeMenu(id, revision))
                        self->PluginRuntimeStatus().Text(L"插件菜单已变化或任务队列不可用；本次操作未提交。");
                } catch (...) {
                    self->StopLocalPlugin();
                    self->PluginRuntimeStatus().Text(L"无法提交插件菜单操作，插件已停止。");
                }
            }
        };
        std::map<std::string, std::pair<std::string, std::string>, std::less<>> shortcuts;
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
            if (!node.keyBinding.empty()) {
                // Visual hint only: a native KeyboardAccelerator would bypass
                // the player's text-entry/conflict policy and may fire twice.
                item.KeyboardAcceleratorTextOverride(to_hstring(iima::DisplayMpvKey(node.keyBinding)));
                Automation::AutomationProperties::SetName(item, node.title);
                Automation::AutomationProperties::SetHelpText(item,
                    L"快捷键 " + to_hstring(iima::DisplayMpvKey(node.keyBinding)) +
                    L"。仅播放器输入区生效，用户配置和保存滤镜优先；系统保留键与多键序列不支持。");
                if (enabled) shortcuts.try_emplace(node.keyBinding, node.id, snapshot.revision);
            }
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
        std::vector<MenuFlyoutItemBase> nativeItems;
        Controls::MenuFlyoutSubItem more{nullptr};
        for (std::size_t index = 0; index < snapshot.items.size(); ++index) {
            auto item = build(snapshot.items[index], true); // Independent XAML parent.
            if (index < 5) nativeItems.push_back(std::move(item));
            else {
                if (!more) {
                    more = Controls::MenuFlyoutSubItem{}; more.Text(L"更多…");
                    nativeItems.push_back(more);
                }
                more.Items().Append(item);
            }
        }
        if (nativeItems.empty()) {
            Controls::MenuFlyoutItem empty; empty.Text(L"暂无插件命令"); empty.IsEnabled(false);
            nativeItems.push_back(empty);
        }
        auto visible = std::make_shared<bool>(false);
        replacement.Opened([weak, visible](auto const&, auto const&) {
            if (auto self = weak.get(); self && !*visible) {
                *visible = true; ++self->pluginMenuVisibleCount_;
            }
        });
        replacement.Closed([weak, visible](auto const&, auto const&) {
            if (auto self = weak.get(); self && *visible) {
                *visible = false;
                if (self->pluginMenuVisibleCount_) --self->pluginMenuVisibleCount_;
                self->QueuePluginMenuRefresh();
            }
        });
        // Construct both trees before publishing. Never reuse a XAML node in
        // two parents. All handlers capture immutable generation/id/revision.
        auto button = PluginMenuButton();
        if (nativePluginMenu_) {
            nativePluginMenu_.Items().Clear();
            for (auto const& item : nativeItems) nativePluginMenu_.Items().Append(item);
            nativePluginMenu_.IsEnabled(!snapshot.items.empty());
        }
        button.Flyout(replacement);
        pluginMenuShortcuts_ = std::move(shortcuts);
        pluginMenuClearPending_ = false;
        pendingPluginMenuJson_.reset();
        button.IsEnabled(!snapshot.items.empty());
        QueuePluginMenuRefresh();
    } catch (...) {
        StopLocalPlugin();
        if (!closing_) PluginRuntimeStatus().Text(L"插件菜单数据无效或无法显示，插件已停止。");
    }
}
bool MainWindow::InvokePluginMenuShortcut(std::string const& key, bool repeated) {
    if (closing_ || pickerOpen_ || pluginStopPending_ || !pluginRuntime_ || !KeyboardPlaybackAllowed()) return false;
    auto found = pluginMenuShortcuts_.find(key);
    if (found == pluginMenuShortcuts_.end()) return false;
    // One action per physical press; the existing low-priority input observer
    // may still receive repeated key events, but cannot consume native input.
    if (repeated) return true;
    auto identity = found->second; // StopLocalPlugin may clear the map below.
    try {
        if (!pluginRuntime_->InvokeMenu(identity.first, identity.second))
            PluginRuntimeStatus().Text(L"插件菜单已更新或队列不可用，快捷键操作未提交。");
    } catch (...) {
        StopLocalPlugin();
        PluginRuntimeStatus().Text(L"插件菜单快捷键提交失败，插件已停止。");
    }
    return true;
}

}

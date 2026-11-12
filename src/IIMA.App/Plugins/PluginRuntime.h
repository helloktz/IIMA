// IINA JavascriptPluginInstance / JavascriptPolyfill / JavascriptAPICore /
// JavascriptAPIEvent / JavascriptAPIConsole Windows adaptation.
// Copyright (c) 2018-2020 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <string_view>

namespace iima::plugins {
struct PluginCommand {
    std::string name;
    double number{};
    bool flag{};
    std::string text;
    std::string mediaToken; // host snapshot identity for track/chapter actions
};

// One explicitly started, local, trusted plugin entry per owner/window.
// This is NOT a security sandbox or complete IINA plugin compatibility layer.
// No QuickJS std/os modules, host filesystem, network, process, native module
// loader, global controller or HTML capabilities are installed. CommonJS require
// is limited to the caller-supplied immutable package source snapshot.
class PluginRuntime final {
public:
    struct Callbacks {
        // Called on the engine worker. MUST return promptly and must not wait for
        // the UI or call Stop/destruct this instance. Enqueue weak/generation-
        // guarded work; the owner must discard commands after Stop/replacement.
        std::function<void(PluginCommand)> command;
        std::function<void(std::string level, std::string message)> log;
        std::function<void(bool running, std::string message)> state;
    };
    // Modules use canonical package-relative keys, exact filenames, no automatic
    // extensions; at most 128 modules, 1 MiB each and 8 MiB including the entry.
    // Caller reads the entire snapshot before asking the user to run it.
    static std::unique_ptr<PluginRuntime> Start(std::string source,
        std::string pluginName, bool allowOsd, Callbacks callbacks,
        std::string initialStatusJson = "{}",
        std::string entryRelativePath = "main.js",
        std::map<std::string, std::string> modules = {});
    ~PluginRuntime();
    PluginRuntime(PluginRuntime const&) = delete;
    PluginRuntime& operator=(PluginRuntime const&) = delete;

    // UI-owned observed status, never a synchronous call into PlayerSession.
    // A status-only update coalesces. An event keeps its own status snapshot;
    // callback receives one JSON value argument. A full event queue is rejected
    // (false), never silently presented as a reliable event delivery.
    bool Update(std::string statusJson, std::string eventName = {},
        std::string eventArgumentJson = "null");
    // Nonblocking cancellation; deadline handler interrupts active JS. Destructor
    // joins worker; worker never waits for UI. Native caller callbacks must obey
    // the above contract for shutdown to remain bounded.
    void Stop() noexcept;
    bool Running() const noexcept;
    static bool SupportsEvent(std::string_view name) noexcept;
private:
    struct Impl;
    explicit PluginRuntime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}

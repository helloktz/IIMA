// IINA JavascriptPluginInstance / JavascriptPolyfill / JavascriptAPICore /
// JavascriptAPIEvent / JavascriptAPIConsole / JavascriptAPIMenu Windows adaptation.
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
// No QuickJS std/os modules, arbitrary filesystem, network, process, native module
// loader, global controller or HTML capabilities are installed. Private @data/@tmp
// text-file operations are delegated to the bounded native host. CommonJS require
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
        // Explicit preferences.sync only. Called synchronously on the engine
        // worker, never UI; return means durable host save succeeded. Throw to
        // report failure to JavaScript. Do not wait for UI or reenter runtime.
        std::function<void(std::string json)> preferencesSync;
        // Synchronous, worker-only private file operation. Arguments and return
        // are bounded JSON. The host validates paths/quotas and throws on errors;
        // it must not wait for UI or reenter this runtime. Native disk I/O cannot
        // be interrupted by the JavaScript deadline. No arbitrary paths allowed.
        std::function<std::string(std::string operation, std::string argumentsJson)> fileOperation;
        // Best-effort host cleanup on worker exit, after all JS is disposed.
        // Must not reenter runtime or wait for UI. Destructor itself does no I/O.
        std::function<void()> cleanup;
        // Immutable tree snapshot {revision,items}; coalesced once per JS turn.
        // Enqueue UI work and return promptly; do not invoke native UI here.
        std::function<void(std::string json)> menuChanged;
    };
    // Modules use canonical package-relative keys, exact filenames, no automatic
    // extensions; at most 128 modules, 1 MiB each and 8 MiB including the entry.
    // Caller reads the entire snapshot before asking the user to run it.
    static std::unique_ptr<PluginRuntime> Start(std::string source,
        std::string pluginName, bool allowOsd, Callbacks callbacks,
        std::string initialStatusJson = "{}",
        std::string entryRelativePath = "main.js",
        std::map<std::string, std::string> modules = {},
        std::string defaultsJson = "{}", std::string storedJson = "{}",
        std::string initialKeyBindingsJson = "{}");
    ~PluginRuntime();
    PluginRuntime(PluginRuntime const&) = delete;
    PluginRuntime& operator=(PluginRuntime const&) = delete;

    // UI-owned observed status, never a synchronous call into PlayerSession.
    // A status-only update coalesces. An event keeps its own status snapshot;
    // callback receives one JSON value argument. A full event queue is rejected
    // (false), never silently presented as a reliable event delivery.
    bool Update(std::string statusJson, std::string eventName = {},
        std::string eventArgumentJson = "null");
    // Queue a click from the exact rendered menu revision. Worker rechecks
    // revision, reachability, enabled ancestors and callback identity.
    bool InvokeMenu(std::string id, std::string revision);
    // UI publishes active key bindings, not arbitrary files. 1024 entries and
    // 256 KiB maximum are enforced by the host projection; replacement coalesces.
    bool UpdateKeyBindings(std::string json);
    // Asynchronous observation AFTER native handling; cannot consume or suppress
    // OS/default behavior. Caller excludes text/password editors and stale owners.
    // Callback receives {x:null,y:null,isRepeat}; no global keyboard hook.
    bool PublishKeyInput(std::string normalizedKey, bool down, bool isRepeat);
    // Window-local video mouse observation. button is *mouse/*rightMouse/
    // *otherMouse; event is down/up/drag. x/y are root logical pixels, bottom-left.
    // Caller excludes touch, pen and editor/control interactions, and bounds drag
    // frequency; callback cannot cancel default native behavior.
    bool PublishMouseInput(std::string button, std::string event, double x, double y,
        unsigned clickCount, double pressure);
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

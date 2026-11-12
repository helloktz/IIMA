// IINA JavascriptPluginInstance / JavascriptPolyfill / JavascriptAPIEvent /
// JavascriptAPICore / JavascriptAPIConsole / JavascriptAPIPreferences / JavascriptAPIMenu /
// JavascriptAPIFile / JavascriptAPIHttp adaptation,
// not full API compatibility.
// Copyright (c) 2018-2020 lhc and IINA contributors. GPL-3.0.
#include "pch.h"
#include "PluginRuntime.h"
#include "PluginBootstrap.h"
#include "../../IIMA.Core/Plugins/PluginModulePath.h"
#include "../../IIMA.Core/Input/KeyMapping.h"
#include <quickjs.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace iima::plugins {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t MaxSource = 1024 * 1024;
constexpr std::size_t MaxModuleBytes = 8 * 1024 * 1024;
constexpr std::size_t MaxModules = 128;
constexpr std::size_t MaxModuleDepth = 32;
constexpr std::size_t MaxJson = 64 * 1024;
constexpr std::size_t MaxHttpRequest = 128 * 1024;
constexpr std::size_t MaxHttpResult = 16 * 1024 * 1024;
constexpr std::size_t MaxPendingHttp = 4;
constexpr std::size_t MaxFileJson = 6 * 1024 * 1024 + 8192; // escaped 1 MiB UTF-8 + envelope
constexpr std::size_t MaxBindingsJson = 256 * 1024;
constexpr std::size_t MaxEventBytes = 2 * 1024 * 1024;
constexpr std::size_t MaxEvents = 128;
constexpr std::size_t MaxListeners = 256;
constexpr std::size_t MaxTimers = 128;
constexpr std::array EventNames{
    std::string_view("iina.file-loaded"), std::string_view("iina.file-started"),
    std::string_view("iina.music-mode.changed"), std::string_view("mpv.pause.changed"),
    std::string_view("mpv.time-pos.changed"), std::string_view("mpv.duration.changed"),
    std::string_view("mpv.speed.changed"), std::string_view("mpv.volume.changed"),
    std::string_view("mpv.mute.changed"), std::string_view("mpv.audio-delay.changed"),
    std::string_view("mpv.sub-delay.changed")
};
std::size_t Utf8PrefixSize(std::string_view value, std::size_t maximum) noexcept {
    auto size = std::min(value.size(), maximum);
    if (size < value.size()) {
        // Do not cut a multi-byte codepoint when limiting exception text.
        while (size && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80) --size;
    }
    return size;
}
std::string BoundedUtf8(std::string_view value, std::size_t maximum) {
    return std::string(value.substr(0, Utf8PrefixSize(value, maximum)));
}
JSValue NativeException(JSContext* context, std::exception const& error, bool typeError) noexcept {
    auto text = error.what();
    auto size = static_cast<int>(Utf8PrefixSize(text, 4096));
    return typeError ? JS_ThrowTypeError(context, "%.*s", size, text)
        : JS_ThrowInternalError(context, "%.*s", size, text);
}
struct Value {
    JSContext* context{};
    JSValue value{JS_UNDEFINED};
    Value(JSContext* context, JSValue value) : context(context), value(value) {}
    ~Value() { JS_FreeValue(context, value); }
    Value(Value const&) = delete;
    Value& operator=(Value const&) = delete;
};
}

struct PluginRuntime::Impl {
    struct UpdateItem {
        std::string status;
        std::string event;
        std::string argument;
        bool menuClick{};
        bool keyInput{}, keyDown{}, keyRepeat{};
        bool mouseInput{};
        double mouseX{}, mouseY{}, mousePressure{};
        unsigned mouseClicks{};
        // For a click, event carries its item ID and argument its revision;
        // there is no stale media snapshot to overwrite current player status.
        std::size_t Bytes() const noexcept { return status.size() + event.size() + argument.size(); }
    };
    struct HttpRequest { std::uint64_t id{}; std::string json; };
    struct HttpCompletion { std::uint64_t id{}; std::string json; bool transportFailure{}; };
    std::atomic_bool stop{}, running{};
    std::mutex mutex;
    std::condition_variable changed, networkChanged;
    std::deque<HttpRequest> networkRequests;
    std::deque<HttpCompletion> networkCompletions;
    std::stop_source networkStop;
    std::jthread networkThread;
    std::deque<UpdateItem> events;
    std::size_t queuedBytes{};
    std::string latestStatus, latestKeyBindings;
    std::uint64_t statusRevision{1};
    std::string source, name, entryPath, defaultsJson, storedJson;
    std::map<std::string, std::string> modules;
    bool allowOsd{};
    Callbacks callbacks;
    std::jthread thread;

    Impl(std::string text, std::string pluginName, bool grant, Callbacks cb, std::string status,
         std::string entry, std::map<std::string, std::string> package,
         std::string defaults, std::string stored, std::string keyBindings)
        : latestStatus(std::move(status)), latestKeyBindings(std::move(keyBindings)), source(std::move(text)), name(std::move(pluginName)),
          entryPath(std::move(entry)), defaultsJson(std::move(defaults)), storedJson(std::move(stored)),
          modules(std::move(package)), allowOsd(grant), callbacks(std::move(cb)) {}
    ~Impl() {
        stop.store(true); networkStop.request_stop();
        changed.notify_all(); networkChanged.notify_all();
        // The public ownership contract prevents destruction from a callback.
        if (thread.joinable()) thread.join();
        if (networkThread.joinable()) networkThread.join();
    }
    void State(bool active, std::string message) noexcept {
        running.store(active);
        try {
            if (message.size() > 8192) message = BoundedUtf8(message, 8192);
            if (callbacks.state) callbacks.state(active, std::move(message));
        }
        catch (...) { stop.store(true); }
    }
    void Report(std::string level, std::string message) noexcept {
        try {
            if (message.size() > 8192) message = BoundedUtf8(message, 8192);
            if (callbacks.log) callbacks.log(std::move(level), std::move(message));
        }
        catch (...) { stop.store(true); }
    }

    // Native transport owns no JS values. At most four requests, including
    // queued/completed work, exist because only the JS promise map admits them.
    void NetworkLoop() noexcept {
        try {
            auto token = networkStop.get_token();
            for (;;) {
                HttpRequest request;
                {
                    std::unique_lock lock(mutex);
                    networkChanged.wait(lock, [&] {
                        return stop.load() || token.stop_requested() || !networkRequests.empty();
                    });
                    if (stop.load() || token.stop_requested()) break;
                    request = std::move(networkRequests.front()); networkRequests.pop_front();
                }
                HttpCompletion completion;
                completion.id = request.id;
                try {
                    completion.json = callbacks.httpRequest(std::move(request.json), token);
                    if (completion.json.empty() || completion.json.size() > MaxHttpResult ||
                        completion.json.find('\0') != completion.json.npos) {
                        completion.json.clear(); completion.transportFailure = true;
                    }
                } catch (...) {
                    // Never expose an exception containing a URL, credential,
                    // response body or native transport detail to script/logs.
                    completion.transportFailure = true;
                }
                {
                    std::lock_guard lock(mutex);
                    if (stop.load() || token.stop_requested()) break;
                    if (networkCompletions.size() >= MaxPendingHttp)
                        throw std::runtime_error("Plugin HTTP completion queue exceeded its bound.");
                    networkCompletions.push_back(std::move(completion));
                }
                changed.notify_one();
            }
        } catch (...) { stop.store(true); changed.notify_all(); }
    }

    struct Worker {
        struct Listener { std::string event; JSValue callback; };
        struct Timer {
            JSValue callback;
            Clock::time_point due;
            std::chrono::milliseconds interval{};
        };
        enum Function {
            Pause, Resume, StopPlayback, Seek, SeekTo, Speed, Osd,
            Log, Warn, Error, Status, On, Off, Timeout, Interval, ClearTimer, Require,
            PlayChapter, AudioVolume, AudioMuted, AudioDelay, AudioId, SubtitleDelay, SubtitleId, VideoId,
            PreferenceDefaults, PreferenceStored, PreferenceSync, MenuInstall, MenuPublish,
            InputInstall, InputNormalize, InputBindings, MouseInstall, FileOperation, HttpRequestCall, PlaylistCommandCall
        };
        Impl& owner;
        JSRuntime* runtime{};
        JSContext* context{};
        struct HttpPromise { JSValue resolve; JSValue reject; };
        std::map<std::uint64_t, HttpPromise> httpPromises;
        std::map<std::string, Listener> listeners;
        std::map<std::string, Timer> timers;
        std::map<std::string, JSValue> moduleCache;
        std::vector<std::string> moduleStack;
        std::uint64_t nextIdentity{1};
        std::string status;
        JSValue menuDispatch{JS_UNDEFINED};
        JSValue inputDispatch{JS_UNDEFINED};
        JSValue mouseDispatch{JS_UNDEFINED};
        std::string menuRevision, pendingMenu;
        Clock::time_point deadline{Clock::now()};
        Clock::time_point rateWindow{Clock::now()};
        Clock::time_point lastPreferenceSync{};
        Clock::time_point fileRateWindow{Clock::now()};
        unsigned fileCalls{}, fileMutations{};
        unsigned outboundInWindow{}, callbacksInWindow{}, outboundInTurn{};
        bool interrupted{}, failed{};
        std::string failure;

        explicit Worker(Impl& instance) : owner(instance) {
            { std::lock_guard lock(instance.mutex); status = instance.latestStatus; }
            runtime = JS_NewRuntime();
            if (!runtime) throw std::runtime_error("Cannot allocate JavaScript runtime.");
            JS_SetMemoryLimit(runtime, 64 * 1024 * 1024);
            JS_SetMaxStackSize(runtime, 1024 * 1024);
            JS_SetCanBlock(runtime, false); // Atomics.wait must not bypass cancellation.
            JS_SetInterruptHandler(runtime, Interrupt, this);
            context = JS_NewContext(runtime);
            if (!context) { JS_FreeRuntime(runtime); runtime = nullptr; throw std::runtime_error("Cannot allocate JavaScript context."); }
            JS_SetContextOpaque(context, this);
            JS_SetHostPromiseRejectionTracker(runtime, Rejected, this);
        }
        ~Worker() {
            for (auto const& [id, promise] : httpPromises) {
                JS_FreeValue(context, promise.resolve); JS_FreeValue(context, promise.reject);
            }
            JS_FreeValue(context, mouseDispatch);
            JS_FreeValue(context, inputDispatch);
            JS_FreeValue(context, menuDispatch);
            for (auto const& [path, exports] : moduleCache) JS_FreeValue(context, exports);
            for (auto const& [id, item] : timers) JS_FreeValue(context, item.callback);
            for (auto const& [id, item] : listeners) JS_FreeValue(context, item.callback);
            if (context) { JS_SetContextOpaque(context, nullptr); JS_FreeContext(context); }
            if (runtime) JS_FreeRuntime(runtime);
        }
        Worker(Worker const&) = delete;
        Worker& operator=(Worker const&) = delete;
        static int Interrupt(JSRuntime*, void* opaque) noexcept {
            auto& self = *static_cast<Worker*>(opaque);
            if (self.owner.stop.load() || Clock::now() >= self.deadline) {
                self.interrupted = true;
                return 1;
            }
            return 0;
        }
        static void Rejected(JSContext*, JSValueConst, JSValueConst, bool handled, void* opaque) noexcept {
            auto& self = *static_cast<Worker*>(opaque);
            // Do not invoke arbitrary user toString/JSON from a rejection hook.
            try {
                if (!handled && self.TakeOutbound()) self.owner.Report("error", "Unhandled plugin promise rejection.");
            } catch (...) { self.owner.stop.store(true); }
        }
        void RefreshRateWindow() noexcept {
            auto now = Clock::now();
            if (now - rateWindow >= std::chrono::seconds(1)) {
                rateWindow = now; outboundInWindow = 0; callbacksInWindow = 0;
            }
        }
        bool TakeOutbound() noexcept {
            RefreshRateWindow();
            if (owner.stop.load() || outboundInTurn >= 32 || outboundInWindow >= 128) return false;
            ++outboundInTurn; ++outboundInWindow;
            return true;
        }
        bool BeginTurn(bool initialization = false) {
            RefreshRateWindow();
            if (owner.stop.load() || failed) return false;
            if (!initialization && ++callbacksInWindow > 256) {
                Fail("Plugin callback rate exceeded 256 turns per second.");
                return false;
            }
            outboundInTurn = 0;
            interrupted = false;
            deadline = Clock::now() + (initialization ? std::chrono::milliseconds(500) : std::chrono::milliseconds(100));
            return true;
        }
        void Fail(std::string message) {
            if (!failed) { failed = true; failure = std::move(message); }
        }
        std::string ExceptionText() {
            Value exception(context, JS_GetException(context));
            if (interrupted) return owner.stop.load() ? "Plugin stopped." : "Plugin exceeded the execution time budget.";
            // Exception formatting runs under the existing deadline. An exception
            // object's user-defined toString cannot get an unbounded second turn.
            std::size_t size{};
            auto text = JS_ToCStringLen(context, &size, exception.value);
            if (!text) {
                Value ignored(context, JS_GetException(context));
                return "Plugin JavaScript exception.";
            }
            try {
                auto result = BoundedUtf8(std::string_view(text, size), 8192);
                JS_FreeCString(context, text);
                return result;
            } catch (...) { JS_FreeCString(context, text); throw; }
        }
        bool Result(JSValue value) {
            Value result(context, value);
            if (JS_IsException(result.value)) { Fail(ExceptionText()); return false; }
            if (interrupted) { Fail("Plugin execution was interrupted."); return false; }
            return true;
        }
        bool Jobs() {
            // Promise continuations are part of the same turn, not a way to
            // reset the deadline or emission budget via an infinite microtask loop.
            unsigned count{};
            while (!owner.stop.load() && JS_IsJobPending(runtime)) {
                if (++count > 512 || Clock::now() >= deadline) {
                    Fail("Plugin exceeded the promise-job budget."); return false;
                }
                JSContext* jobContext{};
                auto result = JS_ExecutePendingJob(runtime, &jobContext);
                if (result < 0) { Fail(ExceptionText()); return false; }
            }
            if (!failed && !owner.stop.load() && !pendingMenu.empty()) {
                if (!TakeOutbound()) { Fail("Plugin menu publication rate exceeded."); return false; }
                if (!owner.callbacks.menuChanged) { Fail("Plugin native menu host is unavailable."); return false; }
                // One UI publication after all same-turn menu mutations/jobs.
                // Callback must not block or reenter this runtime.
                owner.callbacks.menuChanged(std::exchange(pendingMenu, {}));
            }
            return !failed && !owner.stop.load();
        }
        static std::string String(JSContext* ctx, JSValueConst value, std::size_t maximum) {
            if (!JS_IsString(value)) throw std::invalid_argument("Expected a string.");
            std::size_t size{};
            auto text = JS_ToCStringLen(ctx, &size, value);
            if (!text) throw std::runtime_error("Cannot read JavaScript string.");
            std::string result;
            try {
                if (size > maximum) throw std::invalid_argument("String exceeds the API size limit.");
                result.assign(text, size);
                if (result.find('\0') != result.npos) throw std::invalid_argument("NUL is not accepted by this API.");
            } catch (...) { JS_FreeCString(ctx, text); throw; }
            JS_FreeCString(ctx, text);
            return result;
        }
        static double Number(JSContext* ctx, JSValueConst value) {
            double result{};
            if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &result, value) < 0 || !std::isfinite(result))
                throw std::invalid_argument("Expected a finite number.");
            return result;
        }
        static JSValue Dispatch(JSContext* context, JSValueConst, int argc,
                                JSValueConst* argv, int magic) noexcept {
            auto self = static_cast<Worker*>(JS_GetContextOpaque(context));
            if (!self || self->owner.stop.load()) return JS_ThrowInternalError(context, "Plugin is stopping.");
            try { return self->Call(static_cast<Function>(magic), argc, argv); }
            catch (std::invalid_argument const& e) { return NativeException(context, e, true); }
            catch (std::exception const& e) { return NativeException(context, e, false); }
            catch (...) { return JS_ThrowInternalError(context, "Native plugin API failed."); }
        }
        static JSValue ModuleRequireDispatch(JSContext* context, JSValueConst, int argc,
                JSValueConst* argv, int, JSValueConst* data) noexcept {
            auto self = static_cast<Worker*>(JS_GetContextOpaque(context));
            if (!self || self->owner.stop.load()) return JS_ThrowInternalError(context, "Plugin is stopping.");
            try {
                if (argc < 1) throw std::invalid_argument("require expects a package-relative path.");
                return self->RequireModule(String(context, data[0], 4096), String(context, argv[0], 4096));
            } catch (std::invalid_argument const& e) { return NativeException(context, e, true); }
              catch (std::exception const& e) { return NativeException(context, e, false); }
              catch (...) { return JS_ThrowInternalError(context, "Plugin module evaluation failed."); }
        }
        JSValue RequireModule(std::string const& currentFile, std::string const& request) {
            if (owner.stop.load() || interrupted || Clock::now() >= deadline)
                return JS_ThrowInternalError(context, "Plugin module evaluation was interrupted.");
            auto resolved = ResolvePluginModulePath(currentFile, request);
            if (!resolved) return JS_ThrowTypeError(context, "require path escapes the plugin or is invalid.");
            if (auto cached = moduleCache.find(*resolved); cached != moduleCache.end())
                return JS_DupValue(context, cached->second);
            if (moduleStack.size() >= MaxModuleDepth)
                return JS_ThrowRangeError(context, "Plugin module nesting exceeds 32 files.");
            if (std::find(moduleStack.begin(), moduleStack.end(), *resolved) != moduleStack.end())
                return JS_ThrowTypeError(context, "Circular plugin require is not supported.");
            auto module = owner.modules.find(*resolved);
            if (module == owner.modules.end())
                return JS_ThrowReferenceError(context, "Module is not in the approved source snapshot: %s", resolved->c_str());
            moduleStack.push_back(*resolved);
            struct StackPop {
                std::vector<std::string>& stack;
                ~StackPop() { stack.pop_back(); }
            } pop{moduleStack};
            // IINA evaluateFile(asModule) supplies only `module = {}` and returns
            // module.exports, not the Node.js exports alias or module metadata.
            // Pass a lexical require so module callbacks can safely load siblings
            // after evaluation; IINA's transient currentFile stack cannot do so.
            auto text = "(function(require) {\nconst module = {};\n" + module->second +
                "\n;return module.exports;\n})";
            Value function(context, JS_Eval(context, text.data(), text.size(), resolved->c_str(), JS_EVAL_TYPE_GLOBAL));
            if (JS_IsException(function.value)) return JS_EXCEPTION;
            Value fileName(context, JS_NewStringLen(context, resolved->data(), resolved->size()));
            if (JS_IsException(fileName.value)) return JS_EXCEPTION;
            Value require(context, JS_NewCFunctionData(context, ModuleRequireDispatch, 1, 0, 1, &fileName.value));
            if (JS_IsException(require.value)) return JS_EXCEPTION;
            Value exports(context, JS_Call(context, function.value, JS_UNDEFINED, 1, &require.value));
            if (JS_IsException(exports.value)) return JS_EXCEPTION;
            // Undefined/falsy exports are cached too; do not repeat side effects
            // merely because IINA's old JS truthiness-based cache would miss them.
            auto retained = JS_DupValue(context, exports.value);
            try { moduleCache.emplace(*resolved, retained); }
            catch (...) { JS_FreeValue(context, retained); throw; }
            return JS_DupValue(context, exports.value);
        }
        std::string MediaToken() {
            Value snapshot(context, JS_ParseJSON(context, status.data(), status.size(), "iima-media-identity"));
            if (JS_IsException(snapshot.value)) {
                Value ignored(context, JS_GetException(context));
                throw std::runtime_error("Cannot read host media identity.");
            }
            Value token(context, JS_GetPropertyStr(context, snapshot.value, "_mediaToken"));
            return String(context, token.value, 64);
        }
        JSValue Call(Function function, int argc, JSValueConst* argv) {
            auto argument = [&](int index) -> JSValueConst {
                if (index >= argc) throw std::invalid_argument("Missing API argument.");
                return argv[index];
            };
            if (function == PlaylistCommandCall) {
                auto operation = String(context, argument(0), 16);
                if (operation != "play" && operation != "remove" && operation != "move" &&
                    operation != "next" && operation != "previous")
                    throw std::invalid_argument("Unsupported playlist command.");
                auto arguments = String(context, argument(1), 16 * 1024);
                Value supplied(context, JS_ParseJSON(context, arguments.data(), arguments.size(), "playlist-command"));
                if (JS_IsException(supplied.value)) return JS_EXCEPTION;
                if (!JS_IsArray(supplied.value)) throw std::invalid_argument("Playlist indices must be an array.");
                Value snapshot(context, JS_ParseJSON(context, status.data(), status.size(), "playlist-state"));
                if (JS_IsException(snapshot.value)) return JS_EXCEPTION;
                Value idle(context, JS_GetPropertyStr(context, snapshot.value, "idle"));
                if (!JS_IsBool(idle.value)) throw std::runtime_error("Playlist state is unavailable.");
                if (JS_ToBool(context, idle.value)) return JS_FALSE;
                Value identities(context, JS_GetPropertyStr(context, snapshot.value, "_playlistIds"));
                if (!JS_IsArray(identities.value)) throw std::runtime_error("Playlist identities are unavailable.");
                Value length(context, JS_GetPropertyStr(context, identities.value, "length"));
                auto count = Number(context, length.value);
                if (count < 0 || count > 1024 || std::floor(count) != count)
                    throw std::runtime_error("Playlist identity snapshot exceeds its limit.");
                Value inputLength(context, JS_GetPropertyStr(context, supplied.value, "length"));
                auto inputCount = Number(context, inputLength.value);
                if (inputCount < 0 || inputCount > 1024 || std::floor(inputCount) != inputCount)
                    throw std::invalid_argument("Too many playlist indices.");
                if ((operation == "play" && inputCount != 1) ||
                    (operation == "move" && inputCount != 2) ||
                    ((operation == "next" || operation == "previous") && inputCount != 0))
                    throw std::invalid_argument("Wrong playlist command argument count.");
                std::vector<std::size_t> indices;
                for (unsigned index = 0; index < static_cast<unsigned>(inputCount); ++index) {
                    Value item(context, JS_GetPropertyUint32(context, supplied.value, index));
                    auto value = Number(context, item.value);
                    if (value < 0 || value >= count || std::floor(value) != value) return JS_FALSE;
                    indices.push_back(static_cast<std::size_t>(value));
                }
                if (operation == "move" && indices[0] == indices[1]) return JS_FALSE;
                if (operation == "remove") {
                    std::sort(indices.begin(), indices.end());
                    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
                    if (indices.empty()) return JS_TRUE;
                }
                // Do not accept a JS-provided snapshot identity: only the worker's
                // host-owned ordered ID list can authorize index-based actions.
                PluginCommand command;
                command.name = "playlist." + operation;
                command.mediaToken = MediaToken();
                command.text = "{\"ids\":[";
                for (unsigned index = 0; index < static_cast<unsigned>(count); ++index) {
                    Value item(context, JS_GetPropertyUint32(context, identities.value, index));
                    auto id = String(context, item.value, 20);
                    if (id.empty() || !std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; }))
                        throw std::runtime_error("Invalid playlist identity.");
                    if (index) command.text += ',';
                    command.text += "\"" + id + "\"";
                }
                command.text += ']';
                if (operation == "move") {
                    command.text += ",\"from\":" + std::to_string(indices[0]) +
                        ",\"to\":" + std::to_string(indices[1]);
                } else {
                    command.text += ",\"indices\":[";
                    for (std::size_t index = 0; index < indices.size(); ++index) {
                        if (index) command.text += ',';
                        command.text += std::to_string(indices[index]);
                    }
                    command.text += ']';
                }
                command.text += '}';
                if (!owner.callbacks.command) throw std::runtime_error("Player command host is unavailable.");
                if (!TakeOutbound()) throw std::invalid_argument("Plugin outbound command rate limit exceeded.");
                owner.callbacks.command(std::move(command));
                return JS_TRUE; // queued, never synchronous mpv completion
            }
            if (function == MouseInstall) {
                if (!JS_IsUndefined(mouseDispatch) || !JS_IsFunction(context, argument(0)))
                    throw std::invalid_argument("Mouse dispatcher must be installed once with a function.");
                mouseDispatch = JS_DupValue(context, argv[0]);
                return JS_UNDEFINED;
            }
            if (function == InputInstall) {
                if (!JS_IsUndefined(inputDispatch) || !JS_IsFunction(context, argument(0)))
                    throw std::invalid_argument("Input dispatcher must be installed once with a function.");
                inputDispatch = JS_DupValue(context, argv[0]);
                return JS_UNDEFINED;
            }
            if (function == InputNormalize) {
                auto key = iima::NormalizeMpvKey(String(context, argument(0), 256));
                if (key.empty() || key.size() > 256) throw std::invalid_argument("Invalid normalized input key.");
                return JS_NewStringLen(context, key.data(), key.size());
            }
            if (function == InputBindings) {
                std::string json;
                { std::lock_guard lock(owner.mutex); json = owner.latestKeyBindings; }
                // Parse a fresh independent tree on each call. Plugins cannot
                // mutate the host snapshot through returned objects.
                return JS_ParseJSON(context, json.data(), json.size(), "iima-key-bindings");
            }
            if (function == MenuInstall) {
                if (!JS_IsUndefined(menuDispatch)) throw std::invalid_argument("Plugin menu dispatcher is already installed.");
                if (!JS_IsFunction(context, argument(0))) throw std::invalid_argument("Menu dispatcher must be a function.");
                menuDispatch = JS_DupValue(context, argv[0]);
                return JS_UNDEFINED;
            }
            if (function == MenuPublish) {
                auto json = String(context, argument(0), MaxJson);
                auto revision = String(context, argument(1), 128);
                if (revision.empty() || revision.find_first_not_of("0123456789") != revision.npos)
                    throw std::invalid_argument("Invalid plugin menu revision.");
                if (!owner.callbacks.menuChanged) throw std::runtime_error("Plugin native menu host is unavailable.");
                menuRevision = std::move(revision);
                pendingMenu = std::move(json);
                return JS_UNDEFINED;
            }
            if (function == PreferenceDefaults || function == PreferenceStored) {
                auto const& json = function == PreferenceDefaults ? owner.defaultsJson : owner.storedJson;
                return JS_ParseJSON(context, json.data(), json.size(), "iima-plugin-preferences");
            }
            if (function == PreferenceSync) {
                auto json = String(context, argument(0), MaxJson);
                Value parsed(context, JS_ParseJSON(context, json.data(), json.size(), "iima-plugin-preferences"));
                if (JS_IsException(parsed.value)) return JS_EXCEPTION;
                if (!JS_IsObject(parsed.value) || JS_IsArray(parsed.value))
                    throw std::invalid_argument("Plugin preferences must be a JSON object.");
                if (!owner.callbacks.preferencesSync)
                    throw std::runtime_error("Plugin preference storage is unavailable.");
                if (!TakeOutbound()) throw std::invalid_argument("Plugin preference sync rate limit exceeded.");
                auto now = Clock::now();
                if (lastPreferenceSync != Clock::time_point{} && now - lastPreferenceSync < std::chrono::milliseconds(250))
                    throw std::invalid_argument("Plugin preferences.sync is limited to four calls per second.");
                lastPreferenceSync = now;
                // Native I/O cannot be interrupted by the JS deadline. Exclude
                // precisely this host call's elapsed time on success or failure,
                // not subsequent JS work. Stop remains visible to Interrupt.
                struct NativeCallBudget {
                    Clock::time_point& deadline;
                    Clock::time_point started{Clock::now()};
                    ~NativeCallBudget() { deadline += Clock::now() - started; }
                } budget{deadline};
                owner.callbacks.preferencesSync(std::move(json));
                return JS_UNDEFINED;
            }
            if (function == HttpRequestCall) {
                auto json = String(context, argument(0), MaxHttpRequest);
                if (!owner.callbacks.httpRequest) throw std::runtime_error("Plugin HTTP permission is unavailable.");
                if (httpPromises.size() >= MaxPendingHttp || !TakeOutbound())
                    throw std::invalid_argument("Plugin HTTP pending/rate limit exceeded.");
                if (nextIdentity == std::numeric_limits<std::uint64_t>::max())
                    throw std::runtime_error("Plugin request identity exhausted.");
                auto id = nextIdentity++;
                JSValue resolving[2]{JS_UNDEFINED, JS_UNDEFINED};
                Value promise(context, JS_NewPromiseCapability(context, resolving));
                Value resolve(context, resolving[0]), reject(context, resolving[1]);
                if (JS_IsException(promise.value)) return JS_EXCEPTION;
                // Retained callbacks never leave this JS worker.
                auto pair = HttpPromise{JS_DupValue(context, resolve.value), JS_DupValue(context, reject.value)};
                try { httpPromises.emplace(id, pair); }
                catch (...) { JS_FreeValue(context, pair.resolve); JS_FreeValue(context, pair.reject); throw; }
                try {
                    std::lock_guard lock(owner.mutex);
                    if (owner.stop.load() || owner.networkStop.stop_requested())
                        throw std::runtime_error("Plugin stopped.");
                    owner.networkRequests.push_back({id, std::move(json)});
                } catch (...) {
                    JS_FreeValue(context, pair.resolve); JS_FreeValue(context, pair.reject);
                    httpPromises.erase(id); throw;
                }
                owner.networkChanged.notify_one();
                return JS_DupValue(context, promise.value);
            }
            if (function == FileOperation) {
                auto operation = String(context, argument(0), 16);
                bool mutation = operation == "write" || operation == "delete" || operation == "move";
                if (!mutation && operation != "read" && operation != "exists" && operation != "list")
                    throw std::invalid_argument("Unsupported plugin file operation.");
                auto json = String(context, argument(1), MaxFileJson);
                if (!owner.callbacks.fileOperation)
                    throw std::runtime_error("Plugin private file storage is unavailable.");
                if (owner.stop.load()) throw std::runtime_error("Plugin stopped.");
                auto now = Clock::now();
                if (now - fileRateWindow >= std::chrono::seconds(1)) {
                    fileRateWindow = now; fileCalls = 0; fileMutations = 0;
                }
                if (fileCalls >= 32 || (mutation && fileMutations >= 4) || !TakeOutbound())
                    throw std::invalid_argument("Plugin file operation rate limit exceeded.");
                ++fileCalls;
                if (mutation) ++fileMutations;
                // Same synchronous semantics as IINA's Foundation file APIs.
                // Native time is excluded from JS work budget, but cancellation
                // is checked before/after the call and no further JS is admitted.
                std::string result;
                {
                    struct NativeCallBudget {
                        Clock::time_point& deadline;
                        Clock::time_point started{Clock::now()};
                        ~NativeCallBudget() { deadline += Clock::now() - started; }
                    } budget{deadline};
                    result = owner.callbacks.fileOperation(std::move(operation), std::move(json));
                }
                if (owner.stop.load()) throw std::runtime_error("Plugin stopped during native file operation.");
                if (result.empty() || result.size() > MaxFileJson || result.find('\0') != result.npos)
                    throw std::runtime_error("Invalid or oversized private file result.");
                return JS_ParseJSON(context, result.data(), result.size(), "iima-private-file-result");
            }
            if (function == Require) return RequireModule(owner.entryPath, String(context, argument(0), 4096));
            if (function == Status) {
                // Parse anew: JavaScript never holds the host's mutable snapshot
                // and changes to a returned object cannot mutate PlayerSession.
                return JS_ParseJSON(context, status.data(), status.size(), "iima-observed-status");
            }
            if (function == On) {
                auto event = String(context, argument(0), 128);
                auto callback = argument(1);
                if (!PluginRuntime::SupportsEvent(event)) throw std::invalid_argument("This event is not implemented in IIMA.");
                if (!JS_IsFunction(context, callback)) throw std::invalid_argument("Event callback must be a function.");
                if (listeners.size() >= MaxListeners) throw std::invalid_argument("Too many plugin event listeners.");
                auto id = "listener-" + std::to_string(nextIdentity++);
                auto retained = JS_DupValue(context, callback);
                try { listeners.emplace(id, Listener{std::move(event), retained}); }
                catch (...) { JS_FreeValue(context, retained); throw; }
                return JS_NewStringLen(context, id.data(), id.size());
            }
            if (function == Off) {
                auto event = String(context, argument(0), 128);
                auto id = String(context, argument(1), 64);
                auto found = listeners.find(id);
                if (found != listeners.end() && found->second.event == event) {
                    JS_FreeValue(context, found->second.callback); listeners.erase(found);
                }
                return JS_UNDEFINED;
            }
            if (function == Timeout || function == Interval) {
                auto callback = argument(0);
                if (!JS_IsFunction(context, callback)) throw std::invalid_argument("Timer callback must be a function; script strings are not supported.");
                auto milliseconds = Number(context, argument(1));
                if (milliseconds < 0 || milliseconds > 86400000) throw std::invalid_argument("Timer delay must be between zero and one day.");
                if (timers.size() >= MaxTimers) throw std::invalid_argument("Too many plugin timers.");
                auto delay = std::chrono::milliseconds(static_cast<std::int64_t>(std::ceil(milliseconds)));
                delay = std::max(delay, std::chrono::milliseconds(function == Interval ? 50 : 16));
                auto id = "timer-" + std::to_string(nextIdentity++);
                auto retained = JS_DupValue(context, callback);
                try { timers.emplace(id, Timer{retained, Clock::now() + delay,
                    function == Interval ? delay : std::chrono::milliseconds::zero()}); }
                catch (...) { JS_FreeValue(context, retained); throw; }
                return JS_NewStringLen(context, id.data(), id.size());
            }
            if (function == ClearTimer) {
                auto id = String(context, argument(0), 64);
                auto found = timers.find(id);
                if (found != timers.end()) { JS_FreeValue(context, found->second.callback); timers.erase(found); }
                return JS_UNDEFINED;
            }
            if (function == Log || function == Warn || function == Error) {
                auto text = String(context, argument(0), 8192);
                if (!TakeOutbound()) throw std::invalid_argument("Plugin outbound message rate limit exceeded.");
                owner.Report(function == Log ? "debug" : function == Warn ? "warning" : "error", std::move(text));
                return JS_UNDEFINED;
            }
            PluginCommand command;
            switch (function) {
            case PlayChapter:
                command.name = "playChapter"; command.number = Number(context, argument(0));
                if (command.number < 0 || command.number > 2147483647.0 || std::floor(command.number) != command.number)
                    throw std::invalid_argument("Chapter index must be a non-negative 32-bit integer.");
                break;
            case AudioVolume:
                command.name = "audio.volume"; command.number = Number(context, argument(0));
                if (command.number < 0 || command.number > 1000) throw std::invalid_argument("Volume must be between zero and 1000; the player also clamps to its configured maximum.");
                break;
            case AudioMuted:
                command.name = "audio.muted";
                if (!JS_IsBool(argument(0))) throw std::invalid_argument("Mute must be a boolean.");
                command.flag = JS_ToBool(context, argv[0]) != 0;
                break;
            case AudioDelay: case SubtitleDelay:
                command.name = function == AudioDelay ? "audio.delay" : "subtitle.delay";
                command.number = Number(context, argument(0));
                if (std::abs(command.number) > 3600) throw std::invalid_argument("Plugin track delay must be within one hour.");
                break;
            case AudioId: case SubtitleId: case VideoId:
                command.name = function == AudioId ? "audio.id" : function == SubtitleId ? "subtitle.id" : "video.id";
                command.number = Number(context, argument(0));
                if (command.number < 0 || command.number > 9007199254740991.0 || std::floor(command.number) != command.number)
                    throw std::invalid_argument("Track ID must be a non-negative safe integer; zero disables the track.");
                break;
            case Pause: command.name = "pause"; break;
            case Resume: command.name = "resume"; break;
            case StopPlayback: command.name = "stop"; break;
            case Seek:
                command.name = "seek"; command.number = Number(context, argument(0));
                if (std::abs(command.number) > 3155760000.0) throw std::invalid_argument("Seek interval is out of range.");
                if (!JS_IsBool(argument(1))) throw std::invalid_argument("Exact seek must be boolean.");
                command.flag = JS_ToBool(context, argv[1]) != 0;
                break;
            case SeekTo:
                command.name = "seekTo"; command.number = Number(context, argument(0));
                if (command.number < 0 || command.number > 3155760000.0) throw std::invalid_argument("Seek position is out of range.");
                break;
            case Speed:
                command.name = "setSpeed"; command.number = Number(context, argument(0));
                if (command.number < 0.01 || command.number > 100) throw std::invalid_argument("Speed is outside the supported range.");
                break;
            case Osd:
                if (!owner.allowOsd) throw std::invalid_argument("show-osd permission has not been granted.");
                command.name = "osd"; command.text = String(context, argument(0), 4096);
                break;
            default: throw std::invalid_argument("Plugin API is not implemented.");
            }
            if (function == PlayChapter || function == AudioId || function == SubtitleId || function == VideoId)
                command.mediaToken = MediaToken();
            if (!owner.callbacks.command) throw std::runtime_error("Player command host is unavailable.");
            if (!TakeOutbound()) throw std::invalid_argument("Plugin outbound command rate limit exceeded.");
            owner.callbacks.command(std::move(command));
            return JS_UNDEFINED; // Enqueued is not an mpv completion acknowledgement.
        }
        bool SetFunction(JSValueConst target, char const* name, int arity, Function magic) {
            auto function = JS_NewCFunctionMagic(context, Dispatch, name, arity, JS_CFUNC_generic_magic, magic);
            if (JS_IsException(function)) { Fail(ExceptionText()); return false; }
            if (JS_SetPropertyStr(context, target, name, function) < 0) { Fail(ExceptionText()); return false; }
            return true;
        }
        bool Install() {
            if (!BeginTurn(true)) return false;
            Value global(context, JS_GetGlobalObject(context));
            Value host(context, JS_NewObject(context));
            if (JS_IsException(host.value)) { Fail(ExceptionText()); return false; }
            constexpr std::array<std::tuple<char const*, int, Function>, 37> definitions{{
                {"pause", 0, Pause}, {"resume", 0, Resume}, {"stop", 0, StopPlayback},
                {"seek", 2, Seek}, {"seekTo", 1, SeekTo}, {"setSpeed", 1, Speed}, {"osd", 1, Osd},
                {"log", 1, Log}, {"warn", 1, Warn}, {"error", 1, Error}, {"status", 0, Status},
                {"on", 2, On}, {"off", 2, Off}, {"timeout", 2, Timeout}, {"interval", 2, Interval},
                {"clearTimer", 1, ClearTimer}, {"require", 1, Require},
                {"playChapter", 1, PlayChapter}, {"audioVolume", 1, AudioVolume},
                {"audioMuted", 1, AudioMuted}, {"audioDelay", 1, AudioDelay},
                {"audioId", 1, AudioId}, {"subtitleDelay", 1, SubtitleDelay},
                {"subtitleId", 1, SubtitleId}, {"videoId", 1, VideoId},
                {"preferenceDefaults", 0, PreferenceDefaults}, {"preferenceStored", 0, PreferenceStored},
                {"preferenceSync", 1, PreferenceSync}, {"menuInstall", 1, MenuInstall}, {"menuPublish", 2, MenuPublish},
                {"inputInstall", 1, InputInstall}, {"inputNormalize", 1, InputNormalize}, {"inputBindings", 0, InputBindings},
                {"mouseInstall", 1, MouseInstall}, {"fileOperation", 2, FileOperation}, {"httpRequest", 1, HttpRequestCall},
                {"playlistCommand", 2, PlaylistCommandCall}
            }};
            for (auto const& [name, arity, function] : definitions) {
                if (!SetFunction(host.value, name, arity, function)) return false;
            }
            if (JS_SetPropertyStr(context, global.value, "__iimaHost", JS_DupValue(context, host.value)) < 0) {
                Fail(ExceptionText()); return false;
            }
            if (!Result(JS_Eval(context, PluginBootstrap, sizeof(PluginBootstrap) - 1,
                "iima-plugin-bootstrap", JS_EVAL_TYPE_GLOBAL))) return false;
            if (!ValidateStatus()) return false;
            moduleStack.push_back(owner.entryPath);
            auto result = JS_Eval(context, owner.source.data(), owner.source.size(),
                owner.entryPath.c_str(), JS_EVAL_TYPE_GLOBAL);
            moduleStack.pop_back();
            if (!Result(result)) return false;
            owner.source.clear();
            return Jobs();
        }
        bool ValidateStatus() {
            Value parsed(context, JS_ParseJSON(context, status.data(), status.size(), "iima-observed-status"));
            if (JS_IsException(parsed.value)) { Fail("Invalid host status JSON."); Value ignored(context, JS_GetException(context)); return false; }
            if (!JS_IsObject(parsed.value) || JS_IsArray(parsed.value)) { Fail("Host status must be a JSON object."); return false; }
            return true;
        }
        void Event(UpdateItem const& item) {
            if (!BeginTurn()) return;
            status = item.status;
            if (!ValidateStatus()) return;
            Value argument(context, JS_ParseJSON(context, item.argument.data(), item.argument.size(), "iima-event-argument"));
            if (JS_IsException(argument.value)) { Fail(ExceptionText()); return; }
            // Copy IDs, not map iterators: callbacks may register/remove listeners.
            // Newly registered listeners never run during the same emission.
            std::vector<std::string> ids;
            for (auto const& [id, listener] : listeners) if (listener.event == item.event) ids.push_back(id);
            for (auto const& id : ids) {
                if (owner.stop.load() || failed) break;
                auto found = listeners.find(id);
                if (found == listeners.end()) continue;
                Value callback(context, JS_DupValue(context, found->second.callback));
                // Each listener gets its own argument value tree; one callback
                // cannot corrupt another callback's observed event payload.
                Value isolated(context, JS_ParseJSON(context, item.argument.data(), item.argument.size(), "iima-event-argument"));
                if (JS_IsException(isolated.value)) { Fail(ExceptionText()); break; }
                if (!Result(JS_Call(context, callback.value, JS_UNDEFINED, 1, &isolated.value))) break;
                if (Clock::now() >= deadline) { Fail("Plugin event handlers exceeded the time budget."); break; }
            }
            if (!failed) Jobs();
        }
        void KeyInput(UpdateItem const& item) {
            if (JS_IsUndefined(inputDispatch) || !BeginTurn()) return;
            status = item.status;
            if (!ValidateStatus()) return;
            Value key(context, JS_NewStringLen(context, item.event.data(), item.event.size()));
            if (JS_IsException(key.value)) { Fail(ExceptionText()); return; }
            JSValueConst args[]{key.value, JS_NewBool(context, item.keyDown), JS_NewBool(context, item.keyRepeat)};
            if (Result(JS_Call(context, inputDispatch, JS_UNDEFINED, 3, args))) Jobs();
        }
        void MouseInput(UpdateItem const& item) {
            if (JS_IsUndefined(mouseDispatch) || !BeginTurn()) return;
            status = item.status;
            if (!ValidateStatus()) return;
            Value button(context, JS_NewStringLen(context, item.event.data(), item.event.size()));
            Value event(context, JS_NewStringLen(context, item.argument.data(), item.argument.size()));
            Value data(context, JS_NewObject(context));
            if (JS_IsException(button.value) || JS_IsException(event.value) || JS_IsException(data.value)) {
                Fail(ExceptionText()); return;
            }
            if (JS_SetPropertyStr(context, data.value, "x", JS_NewFloat64(context, item.mouseX)) < 0 ||
                JS_SetPropertyStr(context, data.value, "y", JS_NewFloat64(context, item.mouseY)) < 0 ||
                JS_SetPropertyStr(context, data.value, "clickCount", JS_NewUint32(context, item.mouseClicks)) < 0 ||
                JS_SetPropertyStr(context, data.value, "pressure", JS_NewFloat64(context, item.mousePressure)) < 0) {
                Fail(ExceptionText()); return;
            }
            JSValueConst args[]{button.value, event.value, data.value};
            if (Result(JS_Call(context, mouseDispatch, JS_UNDEFINED, 3, args))) Jobs();
        }
        void MenuClicked(UpdateItem const& click) {
            if (click.argument != menuRevision || JS_IsUndefined(menuDispatch) || !BeginTurn()) return;
            Value id(context, JS_NewStringLen(context, click.event.data(), click.event.size()));
            Value revision(context, JS_NewStringLen(context, click.argument.data(), click.argument.size()));
            if (JS_IsException(id.value) || JS_IsException(revision.value)) { Fail(ExceptionText()); return; }
            JSValueConst arguments[]{id.value, revision.value};
            // The private JS dispatcher checks reachability and enabled parents
            // against live model state, and passes the retained item to action.
            if (Result(JS_Call(context, menuDispatch, JS_UNDEFINED, 2, arguments))) Jobs();
        }
        void HttpFinished(HttpCompletion const& completion) {
            auto found = httpPromises.find(completion.id);
            if (found == httpPromises.end() || !BeginTurn()) return;
            Value resolve(context, found->second.resolve), reject(context, found->second.reject);
            httpPromises.erase(found); // allow a continuation to enqueue another request
            auto rejectTransport = [&] {
                Value error(context, JS_NewError(context));
                if (JS_IsException(error.value) || JS_SetPropertyStr(context, error.value, "message",
                    JS_NewString(context, "Plugin HTTP transport failed, was refused, or returned an invalid response.")) < 0) {
                    Fail(ExceptionText()); return;
                }
                if (Result(JS_Call(context, reject.value, JS_UNDEFINED, 1, &error.value))) Jobs();
            };
            if (completion.transportFailure) { rejectTransport(); return; }
            Value envelope(context, JS_ParseJSON(context, completion.json.data(), completion.json.size(), "iima-http-result"));
            if (JS_IsException(envelope.value)) {
                Value ignored(context, JS_GetException(context)); rejectTransport(); return;
            }
            if (!JS_IsObject(envelope.value) || JS_IsArray(envelope.value)) { rejectTransport(); return; }
            Value ok(context, JS_GetPropertyStr(context, envelope.value, "ok"));
            Value response(context, JS_GetPropertyStr(context, envelope.value, "response"));
            if (!JS_IsBool(ok.value) || !JS_IsObject(response.value) || JS_IsArray(response.value)) {
                rejectTransport(); return;
            }
            // Like IINA HTTPResult.toDict: failed HTTP status rejects with the
            // same response object, not an Error or an apparently successful body.
            auto callback = JS_ToBool(context, ok.value) ? resolve.value : reject.value;
            if (Result(JS_Call(context, callback, JS_UNDEFINED, 1, &response.value))) Jobs();
        }
        void TimerFired(std::string const& id) {
            auto found = timers.find(id);
            if (found == timers.end() || !BeginTurn()) return;
            Value callback(context, JS_DupValue(context, found->second.callback));
            if (found->second.interval.count() == 0) {
                JS_FreeValue(context, found->second.callback); timers.erase(found);
            } else {
                // Skip missed ticks instead of catch-up bursts after a slow script.
                found->second.due = Clock::now() + found->second.interval;
            }
            if (Result(JS_Call(context, callback.value, JS_UNDEFINED, 0, nullptr))) Jobs();
        }
        void Loop() {
            if (!Install()) return;
            owner.State(true, "Plugin entry is running with limited IINA APIs.");
            std::uint64_t observedRevision{};
            while (!owner.stop.load() && !failed) {
                UpdateItem update;
                bool hasEvent{}, hasStatus{}, hasHttp{};
                HttpCompletion httpCompletion;
                std::string dueTimer;
                auto nextDue = Clock::time_point::max();
                for (auto const& [id, timer] : timers) if (timer.due < nextDue) { nextDue = timer.due; dueTimer = id; }
                {
                    std::unique_lock lock(owner.mutex);
                    if (owner.events.empty() && owner.networkCompletions.empty() && owner.statusRevision == observedRevision && Clock::now() < nextDue) {
                        auto ready = [&] {
                            return owner.stop.load() || !owner.events.empty() || !owner.networkCompletions.empty() || owner.statusRevision != observedRevision;
                        };
                        if (nextDue == Clock::time_point::max()) owner.changed.wait(lock, ready);
                        else owner.changed.wait_until(lock, nextDue, ready);
                    }
                    if (owner.stop.load()) break;
                    if (!owner.networkCompletions.empty()) {
                        httpCompletion = std::move(owner.networkCompletions.front());
                        owner.networkCompletions.pop_front(); hasHttp = true;
                    }
                    if (!owner.events.empty()) {
                        update = std::move(owner.events.front()); owner.events.pop_front();
                        owner.queuedBytes -= update.Bytes(); hasEvent = true;
                    } else if (owner.statusRevision != observedRevision) {
                        update.status = owner.latestStatus; observedRevision = owner.statusRevision; hasStatus = true;
                    }
                }
                if (hasEvent) {
                    if (update.mouseInput) MouseInput(update);
                    else if (update.keyInput) KeyInput(update);
                    else if (update.menuClick) MenuClicked(update);
                    else Event(update);
                }
                else if (hasStatus) { status = std::move(update.status); }
                if (hasHttp && !owner.stop.load() && !failed) HttpFinished(httpCompletion);
                // Process at most one due timer per iteration so a flood of
                // inbound status updates cannot permanently starve timers.
                if (!owner.stop.load() && !failed && !dueTimer.empty() && nextDue <= Clock::now()) TimerFired(dueTimer);
            }
        }
    };
    void Run() noexcept {
        std::string finalMessage = "Plugin stopped.";
        try {
            Worker worker(*this);
            worker.Loop();
            if (worker.failed) {
                finalMessage = worker.failure;
                if (!stop.load()) Report("error", finalMessage);
            }
        } catch (std::exception const& e) { finalMessage = e.what(); }
          catch (...) { finalMessage = "Plugin worker failed."; }
        stop.store(true); networkStop.request_stop(); networkChanged.notify_all();
        if (networkThread.joinable()) networkThread.join();
        { std::lock_guard lock(mutex); networkRequests.clear(); networkCompletions.clear(); }
        try { if (callbacks.cleanup) callbacks.cleanup(); } catch (...) { /* best-effort private temporary cleanup */ }
        { std::lock_guard lock(mutex); events.clear(); queuedBytes = 0; latestStatus.clear(); }
        State(false, std::move(finalMessage));
    }
};

bool PluginRuntime::SupportsEvent(std::string_view name) noexcept {
    return std::find(EventNames.begin(), EventNames.end(), name) != EventNames.end();
}
PluginRuntime::PluginRuntime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
std::unique_ptr<PluginRuntime> PluginRuntime::Start(std::string source, std::string pluginName,
    bool allowOsd, Callbacks callbacks, std::string initialStatusJson,
    std::string entryRelativePath, std::map<std::string, std::string> modules,
    std::string defaultsJson, std::string storedJson, std::string initialKeyBindingsJson) {
    if (source.empty() || source.size() > MaxSource || source.find('\0') != source.npos)
        throw std::invalid_argument("Plugin entry must contain 1 byte to 1 MiB of UTF-8 source without NUL.");
    if (pluginName.size() > 512 || initialStatusJson.empty() || initialStatusJson.size() > MaxJson)
        throw std::invalid_argument("Plugin name or initial status exceeds limits.");
    if (defaultsJson.empty() || storedJson.empty() || defaultsJson.size() > MaxJson || storedJson.size() > MaxJson)
        throw std::invalid_argument("Plugin preference documents must be JSON objects no larger than 64 KiB.");
    if (initialKeyBindingsJson.empty() || initialKeyBindingsJson.size() > MaxBindingsJson)
        throw std::invalid_argument("Plugin key bindings snapshot exceeds 256 KiB.");
    auto entry = ResolvePluginModulePath("__iima_root__.js", entryRelativePath);
    if (!entry || *entry != entryRelativePath) throw std::invalid_argument("Plugin entry path must be canonical and package-relative.");
    if (modules.size() > MaxModules) throw std::invalid_argument("Plugin module snapshot has too many files.");
    auto bytes = source.size();
    for (auto const& [path, text] : modules) {
        auto normalized = ResolvePluginModulePath("__iima_root__.js", path);
        if (!normalized || *normalized != path || path == *entry)
            throw std::invalid_argument("Plugin module path is invalid or duplicates the entry.");
        if (text.size() > MaxSource || text.find('\0') != text.npos || text.size() > MaxModuleBytes - bytes)
            throw std::invalid_argument("Plugin source snapshot exceeds the size limit or contains NUL.");
        bytes += text.size();
    }
    auto instance = std::unique_ptr<PluginRuntime>(new PluginRuntime(std::make_unique<Impl>(
        std::move(source), std::move(pluginName), allowOsd, std::move(callbacks), std::move(initialStatusJson),
        std::move(entryRelativePath), std::move(modules), std::move(defaultsJson), std::move(storedJson), std::move(initialKeyBindingsJson))));
    auto impl = instance->impl_.get();
    if (impl->callbacks.httpRequest) impl->networkThread = std::jthread([impl] { impl->NetworkLoop(); });
    impl->thread = std::jthread([impl] { impl->Run(); });
    return instance;
}
PluginRuntime::~PluginRuntime() = default;
void PluginRuntime::Stop() noexcept {
    impl_->stop.store(true); impl_->networkStop.request_stop();
    impl_->changed.notify_all(); impl_->networkChanged.notify_all();
}
bool PluginRuntime::Running() const noexcept { return impl_->running.load(); }
bool PluginRuntime::InvokeMenu(std::string id, std::string revision) {
    if (impl_->stop.load() || id.empty() || id.size() > 128 || revision.empty() || revision.size() > 128 ||
        id.find('\0') != id.npos || revision.find_first_not_of("0123456789") != revision.npos) return false;
    std::lock_guard lock(impl_->mutex);
    auto bytes = id.size() + revision.size();
    if (impl_->stop.load() || impl_->events.size() >= MaxEvents || bytes > MaxEventBytes - impl_->queuedBytes) return false;
    impl_->events.push_back({{}, std::move(id), std::move(revision), true});
    impl_->queuedBytes += bytes;
    impl_->changed.notify_one();
    return true;
}
bool PluginRuntime::UpdateKeyBindings(std::string json) {
    if (json.empty() || json.size() > MaxBindingsJson || impl_->stop.load()) return false;
    std::lock_guard lock(impl_->mutex);
    if (impl_->stop.load()) return false;
    impl_->latestKeyBindings = std::move(json);
    return true;
}
bool PluginRuntime::PublishKeyInput(std::string key, bool down, bool isRepeat) {
    if (key.empty() || key.size() > 256 || key.find('\0') != key.npos || impl_->stop.load()) return false;
    std::lock_guard lock(impl_->mutex);
    auto bytes = key.size() + impl_->latestStatus.size();
    if (impl_->stop.load() || impl_->events.size() >= MaxEvents || bytes > MaxEventBytes - impl_->queuedBytes) return false;
    Impl::UpdateItem item;
    item.status = impl_->latestStatus;
    item.event = std::move(key);
    item.keyInput = true; item.keyDown = down; item.keyRepeat = isRepeat;
    impl_->events.push_back(std::move(item));
    impl_->queuedBytes += bytes;
    impl_->changed.notify_one();
    return true;
}
bool PluginRuntime::PublishMouseInput(std::string button, std::string event,
    double x, double y, unsigned clickCount, double pressure) {
    if ((button != "*mouse" && button != "*rightMouse" && button != "*otherMouse") ||
        (event != "down" && event != "up" && event != "drag") ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(pressure) ||
        std::abs(x) > 1000000 || std::abs(y) > 1000000 || pressure < 0 || pressure > 1 || impl_->stop.load()) return false;
    std::lock_guard lock(impl_->mutex);
    auto bytes = button.size() + event.size() + impl_->latestStatus.size();
    if (impl_->stop.load() || impl_->events.size() >= MaxEvents || bytes > MaxEventBytes - impl_->queuedBytes) return false;
    Impl::UpdateItem item;
    item.status = impl_->latestStatus;
    item.event = std::move(button); item.argument = std::move(event);
    item.mouseInput = true; item.mouseX = x; item.mouseY = y;
    item.mouseClicks = clickCount; item.mousePressure = pressure;
    impl_->events.push_back(std::move(item));
    impl_->queuedBytes += bytes;
    impl_->changed.notify_one();
    return true;
}
bool PluginRuntime::Update(std::string statusJson, std::string eventName, std::string eventArgumentJson) {
    if (impl_->stop.load() || statusJson.empty() || statusJson.size() > MaxJson ||
        eventArgumentJson.size() > MaxJson || (!eventName.empty() && !SupportsEvent(eventName))) return false;
    std::lock_guard lock(impl_->mutex);
    if (impl_->stop.load()) return false;
    if (!eventName.empty()) {
        auto bytes = statusJson.size() + eventName.size() + eventArgumentJson.size();
        if (impl_->events.size() >= MaxEvents || bytes > MaxEventBytes - impl_->queuedBytes) return false;
        impl_->events.push_back({statusJson, std::move(eventName), std::move(eventArgumentJson)});
        impl_->queuedBytes += bytes;
    }
    impl_->latestStatus = std::move(statusJson);
    ++impl_->statusRevision;
    impl_->changed.notify_one();
    return true;
}
}

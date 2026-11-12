// IINA JavascriptPluginInstance / JavascriptPolyfill / JavascriptAPIEvent /
// JavascriptAPICore / JavascriptAPIConsole adaptation, not full API compatibility.
// Copyright (c) 2018-2020 lhc and IINA contributors. GPL-3.0.
#include "pch.h"
#include "PluginRuntime.h"
#include "PluginBootstrap.h"
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
constexpr std::size_t MaxJson = 64 * 1024;
constexpr std::size_t MaxEventBytes = 2 * 1024 * 1024;
constexpr std::size_t MaxEvents = 128;
constexpr std::size_t MaxListeners = 256;
constexpr std::size_t MaxTimers = 128;
constexpr std::array EventNames{
    std::string_view("iina.file-loaded"), std::string_view("iina.file-started"),
    std::string_view("iina.music-mode.changed"), std::string_view("mpv.pause.changed"),
    std::string_view("mpv.time-pos.changed"), std::string_view("mpv.duration.changed"),
    std::string_view("mpv.speed.changed")
};
std::string BoundedUtf8(std::string_view value, std::size_t maximum) {
    auto size = std::min(value.size(), maximum);
    if (size < value.size()) {
        // Do not cut a multi-byte codepoint when limiting exception text.
        while (size && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80) --size;
    }
    return std::string(value.substr(0, size));
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
        std::size_t Bytes() const noexcept { return status.size() + event.size() + argument.size(); }
    };
    std::atomic_bool stop{}, running{};
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<UpdateItem> events;
    std::size_t queuedBytes{};
    std::string latestStatus;
    std::uint64_t statusRevision{1};
    std::string source, name;
    bool allowOsd{};
    Callbacks callbacks;
    std::jthread thread;

    Impl(std::string text, std::string pluginName, bool grant, Callbacks cb, std::string status)
        : latestStatus(std::move(status)), source(std::move(text)), name(std::move(pluginName)),
          allowOsd(grant), callbacks(std::move(cb)) {}
    ~Impl() {
        stop.store(true); changed.notify_all();
        // The public ownership contract prevents destruction from a callback.
        if (thread.joinable()) thread.join();
    }
    void State(bool active, std::string message) noexcept {
        running.store(active);
        try { if (callbacks.state) callbacks.state(active, std::move(message)); }
        catch (...) { stop.store(true); }
    }
    void Report(std::string level, std::string message) noexcept {
        try {
            if (message.size() > 8192) message = BoundedUtf8(message, 8192);
            if (callbacks.log) callbacks.log(std::move(level), std::move(message));
        }
        catch (...) { stop.store(true); }
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
            Log, Warn, Error, Status, On, Off, Timeout, Interval, ClearTimer
        };
        Impl& owner;
        JSRuntime* runtime{};
        JSContext* context{};
        std::map<std::string, Listener> listeners;
        std::map<std::string, Timer> timers;
        std::uint64_t nextIdentity{1};
        std::string status;
        Clock::time_point deadline{Clock::now()};
        Clock::time_point rateWindow{Clock::now()};
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
            auto result = BoundedUtf8(std::string_view(text, size), 8192);
            JS_FreeCString(context, text);
            return result;
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
            catch (std::invalid_argument const& e) { return JS_ThrowTypeError(context, "%s", e.what()); }
            catch (std::exception const& e) { return JS_ThrowInternalError(context, "%s", e.what()); }
            catch (...) { return JS_ThrowInternalError(context, "Native plugin API failed."); }
        }
        JSValue Call(Function function, int argc, JSValueConst* argv) {
            auto argument = [&](int index) -> JSValueConst {
                if (index >= argc) throw std::invalid_argument("Missing API argument.");
                return argv[index];
            };
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
            constexpr std::array<std::tuple<char const*, int, Function>, 16> definitions{{
                {"pause", 0, Pause}, {"resume", 0, Resume}, {"stop", 0, StopPlayback},
                {"seek", 2, Seek}, {"seekTo", 1, SeekTo}, {"setSpeed", 1, Speed}, {"osd", 1, Osd},
                {"log", 1, Log}, {"warn", 1, Warn}, {"error", 1, Error}, {"status", 0, Status},
                {"on", 2, On}, {"off", 2, Off}, {"timeout", 2, Timeout}, {"interval", 2, Interval},
                {"clearTimer", 1, ClearTimer}
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
            if (!Result(JS_Eval(context, owner.source.data(), owner.source.size(),
                "plugin-entry.js", JS_EVAL_TYPE_GLOBAL))) return false;
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
                bool hasEvent{}, hasStatus{};
                std::string dueTimer;
                auto nextDue = Clock::time_point::max();
                for (auto const& [id, timer] : timers) if (timer.due < nextDue) { nextDue = timer.due; dueTimer = id; }
                {
                    std::unique_lock lock(owner.mutex);
                    if (owner.events.empty() && owner.statusRevision == observedRevision && Clock::now() < nextDue) {
                        auto ready = [&] {
                            return owner.stop.load() || !owner.events.empty() || owner.statusRevision != observedRevision;
                        };
                        if (nextDue == Clock::time_point::max()) owner.changed.wait(lock, ready);
                        else owner.changed.wait_until(lock, nextDue, ready);
                    }
                    if (owner.stop.load()) break;
                    if (!owner.events.empty()) {
                        update = std::move(owner.events.front()); owner.events.pop_front();
                        owner.queuedBytes -= update.Bytes(); hasEvent = true;
                    } else if (owner.statusRevision != observedRevision) {
                        update.status = owner.latestStatus; observedRevision = owner.statusRevision; hasStatus = true;
                    }
                }
                if (hasEvent) Event(update);
                else if (hasStatus) { status = std::move(update.status); }
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
        stop.store(true);
        { std::lock_guard lock(mutex); events.clear(); queuedBytes = 0; latestStatus.clear(); }
        State(false, std::move(finalMessage));
    }
};

bool PluginRuntime::SupportsEvent(std::string_view name) noexcept {
    return std::find(EventNames.begin(), EventNames.end(), name) != EventNames.end();
}
PluginRuntime::PluginRuntime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
std::unique_ptr<PluginRuntime> PluginRuntime::Start(std::string source, std::string pluginName,
    bool allowOsd, Callbacks callbacks, std::string initialStatusJson) {
    if (source.empty() || source.size() > MaxSource || source.find('\0') != source.npos)
        throw std::invalid_argument("Plugin entry must contain 1 byte to 1 MiB of UTF-8 source without NUL.");
    if (pluginName.size() > 512 || initialStatusJson.empty() || initialStatusJson.size() > MaxJson)
        throw std::invalid_argument("Plugin name or initial status exceeds limits.");
    auto instance = std::unique_ptr<PluginRuntime>(new PluginRuntime(std::make_unique<Impl>(
        std::move(source), std::move(pluginName), allowOsd, std::move(callbacks), std::move(initialStatusJson))));
    auto impl = instance->impl_.get();
    impl->thread = std::jthread([impl] { impl->Run(); });
    return instance;
}
PluginRuntime::~PluginRuntime() = default;
void PluginRuntime::Stop() noexcept { impl_->stop.store(true); impl_->changed.notify_all(); }
bool PluginRuntime::Running() const noexcept { return impl_->running.load(); }
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

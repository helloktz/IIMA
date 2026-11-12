// Derived from IINA JavascriptPluginInstance.createJSContext,
// JavascriptAPICore.extraSetup and JavascriptPolyfill.register.
// Copyright (c) 2018-2020 lhc and IINA contributors. GPL-3.0.
#pragma once
namespace iima::plugins {
inline constexpr char PluginBootstrap[] = R"IIMAJS(
(() => {
  'use strict';
  const host = globalThis.__iimaHost;
  delete globalThis.__iimaHost;
  const freeze = Object.freeze;
  const stringify = JSON.stringify;
  const toString = String;
  const missing = name => { throw new Error('IIMA plugin API not implemented: ' + name); };
  const namespace = (name, values) => new Proxy(freeze(values), {
    get(target, key) {
      if (typeof key === 'symbol') return undefined;
      if (Object.prototype.hasOwnProperty.call(target, key)) return target[key];
      return missing(name + '.' + key);
    },
    set() { throw new TypeError('IIMA API objects are read-only'); }
  });
  const statusKeys = freeze(['paused', 'idle', 'position', 'duration', 'speed',
      'videoWidth', 'videoHeight', 'isNetworkResource', 'url', 'title']);
  const status = new Proxy(Object.create(null), {
    get(_, key) {
      if (typeof key === 'symbol') return undefined;
      if (key === 'toJSON') return () => host.status();
      if (!statusKeys.includes(key)) return missing('core.status.' + key);
      const value = host.status();
      return Object.prototype.hasOwnProperty.call(value, key) ? value[key] : null;
    },
    set() { throw new TypeError('core.status is read-only'); },
    ownKeys() { return statusKeys.slice(); },
    getOwnPropertyDescriptor(_, key) {
      return statusKeys.includes(key) ? {configurable: true, enumerable: true} : undefined;
    }
  });
  const core = namespace('core', {
    pause: () => host.pause(), resume: () => host.resume(), stop: () => host.stop(),
    seek: (seconds, exact = false) => host.seek(seconds, exact),
    seekTo: seconds => host.seekTo(seconds),
    setSpeed: speed => host.setSpeed(speed), osd: message => host.osd(message),
    status
  });
  function render(value) {
    if (value === undefined) return '<undefined>';
    if (value === null) return '<null>';
    if (typeof value === 'string') return value;
    if (typeof value === 'symbol' || typeof value === 'bigint') return toString(value);
    try { const text = stringify(value, null, 2); return text === undefined ? toString(value) : text; }
    catch (_) { return '<unserializable>'; }
  }
  const consoleApi = namespace('console', {
    log: (...args) => host.log(args.map(render).join(' ')),
    warn: message => host.warn(render(message)),
    error: message => host.error(render(message))
  });
  const event = namespace('event', {
    on: (name, callback) => host.on(name, callback),
    off: (name, id) => host.off(name, id)
  });
  Object.defineProperty(globalThis, 'iina', {
    value: namespace('iina', {core, console: consoleApi, event}),
    writable: false, configurable: false
  });
  Object.defineProperty(globalThis, 'console', {value: consoleApi, configurable: false});
  globalThis.setTimeout = (callback, ms = 0) => host.timeout(callback, ms);
  globalThis.setInterval = (callback, ms = 0) => host.interval(callback, ms);
  globalThis.clearTimeout = id => host.clearTimer(id);
  globalThis.clearInterval = id => host.clearTimer(id);
  globalThis.require = () => missing('require');
})();
)IIMAJS";
}

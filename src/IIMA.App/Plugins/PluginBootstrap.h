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
  const hasOwn = Function.prototype.call.bind(Object.prototype.hasOwnProperty);
  const includes = Function.prototype.call.bind(Array.prototype.includes);
  const slice = Function.prototype.call.bind(Array.prototype.slice);
  const isArray = Array.isArray;
  const missing = name => { throw new Error('IIMA plugin API not implemented: ' + name); };
  const namespace = (name, values) => new Proxy(freeze(values), {
    get(target, key) {
      if (typeof key === 'symbol') return undefined;
      if (hasOwn(target, key)) return target[key];
      return missing(name + '.' + key);
    },
    set() { throw new TypeError('IIMA API objects are read-only'); }
  });
  const statusKeys = freeze(['paused', 'idle', 'position', 'duration', 'speed',
      'videoWidth', 'videoHeight', 'isNetworkResource', 'url', 'title']);
  const status = new Proxy(Object.create(null), {
    get(_, key) {
      if (typeof key === 'symbol') return undefined;
      if (key === 'toJSON') return () => {
        const snapshot = host.status(); const result = {};
        for (let index = 0; index < statusKeys.length; ++index) {
          const property = statusKeys[index]; result[property] = snapshot[property] ?? null;
        }
        return result;
      };
      if (!includes(statusKeys, key)) return missing('core.status.' + key);
      const value = host.status();
      return hasOwn(value, key) ? value[key] : null;
    },
    set() { throw new TypeError('core.status is read-only'); },
    defineProperty() { return false; }, deleteProperty() { return false; }, preventExtensions() { return false; },
    ownKeys() { return slice(statusKeys); },
    getOwnPropertyDescriptor(_, key) {
      return includes(statusKeys, key) ? {configurable: true, enumerable: true} : undefined;
    }
  });
  function mediaProperties(name, writable, readOnly = []) {
    const keys = freeze([...Object.keys(writable), ...readOnly]);
    return new Proxy(Object.create(null), {
      get(_, key) {
        if (typeof key === 'symbol') return undefined;
        if (key === 'toJSON') return () => {
          const snapshot = host.status()[name] ?? {}; const result = {};
          for (let index = 0; index < keys.length; ++index) {
            const property = keys[index]; result[property] = snapshot[property] ?? null;
          }
          return result;
        };
        if (!includes(keys, key)) return missing('core.' + name + '.' + key);
        return host.status()[name]?.[key] ?? null;
      },
      set(_, key, value) {
        if (!includes(keys, key)) return missing('core.' + name + '.' + toString(key));
        if (!hasOwn(writable, key)) throw new TypeError('Track information is read-only');
        writable[key](value);
        return true; // command accepted for enqueue, not synchronous mpv state
      },
      defineProperty() { return false; }, deleteProperty() { return false; }, preventExtensions() { return false; },
      ownKeys() { return slice(keys); },
      getOwnPropertyDescriptor(_, key) {
        return includes(keys, key) ? {configurable: true, enumerable: true} : undefined;
      }
    });
  }
  const audio = mediaProperties('audio', {
    id: host.audioId, delay: host.audioDelay, volume: host.audioVolume, muted: host.audioMuted
  }, ['tracks', 'currentTrack']);
  const subtitle = mediaProperties('subtitle', {id: host.subtitleId, delay: host.subtitleDelay}, ['tracks', 'currentTrack', 'secondID']);
  const video = mediaProperties('video', {id: host.videoId}, ['tracks', 'currentTrack']);
  const core = namespace('core', {
    pause: () => host.pause(), resume: () => host.resume(), stop: () => host.stop(),
    seek: (seconds, exact = false) => host.seek(seconds, exact),
    seekTo: seconds => host.seekTo(seconds),
    setSpeed: speed => host.setSpeed(speed), osd: message => host.osd(message),
    getChapters: () => {
      const chapters = host.status().chapters;
      if (!isArray(chapters)) throw new Error('Chapter snapshot is unavailable');
      return chapters;
    },
    playChapter: index => host.playChapter(index),
    status, audio, subtitle, video
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
  globalThis.require = path => host.require(path);
})();
)IIMAJS";
}

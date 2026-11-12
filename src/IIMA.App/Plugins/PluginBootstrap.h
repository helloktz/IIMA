// Derived from IINA JavascriptPluginInstance.createJSContext,
// JavascriptAPICore.extraSetup, JavascriptAPIPreferences and JavascriptPolyfill.register.
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
  // JavascriptAPIPreferences get -> saved override -> manifest default;
  // set changes only the instance snapshot, sync explicitly publishes it.
  // JSON is an intentional portable subset of Foundation property-list values.
  const parseJson = JSON.parse;
  const ownKeys = Reflect.ownKeys;
  const getPrototype = Object.getPrototypeOf;
  const descriptor = Object.getOwnPropertyDescriptor;
  const create = Object.create;
  const objectPrototype = Object.prototype;
  const arrayPrototype = Array.prototype;
  const finite = Number.isFinite;
  const charCode = Function.prototype.call.bind(String.prototype.charCodeAt);
  function utf8Bytes(text) {
    let count = 0;
    for (let index = 0; index < text.length; ++index) {
      const code = charCode(text, index);
      if (code < 0x80) ++count;
      else if (code < 0x800) count += 2;
      else if (code >= 0xd800 && code <= 0xdbff && index + 1 < text.length &&
               charCode(text, index + 1) >= 0xdc00 && charCode(text, index + 1) <= 0xdfff) {
        count += 4; ++index;
      } else count += 3;
    }
    return count;
  }
  function preferenceKey(key) {
    if (typeof key !== 'string' || key.length === 0 || key.length > 256 || utf8Bytes(key) > 256 || includes(key, '\0'))
      throw new TypeError('Preference keys must be strings of 1 to 256 UTF-8 bytes without NUL');
    return key;
  }
  function encodePreference(value) {
    let output = '', bytes = 0, nodes = 0;
    const ancestors = create(null);
    let ancestorCount = 0;
    function emit(part) {
      bytes += utf8Bytes(part);
      if (bytes > 65536) throw new RangeError('Plugin preference value exceeds 64 KiB');
      output += part;
    }
    function visit(item, depth) {
      if (++nodes > 4096 || depth > 16) throw new RangeError('Plugin preference tree exceeds limits');
      if (item === null) { emit('null'); return; }
      if (typeof item === 'string') {
        if (item.length > 65536) throw new RangeError('Preference string is too large');
        emit(stringify(item)); return;
      }
      if (typeof item === 'boolean') { emit(item ? 'true' : 'false'); return; }
      if (typeof item === 'number') {
        if (!finite(item)) throw new TypeError('Preference numbers must be finite');
        emit(stringify(item)); return;
      }
      if (typeof item !== 'object') throw new TypeError('Preferences support JSON values only');
      if (depth >= 16) throw new RangeError('Plugin preference container depth exceeds 16');
      const array = isArray(item), prototype = getPrototype(item);
      if (prototype !== null && prototype !== (array ? arrayPrototype : objectPrototype))
        throw new TypeError('Preferences require ordinary objects or arrays');
      for (let index = 0; index < ancestorCount; ++index)
        if (ancestors[index] === item) throw new TypeError('Circular preference values are not supported');
      ancestors[ancestorCount++] = item;
      try {
        const keys = ownKeys(item);
        if (keys.length > 4097) throw new RangeError('Preference object has too many properties');
        if (array) {
          const length = descriptor(item, 'length').value;
          if (length > 4096 || keys.length !== length + 1)
            throw new TypeError('Preference arrays must be dense, without extra properties');
          emit('[');
          for (let index = 0; index < length; ++index) {
            const field = descriptor(item, toString(index));
            if (!field || !hasOwn(field, 'value') || !field.enumerable)
              throw new TypeError('Preference arrays require enumerable data elements');
            if (index) emit(','); visit(field.value, depth + 1);
          }
          emit(']');
        } else {
          emit('{'); let count = 0;
          for (let index = 0; index < keys.length; ++index) {
            const key = keys[index];
            if (typeof key !== 'string') throw new TypeError('Symbol preference properties are not supported');
            const field = descriptor(item, key);
            if (!field || !hasOwn(field, 'value') || !field.enumerable)
              throw new TypeError('Preference objects require enumerable data properties');
            preferenceKey(key);
            if (count++) emit(','); emit(stringify(key)); emit(':'); visit(field.value, depth + 1);
          }
          emit('}');
        }
      } finally { delete ancestors[--ancestorCount]; }
    }
    visit(value, 0);
    return output;
  }
  function preferenceMap(document) {
    if (document === null || typeof document !== 'object' || isArray(document))
      throw new TypeError('Plugin preference document must be an object');
    // Validate the complete host document, including depth and aggregate limits.
    encodePreference(document);
    const result = create(null), keys = ownKeys(document);
    if (keys.length > 256) throw new RangeError('Plugin supports at most 256 preference keys');
    for (let index = 0; index < keys.length; ++index) {
      const key = preferenceKey(keys[index]); result[key] = encodePreference(document[key]);
    }
    return result;
  }
  const preferenceDefaults = preferenceMap(host.preferenceDefaults());
  const preferenceValues = preferenceMap(host.preferenceStored());
  function preferenceDocument(replacedKey, replacement) {
    let result = '{', count = 0;
    const keys = ownKeys(preferenceValues);
    for (let index = 0; index < keys.length; ++index) {
      const key = keys[index];
      if (count++) result += ',';
      result += stringify(key) + ':' + (key === replacedKey ? replacement : preferenceValues[key]);
    }
    if (replacedKey !== undefined && !hasOwn(preferenceValues, replacedKey)) {
      if (count >= 256) throw new RangeError('Plugin supports at most 256 preference keys');
      if (count++) result += ',';
      result += stringify(replacedKey) + ':' + replacement;
    }
    result += '}';
    if (utf8Bytes(result) > 65536) throw new RangeError('Plugin preference document exceeds 64 KiB');
    // Aggregate node and depth limits also apply across distinct keys.
    encodePreference(parseJson(result));
    return result;
  }
  const preferences = namespace('preferences', {
    get(key) {
      key = preferenceKey(key);
      if (hasOwn(preferenceValues, key)) return parseJson(preferenceValues[key]);
      if (hasOwn(preferenceDefaults, key)) return parseJson(preferenceDefaults[key]);
      host.warn('Trying to get preference value for undefined key ' + key);
      return undefined;
    },
    set(key, value) {
      key = preferenceKey(key);
      const encoded = encodePreference(value);
      preferenceDocument(key, encoded); // validate before replacing old state
      preferenceValues[key] = encoded;
    },
    sync() { host.preferenceSync(preferenceDocument()); }
  });
  Object.defineProperty(globalThis, 'iina', {
    value: namespace('iina', {core, console: consoleApi, event, preferences}),
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

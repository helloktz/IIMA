// Derived from IINA JavascriptPluginInstance.createJSContext,
// JavascriptAPICore.extraSetup, JavascriptAPIPreferences, JavascriptAPIMenu,
// JavascriptPluginMenuItem and JavascriptPolyfill.register.
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
  // JavascriptAPIMenu/JavascriptPluginMenuItem. Keep callbacks in the worker's
  // JS heap; only immutable JSON crosses to native UI. A WeakMap does not keep
  // detached menu objects alive when the plugin releases them.
  const menuRecords = new WeakMap();
  const weakGet = Function.prototype.call.bind(WeakMap.prototype.get);
  const weakSet = Function.prototype.call.bind(WeakMap.prototype.set);
  const define = Object.defineProperty;
  const preventExtensions = Object.preventExtensions;
  let menuRoots = [], menuSerial = 0, menuRevision = 0;
  let menuReachable = create(null);
  function recordOf(item) {
    const record = weakGet(menuRecords, item);
    if (!record) throw new TypeError('Expected a menu item created by this plugin');
    return record;
  }
  function menuTitle(value) {
    if (typeof value !== 'string' || value.length > 1024 || utf8Bytes(value) > 4096 || includes(value, '\0'))
      throw new TypeError('Menu title must be a string up to 1024 UTF-16 units without NUL');
    return value;
  }
  function menuKey(value) {
    if (value === null || value === undefined || value === '') return null;
    throw new TypeError('Plugin menu keyBinding is not implemented in IIMA');
  }
  function menuBoolean(value) {
    if (typeof value !== 'boolean') throw new TypeError('Menu flag must be boolean');
    return value;
  }
  function menuSnapshot(roots) {
    const reachable = create(null);
    let count = 0, text = '', bytes = 0;
    function append(part) {
      bytes += utf8Bytes(part);
      if (bytes > 64000) throw new RangeError('Plugin menu snapshot exceeds its size budget');
      text += part;
    }
    function list(items, depth, ancestorsEnabled) {
      if (depth > 8) throw new RangeError('Plugin menu depth exceeds eight levels');
      append('[');
      for (let index = 0; index < items.length; ++index) {
        const item = items[index], record = recordOf(item);
        if (++count > 128) throw new RangeError('Plugin menu exceeds 128 items');
        if (hasOwn(reachable, record.id)) throw new TypeError('Duplicate or circular plugin menu item');
        const enabled = ancestorsEnabled && record.enabled && !record.separator;
        reachable[record.id] = {item, enabled, leaf: record.children.length === 0};
        if (index) append(',');
        append('{"id":' + stringify(record.id) + ',"title":' + stringify(record.title) +
          ',"enabled":' + (record.enabled ? 'true' : 'false') +
          ',"selected":' + (record.selected ? 'true' : 'false') +
          ',"separator":' + (record.separator ? 'true' : 'false') + ',"keyBinding":null,"items":');
        if (record.children.length) list(record.children, depth + 1, enabled);
        else append('[]');
        append('}');
      }
      append(']');
    }
    list(roots, 1, true);
    return {text, reachable};
  }
  function publishMenu() {
    const snapshot = menuSnapshot(menuRoots);
    if (menuRevision >= 9007199254740991) throw new RangeError('Menu revision counter exhausted');
    const revision = toString(menuRevision + 1);
    host.menuPublish('{"revision":' + stringify(revision) + ',"items":' + snapshot.text + '}', revision);
    ++menuRevision;
    menuReachable = snapshot.reachable;
  }
  function changeMenu(apply, rollback, localItem) {
    apply();
    try {
      // Validate unattached subtrees too. They may be attached in a later turn.
      if (localItem !== undefined) menuSnapshot([localItem]);
      publishMenu();
    } catch (error) { rollback(); throw error; }
  }
  function makeMenuItem(title, action, options, separator) {
    title = menuTitle(title);
    if (action !== null && action !== undefined && typeof action !== 'function')
      throw new TypeError('Menu action must be a function, null or undefined');
    if (options !== undefined && options !== null && typeof options !== 'object')
      throw new TypeError('Menu options must be an object');
    const readOption = (name, fallback) => {
      if (options === undefined || options === null) return fallback;
      const value = options[name]; return value === undefined ? fallback : value;
    };
    const enabled = menuBoolean(readOption('enabled', !separator));
    const selected = menuBoolean(readOption('selected', false));
    const keyBinding = menuKey(readOption('keyBinding', null));
    if (menuSerial >= 9007199254740991) throw new RangeError('Menu identity counter exhausted');
    const record = {id: 'menu-' + toString(++menuSerial), title, action, enabled, selected,
      keyBinding, separator, children: []};
    const item = create(null);
    weakSet(menuRecords, item, record);
    const fields = ['title', 'enabled', 'selected', 'keyBinding'];
    for (let index = 0; index < fields.length; ++index) {
      const name = fields[index];
      define(item, name, {enumerable: true, configurable: false,
        get: () => record[name],
        set: value => {
          value = name === 'title' ? menuTitle(value) : name === 'keyBinding' ? menuKey(value) : menuBoolean(value);
          const previous = record[name];
          changeMenu(() => { record[name] = value; }, () => { record[name] = previous; }, item);
        }
      });
    }
    define(item, 'items', {enumerable: true, get: () => slice(record.children)});
    define(item, 'addSubMenuItem', {value: child => {
      recordOf(child);
      if (record.separator) throw new TypeError('A separator cannot contain submenus');
      const previous = record.children;
      const next = slice(previous); next[next.length] = child;
      changeMenu(() => { record.children = next; }, () => { record.children = previous; }, item);
      return item;
    }});
    return preventExtensions(item);
  }
  const menu = namespace('menu', {
    item: (title, action = null, options = {}) => makeMenuItem(title, action, options, false),
    separator: () => makeMenuItem('', null, {enabled: false}, true),
    addItem(item) {
      recordOf(item); const previous = menuRoots, next = slice(previous); next[next.length] = item;
      changeMenu(() => { menuRoots = next; }, () => { menuRoots = previous; });
    },
    items: () => slice(menuRoots),
    removeAt(index) {
      if (typeof index !== 'number' || !finite(index) || index < 0 || index % 1 || index >= menuRoots.length) return false;
      const previous = menuRoots, next = [];
      for (let position = 0; position < previous.length; ++position)
        if (position !== index) next[next.length] = previous[position];
      changeMenu(() => { menuRoots = next; }, () => { menuRoots = previous; });
      return true;
    },
    removeAllItems() {
      const previous = menuRoots;
      changeMenu(() => { menuRoots = []; }, () => { menuRoots = previous; });
    },
    forceUpdate: () => publishMenu()
  });
  host.menuInstall((id, revision) => {
    if (revision !== toString(menuRevision) || !hasOwn(menuReachable, id)) return;
    const entry = menuReachable[id];
    if (!entry.enabled || !entry.leaf) return;
    const record = recordOf(entry.item);
    if (typeof record.action === 'function') {
      // An item with children opens a submenu rather than invoking its action.
      // selected is script-owned: do not auto-toggle it on a native click.
      const action = record.action; action(entry.item);
    }
  });
  // JavascriptAPIInput / PluginInputManager: one callback per normalized key
  // and event. Windows delivery is deliberately asynchronous, after the native
  // handler, so high-priority propagation interception is explicitly unsupported.
  const keyListeners = create(null);
  let keyListenerCount = 0;
  function addKeyListener(down, key, callback, priority = 100) {
    if (typeof priority !== 'number') priority = 100;
    if (!finite(priority) || priority % 1 || priority >= 200)
      throw new TypeError('Only finite low-priority input observation is supported; input cannot be consumed');
    if (typeof callback !== 'function') throw new TypeError('Input callback must be a function');
    key = host.inputNormalize(key);
    const identity = (down ? 'down:' : 'up:') + key;
    if (!hasOwn(keyListeners, identity)) {
      if (keyListenerCount >= 256) throw new RangeError('Plugin input listener limit is 256');
      ++keyListenerCount;
    }
    keyListeners[identity] = callback;
  }
  const input = namespace('input', {
    PRIORITY_LOW: 100, PRIORITY_HIGH: 200,
    MOUSE: '*mouse', RIGHT_MOUSE: '*rightMouse', OTHER_MOUSE: '*otherMouse',
    normalizeKeyCode: key => host.inputNormalize(key),
    getAllKeyBindings: () => host.inputBindings(),
    onKeyDown: (key, callback, priority) => addKeyListener(true, key, callback, priority),
    onKeyUp: (key, callback, priority) => addKeyListener(false, key, callback, priority),
    onMouseDown: () => missing('input.onMouseDown'),
    onMouseUp: () => missing('input.onMouseUp'),
    onMouseDrag: () => missing('input.onMouseDrag')
  });
  host.inputInstall((key, down, isRepeat) => {
    const identity = (down ? 'down:' : 'up:') + key;
    if (!hasOwn(keyListeners, identity)) return;
    // No current-pointer guess: keyboard routing has no reliable video-relative
    // coordinates. Callback return values cannot suppress already handled input.
    const callback = keyListeners[identity];
    const result = callback({x: null, y: null, isRepeat});
    if (result === true) host.warn('Input callback return cannot consume input in IIMA');
  });
  Object.defineProperty(globalThis, 'iina', {
    value: namespace('iina', {core, console: consoleApi, event, preferences, menu, input}),
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

/* Jelli Art drafts: unsaved edits survive a reload, a closed tab or a crashed browser.
 * Injected by tools/jelli-art/jelli_art.py before the other studio scripts.
 *
 * A draft is stored in this browser's localStorage, one per (kind, key), with
 * the base hash of the file the edit started from:
 *
 *   JelliDrafts.put(kind, key, base, data)   save or replace a draft (data: any JSON value)
 *   JelliDrafts.get(kind, key)                {kind, key, base, data, at} or null
 *   JelliDrafts.restore(kind, key, base)      {data, base, at, stale} or null; stale when the
 *                                             file changed since the draft's base
 *   JelliDrafts.drop(kind, key)               after a save or an explicit revert
 *   JelliDrafts.list(kind)                    [{kind, key, base, at}], newest first
 *   JelliDrafts.later(kind, key, base, fn)    put(kind, key, base, fn()) after a short pause,
 *                                             coalescing bursts (pointer strokes, typing)
 *   JelliDrafts.encodeBytes(u8) / decodeBytes(str)  base64 for pixel buffers
 *
 * Kinds in use: 'paint' (key = asset key, base = the PNG's sha from /api/data,
 * data = base64 RGBA). Suggested for the other editors: 'clips' (key = clip key,
 * base = clip_shas[key]), 'creatures' (key = 'creatures', base = creature_data_sha),
 * 'behaviour' (key = 'content', base = {behaviors, creatures} shas joined with ':').
 * Restoring a stale draft is fine: send the draft's base with the save, and the
 * server answers 409 so the page can ask before overwriting the newer file.
 * Drafts expire after 14 days; when storage is full the oldest are evicted.
 * Every call is safe when localStorage is unavailable (it then does nothing).
 */
'use strict';
(() => {
  const PREFIX = 'jelli-art:draft:', MAX_AGE_MS = 14 * 864e5, MAX_DRAFTS = 64, PAUSE_MS = 400;
  const timers = {};
  const id = (kind, key) => `${PREFIX}${kind}:${key}`;
  const cancel = name => { if (timers[name]) { clearTimeout(timers[name].timer); delete timers[name]; } };
  const storage = () => { try { return window.localStorage; } catch { return null; } };
  function read(name) {
    try { const v = JSON.parse(storage()?.getItem(name) ?? 'null'); return v && v.kind && 'data' in v ? v : null; } catch { return null; }
  }
  function names() {
    const s = storage(), out = []; if (!s) return out;
    try { for (let i = 0; i < s.length; i++) { const k = s.key(i); if (k && k.startsWith(PREFIX)) out.push(k); } } catch { /* blocked */ }
    return out;
  }
  function all() { return names().map(n => [n, read(n)]).filter(([, v]) => v).sort((a, b) => b[1].at - a[1].at); }
  function prune(keep = MAX_DRAFTS) {
    const now = Date.now();
    all().forEach(([name, v], i) => { if (i >= keep || now - v.at > MAX_AGE_MS) try { storage().removeItem(name); } catch { /* ignore */ } });
  }
  const Drafts = window.JelliDrafts = {
    put(kind, key, base, data) {
      const s = storage(); if (!s) return false;
      const text = JSON.stringify({kind, key, base: base ?? null, data, at: Date.now()});
      for (const keep of [MAX_DRAFTS, Math.floor(MAX_DRAFTS / 4), 0]) {
        try { s.setItem(id(kind, key), text); return true; } catch { prune(keep); }  // quota: evict oldest, retry
      }
      return false;
    },
    get: (kind, key) => read(id(kind, key)),
    restore(kind, key, base) {
      const d = read(id(kind, key)); if (!d) return null;
      return {data: d.data, base: d.base, at: d.at, stale: (base ?? null) !== d.base};
    },
    drop(kind, key) { cancel(id(kind, key)); try { storage()?.removeItem(id(kind, key)); } catch { /* ignore */ } },
    list: kind => all().map(([, v]) => v).filter(v => !kind || v.kind === kind).map(({kind: k, key, base, at}) => ({kind: k, key, base, at})),
    later(kind, key, base, fn) {
      const name = id(kind, key), run = () => { cancel(name); Drafts.put(kind, key, base, fn()); };
      cancel(name); timers[name] = {timer: setTimeout(run, PAUSE_MS), run};
    },
    /* Write every pending later() now; pagehide calls it, so a closing tab keeps its last edit. */
    flush() { for (const pending of Object.values(timers)) pending.run(); },
    encodeBytes(bytes) { let s = ''; for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000)); return btoa(s); },
    decodeBytes(text) { const s = atob(text), out = new Uint8ClampedArray(s.length); for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i); return out; },
    prune,
  };
  prune();
  window.addEventListener('pagehide', () => Drafts.flush());
})();

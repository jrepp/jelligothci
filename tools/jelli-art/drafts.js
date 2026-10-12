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
 *   JelliDrafts.track(kind, key, {dirty, base, data})  keep a draft in step with an editor: while dirty()
 *                                             is true put(kind, key, base(), data()) whenever it changes (checked
 *                                             every 700 ms and when the page is hidden or closed); drop it once clean
 *   JelliDrafts.encodeBytes(u8) / decodeBytes(str)  base64 for pixel buffers
 *
 * Kinds in use: 'paint' (key = asset key, base = the PNG's sha from /api/data,
 * data = base64 RGBA); 'clips' (key = 'clips', base = null, data = {clips, shas}: the
 * unsaved clips by key and the clip_shas they started from); 'creatures' (key = 'creatures',
 * base = creature_data_sha, data = {profiles, behaviors}: Behaviour & size);
 * 'behaviour' (key = 'content', base = behavior_data_sha and creature_data_sha joined with ':',
 * data = {behaviors, looks}: the Behaviour view's states, repertoires, rules and looks);
 * 'activities' (key = 'activities', base = activity_sha, data = the activities document).
 * Several tabs: each draft records its owner tab and a heartbeat; restore() returns null and drop() keeps the draft
 * while another tab owns it with a heartbeat under 5 s old (pagehide releases it at once). track() rewrites a draft
 * that another tab removed from under it.
 * Restoring a stale draft is fine: send the draft's base with the save, and the
 * server answers 409 so the page can ask before overwriting the newer file.
 * Drafts expire after 14 days; when storage is full the oldest are evicted.
 * Every call is safe when localStorage is unavailable (it then does nothing).
 */
'use strict';
(() => {
  const PREFIX = 'jelli-art:draft:', MAX_AGE_MS = 14 * 864e5, MAX_DRAFTS = 64, PAUSE_MS = 400;
  const timers = {}, passes = new Set();
  /* Ownership (several tabs): a draft records the tab that wrote it (owner) and a heartbeat (beat). A tab does not
   * restore or drop a draft whose owner is another tab with a fresh heartbeat; pagehide zeroes the beat, so a reload
   * or a closed tab hands its draft over at once, and a crashed or evicted tab hands it over after ALIVE_MS. */
  const TAB = Math.random().toString(36).slice(2) + Date.now().toString(36), ALIVE_MS = 5000, BEAT_MS = 1500;
  const owned = new Set();
  const foreignLive = v => !!v && !!v.owner && v.owner !== TAB && Date.now() - (v.beat || 0) < ALIVE_MS;
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
      const text = JSON.stringify({kind, key, base: base ?? null, data, at: Date.now(), owner: TAB, beat: Date.now()});
      for (const keep of [MAX_DRAFTS, Math.floor(MAX_DRAFTS / 4), 0]) {
        try { s.setItem(id(kind, key), text); owned.add(id(kind, key)); return true; } catch { prune(keep); }  // quota: evict oldest, retry
      }
      return false;
    },
    get: (kind, key) => read(id(kind, key)),
    restore(kind, key, base) {
      const d = read(id(kind, key)); if (!d || foreignLive(d)) return null;  // another open tab is still editing it
      return {data: d.data, base: d.base, at: d.at, stale: (base ?? null) !== d.base};
    },
    drop(kind, key) {
      cancel(id(kind, key)); owned.delete(id(kind, key));
      if (foreignLive(read(id(kind, key)))) return;  // not ours to discard while its tab is alive
      try { storage()?.removeItem(id(kind, key)); } catch { /* ignore */ }
    },
    list: kind => all().map(([, v]) => v).filter(v => !kind || v.kind === kind).map(({kind: k, key, base, at}) => ({kind: k, key, base, at})),
    later(kind, key, base, fn) {
      const name = id(kind, key), run = () => { cancel(name); Drafts.put(kind, key, base, fn()); };
      cancel(name); timers[name] = {timer: setTimeout(run, PAUSE_MS), run};
    },
    /* Write every pending later() now; pagehide calls it, so a closing tab keeps its last edit. */
    flush() { for (const pending of Object.values(timers)) pending.run(); },
    encodeBytes(bytes) { let s = ''; for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000)); return btoa(s); },
    decodeBytes(text) { const s = atob(text), out = new Uint8ClampedArray(s.length); for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i); return out; },
    /* Called by an editor once its drafts are restored at boot (not before, or the first pass would drop the very
     * draft that is waiting to be restored). It returns a function that runs one pass now. */
    track(kind, key, {dirty, base, data}) {
      let last;  // undefined: nothing seen yet, null: clean, else the last text written successfully
      const pass = () => {
        let on = false; try { on = !!dirty(); } catch { /* an editor mid-render */ }
        if (!on) {  // clean: a draft is dropped when the editor goes from dirty to clean, never for an editor that booted clean
          if (last !== null && last !== undefined) Drafts.drop(kind, key);
          last = null; return;
        }
        let text; try { text = JSON.stringify([base(), data()]); } catch { return; }
        /* Rewrite on a change, and also when another tab removed our draft (its Discard, a prune) though nothing changed. */
        const gone = text === last && !!storage() && !read(id(kind, key));
        if (text !== last || gone) { if (Drafts.put(kind, key, base(), data())) last = text; }  // a failed write is retried next pass
      };
      passes.add(pass);
      setInterval(pass, 700);
      return pass;
    },
    prune,
  };
  prune();
  function beat(release) {
    for (const name of owned) {
      const v = read(name); if (!v || v.owner !== TAB) continue;
      try { storage().setItem(name, JSON.stringify({...v, beat: release ? 0 : Date.now()})); } catch { /* ignore */ }
    }
  }
  setInterval(() => beat(false), BEAT_MS);
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) passes.forEach(p => p());  // Safari evicts a hidden tab without a beforeunload
    else beat(false);
  });
  window.addEventListener('pageshow', () => beat(false));
  window.addEventListener('pagehide', () => { Drafts.flush(); passes.forEach(p => p()); beat(true); });  // release last, so a reload restores at once
})();

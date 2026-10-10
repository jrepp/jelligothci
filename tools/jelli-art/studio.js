/* Jelli Art: painting, palette editing and live reload for compare.html.
 * Injected only by tools/jelli-art/jelli_art.py; the static review page never loads it.
 * Shares the page's globals (state, D, decoded, renderers) and adds a Paint mode.
 * The pixel logic (rasterisers, selection transforms, history) is paint_tools.js (window.JelliPaint). */
'use strict';
(() => {
  const S = window.Studio = {}, T = window.JelliPaint;
  const OUTLINED = new Set(['creatures', 'icons', 'menus', 'meters', 'health', 'effects', 'prizes', 'props']);
  const LOCKED = new Set(['font', 'backgrounds']);
  const SHAPES = new Set(['line', 'rect', 'ellipse']);
  /* [id, name, key, what it does] */
  const TOOLS = [
    ['pencil', 'Pencil', 'P', 'Draw pixels. Shift+arrows draw with the keyboard cursor'],
    ['eraser', 'Eraser', 'E', 'Make pixels transparent (right-click erases with any tool)'],
    ['fill', 'Fill', 'F', 'Fill the touching area of one colour'],
    ['picker', 'Pick colour', 'C', 'Take a palette colour from the sprite (Alt-click with any tool except Select picks without switching)'],
    ['line', 'Line', 'L', 'Drag from end to end; Shift snaps to 45°'],
    ['rect', 'Rectangle', 'R', 'Drag corner to corner; Shift makes a square'],
    ['ellipse', 'Ellipse', 'U', 'Drag the bounding box; Shift makes a circle'],
    ['select', 'Select', 'V', 'Drag a rectangle; drag inside it to move, Alt-drag to copy'],
  ];
  const ARROWS = {ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1]};
  const edits = {};  // key -> {saved, hist}; the working buffer is decoded[key].after
  let pointing = false, stroke = null, view = null, statusTimer = null, clipboard = null, selKey = null, pan = null, anchor = null, wheel = 0, queued = 0;
  const prefs = Object.assign({filled: false, perfect: false, wrap: false, guides: false}, store.get('paint', {}));
  Object.assign(state, {tool: 'pencil', color: '#291b35', custom: false, mirror: false, before: store.get('before', 'HEAD'), artist: store.get('artist', ''),
    sel: null, float: null, cursor: [0, 0], kbd: false}, prefs);
  const savePaintPrefs = () => store.set('paint', {filled: state.filled, perfect: state.perfect, wrap: state.wrap, guides: state.guides});
  MODES.push('paint');
  document.getElementById('modes').insertAdjacentHTML('beforeend', '<button data-mode="paint" title="Paint (6)">Paint</button>');
  document.querySelector('header .refs').insertAdjacentHTML('afterend',
    '<span class="live-badge" id="live-badge">● live</span><label class="lbl" for="before-ref">compare with</label><select id="before-ref"></select>' +
    '<input id="artist" class="artist" placeholder="Your name (for history)" aria-label="Your name, recorded with saves"><span id="git-status" class="studio-status"></span><span id="studio-status" class="studio-status" role="status"></span>');
  document.head.insertAdjacentHTML('beforeend', `<style>
    .live-badge{font:600 11px ui-monospace,monospace;color:var(--accent)}
    select{font:inherit;color:var(--ink);background:var(--raised);border:1px solid var(--line);border-radius:6px;padding:4px 6px;max-width:220px}
    .studio-status{font-size:12px;color:var(--muted)}.studio-status.warn{color:var(--warn)}.studio-status.bad{color:var(--bad)}
    button.primary{background:var(--primary);color:var(--on-primary);border-color:var(--primary);font-weight:700}
    button:disabled{opacity:.45;cursor:default}
    select:focus-visible,[role=button]:focus-visible,canvas:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
    .paint-chip{display:grid;grid-template-columns:auto;justify-items:center;gap:3px;background:var(--raised);border:2px solid transparent;border-radius:8px;padding:6px;cursor:pointer;min-width:62px;font:10px ui-monospace,monospace;color:var(--muted);position:relative}
    .paint-chip .sw{width:34px;height:34px;border-radius:6px;border:1px solid #fff3}
    .paint-chip[aria-pressed=true]{border-color:var(--ink);color:var(--ink)}
    .paint-slot{position:relative;display:grid}
    .paint-slot .edit{position:absolute;top:2px;right:2px;padding:0 4px;font-size:11px;line-height:16px;border-radius:4px;background:var(--panel)}
    .paint-chip.off .sw{outline:2px solid var(--bad);outline-offset:1px}
    .paint-chip.faint .sw{outline:2px dashed var(--ink);outline-offset:2px}
    .eraser-sw{background:repeating-conic-gradient(#555 0 25%,#2b2b31 0 50%) 0 0/10px 10px}
    .studio-note{font-size:12px;color:var(--muted);max-width:760px;margin:6px 0}
    .artist{width:170px;padding:4px 8px}
    .paint-tools{display:flex;gap:10px 14px;flex-wrap:wrap;align-items:end}
    .paint-readout{font:12px ui-monospace,monospace;color:var(--ink);text-transform:none;letter-spacing:0}
    .paint-hist{list-style:none;margin:0;padding:0;max-height:320px;overflow:auto;position:relative;min-width:150px;border:1px solid var(--line);border-radius:8px;background:var(--panel)}
    .paint-hist button{display:block;width:100%;text-align:left;border:0;border-radius:0;background:none;padding:3px 9px;font-size:12px}
    .paint-hist button:hover{background:var(--raised)}.paint-hist button[aria-current=step]{background:var(--ink);color:var(--bg)}
    .paint-hist button.redo{color:var(--muted);font-style:italic}
    .paint-hint{margin:6px 0 0;max-width:640px}
    .paint-work{display:flex;gap:16px;align-items:flex-start;width:100%}
    .paint-canvas{flex:0 1 auto;min-width:0}.paint-side{flex:1 1 300px;min-width:260px;max-width:440px;display:grid;gap:14px;align-content:start}
    .paint-side #palette-block{margin:0;padding:0;border:0}.paint-side #palette-block h3{margin:0 0 6px;font-size:12px}
    .paint-side .palette{gap:4px;margin:0}.paint-side .paint-chip{min-width:46px;padding:4px 3px;font-size:9px}.paint-side .paint-chip .sw{width:26px;height:26px}
    .paint-side .studio-note{font-size:11px}.paint-refs{display:flex;gap:12px;flex-wrap:wrap;align-items:flex-start}.paint-refs .paint-hist{max-height:180px}
    .paint-more{margin-top:8px}.paint-more>summary{cursor:pointer;font-weight:600;font-size:12px;width:fit-content;padding:2px 0}.paint-more>summary .lbl{font-weight:400;margin-left:6px}
    .paint-more>.paint-tools{margin-top:6px}
    @media(max-width:1100px){.paint-work{flex-wrap:wrap}.paint-side{max-width:none}}
    .vh{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}
    @media (prefers-reduced-motion: reduce){*{scroll-behavior:auto!important;transition:none!important}}
  </style>`);

  /* ---------- server calls ---------- */
  async function api(method, url, body) {
    const res = await fetch(url, {method, headers: body ? {'Content-Type': 'application/json'} : {}, body: body ? JSON.stringify(body) : undefined});
    const data = await res.json().catch(() => ({error: res.statusText}));
    if (!res.ok) throw Object.assign(new Error(data.error || res.statusText), {status: res.status, body: data});
    return data;
  }
  function status(text, cls = '', sticky = false, opts = {}) {
    if (window.JelliShell) return window.JelliShell.notify(text, {tone: cls, sticky, ...opts});  // shell.js: live region and toasts
    const el = document.getElementById('studio-status'); el.textContent = text; el.className = 'studio-status ' + cls;
    clearTimeout(statusTimer); if (!sticky) statusTimer = setTimeout(() => { el.textContent = ''; }, 4000);
  }
  const announce = text => { if (window.JelliShell) return window.JelliShell.announce(text); const el = document.getElementById('paint-live'); if (el) el.textContent = text; };

  /* ---------- loading and live reload ---------- */
  async function reload() {
    const keep = Object.entries(edits).filter(([key]) => dirty(key)).map(([key]) => [key, decoded[key].after.data.slice()]);
    await loadPayload(await api('GET', `/api/data?before=${encodeURIComponent(state.before)}`));
    // The server measures with the shared ink; re-measure sprites outlined from their own palette.
    for (const a of D.assets.filter(x => ownPalette(x) && !LOCKED.has(x.kind))) {
      if (decoded[a.key].after) a.after_metrics = measure(decoded[a.key].after, a);
      if (decoded[a.key].before) a.before_metrics = measure(decoded[a.key].before, a);
    }
    state.float = null; stroke = null;
    const kept = new Set(keep.map(([key]) => key));
    for (const key of Object.keys(edits)) {
      if (!decoded[key]?.after) { delete edits[key]; continue; }
      const data = decoded[key].after.data, e = edits[key];
      e.saved = data.slice();
      if (!kept.has(key)) e.base = byKey[key].sha;  // dirty edits keep the base they started from
      // A clean sprite that changed on disk starts a fresh history; dirty ones keep theirs (restored below).
      if (!kept.has(key) && !T.current(e.hist).every((v, i) => v === data[i])) e.hist = T.history(data, 'Loaded from disk');
    }
    for (const [key, data] of keep) if (decoded[key]?.after) { decoded[key].after.data.set(data); refresh(key); }
  }
  /* Unsaved paint from a previous visit (drafts.js); a stale one saves only after a confirm (409).
   * The history starts at the file on disk, then a "Restored draft" step, so Undo shows what is on disk. */
  function restoreDrafts() {
    const back = [];
    for (const {key} of window.JelliDrafts?.list('paint') || []) {
      const p = work(key), d = JelliDrafts.restore('paint', key, byKey[key]?.sha), data = d && JelliDrafts.decodeBytes(d.data);
      if (!p || !data || data.length !== p.data.length) { JelliDrafts.drop('paint', key); continue; }
      const e = ensure(key); e.base = d.base; p.data.set(data); T.record(e.hist, 'Restored draft', p.data); refresh(key);
      back.push(key + (d.stale ? ' (changed on disk since)' : ''));
    }
    if (!back.length) return;
    const text = `Restored unsaved edits from your last visit: ${back.join(', ')}`;
    if (window.JelliShell) window.JelliShell.notify(text, {tone: 'warn', id: 'drafts', hint: 'Save to keep them, or Revert to discard them.'});
    else status(text, 'warn', true);
  }
  S.boot = async () => {
    await reload();
    restoreDrafts();
    const refs = await api('GET', '/api/refs').catch(() => ({tags: [], commits: []}));
    const select = document.getElementById('before-ref');
    const options = [['HEAD', 'HEAD (last commit)'], ...refs.tags.map(t => [t, `tag ${t}`]), ...refs.commits.map(c => [c.ref, `${c.ref} ${c.subject}`.slice(0, 60)])];
    if (!options.some(([v]) => v === state.before)) options.unshift([state.before, state.before]);
    select.innerHTML = options.map(([v, t]) => `<option value="${v}">${t.replace(/</g, '&lt;')}</option>`).join('');
    select.value = state.before;
    select.onchange = async () => { state.before = select.value; store.set('before', state.before); await reload(); rerender(); };
    const artist = document.getElementById('artist'); artist.value = state.artist;
    artist.oninput = () => { state.artist = artist.value; store.set('artist', state.artist); };
    document.getElementById('live-badge').textContent = `● Jelli Art ${D.studio_version || ''}`;
    renderGit(D.git);
    setInterval(poll, 2000);
    window.addEventListener('beforeunload', e => { if (Object.keys(edits).some(dirty) || S.clipsDirty?.()) { e.preventDefault(); e.returnValue = ''; } });
  };
  /* Confirm through the shell's styled dialog (plain confirm() without it); "1 clip", "3 clips". */
  const ask = (text, opts = {}) => window.JelliShell?.confirm ? window.JelliShell.confirm(text, opts) : Promise.resolve(confirm(text));
  const count = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;
  Object.assign(S, {api, status, reload, rerender: () => rerender(), renderGit, paintDirty: key => dirty(key), redraw: () => draw(), ask, count});
  function renderGit(git) {
    const el = document.getElementById('git-status'); if (!git?.enabled) { el.textContent = ''; return; }
    const push = git.last_push || {}, pending = git.unpushed ? `${git.unpushed} to push` : 'synced';
    el.textContent = `⎇ ${git.branch} · ${push.ok === false ? 'push failing, will retry' : git.push ? pending : 'commits only'}`;
    el.className = 'studio-status' + (push.ok === false ? ' bad' : '');
    el.title = push.error || `Every save is committed to ${git.branch}${git.push ? ' and pushed to GitHub for review' : ''}.`;
    if (git.pending_commits) {  // commits that failed are queued server-side and retried (git_sync.py)
      el.textContent += ` · ${git.pending_commits} commit(s) queued`; el.className = 'studio-status bad'; el.title = git.last_commit_error;
      if (queued !== git.pending_commits) window.JelliShell?.notify(`${git.pending_commits} commit(s) are queued and will be retried: ${git.last_commit_error}`,
        {tone: 'warn', id: 'git-queue', sticky: true, hint: 'Your saves are on disk; the studio retries the commit (POST /api/git/retry retries now).'});
    } else if (queued) window.JelliShell?.dismiss?.('git-queue');
    queued = git.pending_commits || 0;
  }
  async function poll() {
    if (document.hidden || stroke) return;
    if (D.git?.enabled) api('GET', '/api/git').then(g => { D.git = g; renderGit(g); }).catch(() => {});
    const {version} = await api('GET', '/api/version').catch(() => ({}));
    if (!version || version === D.version) return;
    await reload(); rerender();
    status(Object.keys(edits).some(dirty) ? 'Files changed on disk; your unsaved edits were kept' : 'Updated from disk', 'warn');
  }
  function rerender() { renderTotals(); renderKinds(); renderList(); renderView(); }

  /* ---------- pixel buffer helpers ---------- */
  const work = key => decoded[key]?.after;
  const dirty = key => { const e = edits[key], p = work(key); if (!e || !p) return false; for (let i = 0; i < p.data.length; i++) if (p.data[i] !== e.saved[i]) return true; return false; };
  const ensure = key => (edits[key] ||= {saved: work(key).data.slice(), hist: T.history(work(key).data), base: byKey[key]?.sha});
  /* Assets may carry their own palette (a name in manifest.palettes or an inline list). */
  const ownPalette = a => !!a.palette_name && a.palette_name !== 'shared';
  const paletteOf = a => (a.palette || D.palette).map(h => h.toLowerCase());
  const paletteWhere = a => ownPalette(a) ? `the ${a.palette_name} palette` : 'the shared palette';
  const luma = hex => [1, 3, 5].reduce((n, i, k) => n + parseInt(hex.slice(i, i + 2), 16) * [299, 587, 114][k], 0);
  /* The outline colour: shared ink, or the darkest colour of the asset's own palette. */
  const inkOf = a => ownPalette(a) ? paletteOf(a).reduce((d, h) => luma(h) < luma(d) ? h : d) : '#291b35';
  const slotName = (a, i) => ownPalette(a) ? `${a.palette_name} ${i + 1}` : NAMES[i];
  S.ownPalette = ownPalette;
  function measure(p, a) {
    const kind = a.kind, ink = inkOf(a), colors = {}, specks = [], open = [], measured = !LOCKED.has(kind);
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) {
      const hex = pixel(p, x, y); if (!hex) continue;
      colors[hex] = (colors[hex] || 0) + 1;
      const n4 = [[1, 0], [-1, 0], [0, 1], [0, -1]].map(([dx, dy]) => pixel(p, x + dx, y + dy));
      const n8 = [...n4, ...[[1, 1], [-1, -1], [1, -1], [-1, 1]].map(([dx, dy]) => pixel(p, x + dx, y + dy))];
      if (measured && hex !== ink && hex !== '#ffffff' && !n8.includes(hex)) specks.push([x, y]);
      if (OUTLINED.has(kind) && hex !== ink && n4.includes(null)) open.push([x, y]);
    }
    return {colors, opaque: Object.values(colors).reduce((n, v) => n + v, 0), specks, open_edges: open};
  }
  /* Push the working buffer to its canvas, metrics, thumbnail and change count. */
  function refresh(key) {
    const a = byKey[key], p = work(key), before = decoded[key].before;
    p.img.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(p.data), p.w, p.h), 0, 0);
    a.after_metrics = measure(p, a);
    a.after = p.img.toDataURL('image/png');
    if (before) { let n = 0; for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) if (pixel(before, x, y) !== pixel(p, x, y)) n++; a.changed = n; }
  }
  /* Keep unsaved paint in drafts.js after each change, or drop the draft once the sprite is clean again. */
  function draft(key) {
    if (!window.JelliDrafts) return;
    if (!dirty(key)) return JelliDrafts.drop('paint', key);
    JelliDrafts.later('paint', key, edits[key].base, () => JelliDrafts.encodeBytes(work(key).data));
  }
  function commit() {
    const a = asset(); refresh(a.key); draft(a.key);
    renderHeader(a); renderPalette(a); renderContext(a); renderTotals(); renderList(); draw(); renderToolbarState(); renderHistory(); renderReadout();
  }
  /* Finish an action: record it in the asset's history (no-ops are skipped) and redraw. */
  function done(label) { T.record(ensure(state.key).hist, label, work(state.key).data); commit(); }
  function restore(i) {
    const e = edits[state.key], data = e && T.jump(e.hist, i); if (!data) return;
    work(state.key).data.set(data); state.float = null; stroke = null; commit();
    announce(`History: ${e.hist.entries[i].label}`);
  }
  const undo = () => { const e = edits[state.key]; if (e) restore(e.hist.at - 1); };
  const redo = () => { const e = edits[state.key]; if (e) restore(e.hist.at + 1); };

  /* ---------- painting ---------- */
  /* The colour tools may paint: a colour of the asset's palette, or any colour once "custom" is chosen. */
  function paintHex() {
    const a = asset();
    if (paletteOf(a).includes(state.color) || state.custom) return state.color;
    status(`${state.color} is not in ${paletteWhere(a)}; pick a palette colour, or choose it under custom`, 'warn', true);
    return undefined;
  }
  const selRect = () => state.sel && T.clip(work(state.key), state.sel);
  const inRect = (r, x, y) => x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
  /* Paint one pixel (and its mirror twin); a selection keeps painting inside it. */
  function plot(p, x, y, hex, mask) {
    if (!mask || inRect(mask, x, y)) T.set(p, x, y, hex);
    const mx = p.w - 1 - x; if (state.mirror && (!mask || inRect(mask, mx, y))) T.set(p, mx, y, hex);
  }
  function constrain(tool, [x0, y0], [x1, y1]) {
    const dx = x1 - x0, dy = y1 - y0, d = Math.max(Math.abs(dx), Math.abs(dy)), sx = Math.sign(dx) || 1, sy = Math.sign(dy) || 1;
    if (tool !== 'line') return [x0 + sx * d, y0 + sy * d];
    if (Math.abs(dx) > 2 * Math.abs(dy)) return [x1, y0];
    if (Math.abs(dy) > 2 * Math.abs(dx)) return [x0, y1];
    return [x0 + sx * d, y0 + sy * d];
  }
  function strokePoints(s) {
    if (s.kind === 'free') return state.perfect && s.hex ? T.pixelPerfect(s.pts) : s.pts;
    const [[x0, y0], [x1, y1]] = [s.from, s.to];
    if (s.tool === 'line') return T.line(x0, y0, x1, y1);
    return (s.tool === 'rect' ? T.rect : T.ellipse)(x0, y0, x1, y1, state.filled);
  }
  function paintStroke() {
    const p = work(state.key); p.data.set(stroke.base);
    for (const [x, y] of strokePoints(stroke)) plot(p, x, y, stroke.hex, stroke.mask);
  }
  const snapshot = () => ({data: work(state.key).data.slice(), sel: state.sel && {...state.sel}, float: state.float && {...state.float}});
  /* Start the tool at a pixel. pointer: false for the keyboard cursor. */
  function press(x, y, tool, {erase = false, alt = false, pointer = true, held = false} = {}) {
    if (tool === 'picker') return pick(x, y);
    if (tool === 'select') return pressSelect(x, y, alt, pointer);
    const hex = tool === 'eraser' || erase ? null : paintHex(); if (hex === undefined) return;
    const p = work(state.key), mask = selRect(); state.float = null;  // painting drops a floating selection where it is
    if (tool === 'fill') {
      if (mask && !inRect(mask, x, y)) return status('Fill inside the selection, or press Escape to clear it', 'warn');
      T.flood(p, x, y, hex, mask || T.whole(p)); if (state.mirror) T.flood(p, p.w - 1 - x, y, hex, mask || T.whole(p));
      return done(hex ? 'Fill' : 'Erase fill');
    }
    const label = SHAPES.has(tool) ? TOOLS.find(t => t[0] === tool)[1] + (tool !== 'line' && state.filled ? ' (filled)' : '') : hex ? 'Pencil' : 'Eraser';
    stroke = {kind: SHAPES.has(tool) ? 'shape' : 'free', tool, hex, pointer, held, mask, label, base: p.data.slice(), from: [x, y], to: [x, y], pts: [[x, y]], snap: snapshot()};
    paintStroke(); draw();
  }
  function pressSelect(x, y, alt, pointer) {
    const snap = snapshot(), r = selRect();
    if (r && inRect(state.sel, x, y)) {
      if (state.float && alt) state.float.base = work(state.key).data.slice();  // Alt-drag a floating selection: stamp a copy here
      lift(alt);
      stroke = {kind: 'move', pointer, from: [x, y], origin: [state.float.x, state.float.y], snap, label: alt ? 'Copy selection' : 'Move selection'};
    } else {
      state.float = null; state.sel = {x, y, w: 1, h: 1};
      stroke = {kind: 'marquee', pointer, from: [x, y], to: [x, y], snap, moved: false};
    }
    draw(); renderReadout();
  }
  function drag(x, y, snapTo = false) {
    const s = stroke; if (!s) return;
    if (s.kind === 'free') { const [lx, ly] = s.pts[s.pts.length - 1]; s.pts.push(...T.line(lx, ly, x, y).slice(1)); paintStroke(); }
    else if (s.kind === 'shape') { s.to = snapTo ? constrain(s.tool, s.from, [x, y]) : [x, y]; paintStroke(); }
    else if (s.kind === 'marquee') { s.moved ||= x !== s.from[0] || y !== s.from[1]; s.to = [x, y]; state.sel = T.clip(work(state.key), T.rectFrom(...s.from, x, y)); }
    else if (s.kind === 'move') {
      place(state.float, s.origin[0] + x - s.from[0], s.origin[1] + y - s.from[1]); compose();
    }
    draw(); renderReadout();
  }
  function release() {
    const s = stroke; if (!s) return; stroke = null;
    if (s.kind === 'marquee') {
      if (s.pointer && !s.moved) state.sel = null;  // a click without a drag clears the selection
      draw(); renderToolbarState(); renderReadout();
      return announce(state.sel ? `Selected ${state.sel.w} by ${state.sel.h} at ${state.sel.x}, ${state.sel.y}` : 'Selection cleared');
    }
    done(s.label);
  }
  function cancel() {
    const s = stroke; if (!s) return false; stroke = null;
    work(state.key).data.set(s.snap.data); state.sel = s.snap.sel; state.float = s.snap.float;
    draw(); renderReadout(); announce('Cancelled'); return true;
  }
  /* Alt-click borrows the picker for one click and keeps the current tool (paint_tools.js pickOutcome). */
  function pick(x, y) {
    const a = asset(), hex = pixel(work(state.key), x, y), out = T.pickOutcome(hex, paletteOf(a), state.tool);
    if (out.off) return status(`${hex} is not in ${paletteWhere(a)}; click its custom chip under Paint colours to paint with it`, 'warn', true);
    if (out.color) { state.color = out.color; state.custom = false; }
    state.tool = out.tool;
    renderPalette(a); renderToolbarState(); renderReadout();
  }

  /* ---------- selection: a rect, plus floating pixels while it is being moved or transformed ---------- */
  /* float = {region, base, x, y}: base is the sprite without the region; the buffer is base with region drawn at x, y. */
  function lift(copyOnly = false) {
    if (state.float) return;
    const p = work(state.key), r = selRect(), base = {w: p.w, h: p.h, data: p.data.slice()};
    if (!copyOnly) T.clear(base, r);
    state.float = {region: T.extract(p, r), base: base.data, x: r.x, y: r.y}; state.sel = {...r};
  }
  /* Keep a floating selection overlapping the sprite by at least one pixel. */
  function place(f, x, y) {
    const p = work(state.key); f.x = Math.max(1 - f.region.w, Math.min(p.w - 1, x)); f.y = Math.max(1 - f.region.h, Math.min(p.h - 1, y));
    state.sel = {x: f.x, y: f.y, w: f.region.w, h: f.region.h};
  }
  function compose() { const p = work(state.key), f = state.float; p.data.set(f.base); T.blit(p, f.region, f.x, f.y); }
  function selectAll() { const p = work(state.key); stroke = null; state.float = null; state.sel = T.whole(p); state.tool = 'select'; afterSelect(); }
  function deselect() { state.float = null; state.sel = null; afterSelect(); }
  function afterSelect() { draw(); renderToolbarState(); renderReadout(); announce(state.sel ? `Selected ${state.sel.w} by ${state.sel.h}` : 'Selection cleared'); }
  function deleteSel() {
    const r = selRect(); if (!r) return false;
    const p = work(state.key);
    if (state.float) { p.data.set(state.float.base); state.float = null; } else T.clear(p, r);
    done('Delete selection'); return true;
  }
  function copySel() {
    const p = work(state.key), r = selRect();
    clipboard = state.float ? T.copy(state.float.region) : T.extract(p, r || T.whole(p));
    status(`Copied ${clipboard.w}×${clipboard.h}${r ? '' : ' (the whole sprite)'}`); renderToolbarState();
  }
  function cutSel() { if (!selRect()) return status('Select an area to cut', 'warn'); copySel(); deleteSel(); }
  function paste() {
    if (!clipboard) return status('Copy something first (⌘C / Ctrl+C)', 'warn');
    const a = asset(), p = work(state.key), off = T.offPalette(clipboard, paletteOf(a));
    if (off.length && !state.custom) return status(`The copied pixels use ${off.join(', ')}, which ${paletteWhere(a)} lacks; choose custom to paste them anyway`, 'bad', true);
    const r = selRect(), [cx, cy] = state.kbd ? state.cursor : [0, 0];
    const x = Math.max(0, Math.min(r ? r.x : cx, p.w - clipboard.w)), y = Math.max(0, Math.min(r ? r.y : cy, p.h - clipboard.h));
    state.float = {region: T.copy(clipboard), base: p.data.slice(), x, y}; state.sel = {x, y, w: clipboard.w, h: clipboard.h};
    state.tool = 'select'; compose(); done('Paste');
    announce(`Pasted ${clipboard.w} by ${clipboard.h} at ${x}, ${y}; arrows with Alt move it`);
  }
  /* Flip or rotate the selection (about its centre), or the whole sprite. */
  function transform(label, fn) {
    if (!view) return;
    const p = work(state.key);
    if (selRect()) {
      lift(); const f = state.float, r = fn(f.region);
      const x = f.x + Math.floor((f.region.w - r.w) / 2), y = f.y + Math.floor((f.region.h - r.h) / 2);
      f.region = r; place(f, x, y); compose();
    } else {
      const r = fn(T.copy(p)); if (r.w !== p.w) return status('This sprite is not square; select a square area to rotate', 'warn');
      p.data.set(r.data); state.float = null;
    }
    done(label);
  }
  /* Alt+arrows: move the selection, or with Wrap on (or no selection) shift the pixels inside it or the sprite. */
  function nudge(dx, dy) {
    if (!view) return;
    const p = work(state.key), r = selRect();
    if (r && !state.wrap) {
      lift(); place(state.float, state.float.x + dx, state.float.y + dy); compose();
      return done('Move selection');
    }
    state.float = null; T.shift(p, r || T.whole(p), dx, dy, state.wrap);
    done(r ? 'Shift selection' : 'Shift sprite');
  }
  function replaceColour() {
    const from = document.getElementById('replace-from')?.value; if (!from || !view) return;
    const to = state.tool === 'eraser' ? null : paintHex(); if (to === undefined) return;
    const p = work(state.key), r = selRect(); state.float = null;
    const n = T.replace(p, r || T.whole(p), from, to);
    done('Replace colour');
    status(n ? `Replaced ${n} px of ${from} with ${to || 'transparent'}${r ? ' in the selection' : ''}` : `No ${from} pixels to replace`);
  }
  const toggle = (name, label) => { state[name] = !state[name]; savePaintPrefs(); renderToolbarState(); draw(); announce(`${label} ${state[name] ? 'on' : 'off'}`); };
  S.toggleGuides = () => toggle('guides', 'Guides');
  /* Zoom to the drawn pixels plus a margin, and scroll them into view (a 48 px frame often has empty rows). */
  function fitContent() {
    if (!view) return;
    const a = view.a, b = T.bounds(work(a.key)); if (!b) return status('Nothing to fit: the sprite has no pixels yet', 'warn');
    const m = 2, x0 = Math.max(0, b.x - m), y0 = Math.max(0, b.y - m), w = Math.min(a.width, b.x + b.w + m) - x0, h = Math.min(a.height, b.y + b.h + m) - y0;
    anchor = {px: x0, py: y0, ox: 0, oy: 0}; setZoom(fitZoom({width: w, height: h}));
    announce(`Fitted ${b.w} by ${b.h} pixels at ${view.z}×`);
  }

  /* ---------- paint stage ---------- */
  function cell(e) { return [Math.floor(e.offsetX / view.z), Math.floor(e.offsetY / view.z)]; }
  function draw() {
    if (!view) return;
    const {ctx, a, z} = view, p = work(a.key);
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0); ctx.imageSmoothingEnabled = false; ctx.clearRect(0, 0, a.width * z, a.height * z);
    fillBackdrop(ctx, a.width, a.height, z, backdropFor(a));
    drawPixels(ctx, state.peek ? decoded[a.key].before : p, z);
    if (!state.peek) S.onion?.(ctx, a, z);  // animation.js ghosts the neighbouring clip frames
    drawGrid(ctx, a.width, a.height, z);
    drawIssues(ctx, a.after_metrics, z);
    if (state.guides) drawGuides(ctx, a, z);
    S.overlay?.(ctx, a, z);  // flipbook.js: a clip frame's eye row, ground row and pivot
    if (state.mirror) { ctx.fillStyle = 'rgba(133,228,182,.8)'; ctx.fillRect(a.width * z / 2 - 1, 0, 2, a.height * z); }
    drawSelection(ctx, z);
    if (state.hover) {
      const [x, y] = state.hover;
      if (state.tool === 'pencil' && !stroke && state.color) {
        ctx.globalAlpha = 0.55; ctx.fillStyle = state.color; ctx.fillRect(x * z, y * z, z, z); ctx.globalAlpha = 1;
        if (z >= 4 && lowContrast(state.color, a)) { ctx.strokeStyle = backdropLuma(backdropFor(a)) > 128 ? '#000' : '#fff'; ctx.lineWidth = Math.max(1, z / 8); ctx.setLineDash([Math.max(2, z / 4), Math.max(2, z / 4)]); ctx.strokeRect(x * z + z / 4, y * z + z / 4, z / 2, z / 2); ctx.setLineDash([]); }
      }
      drawHover(ctx, z);
      if (state.kbd) { ctx.strokeStyle = '#85e4b6'; ctx.lineWidth = 2; ctx.strokeRect(x * z - 3, y * z - 3, z + 6, z + 6); }
    }
    S.afterDraw?.();  // flipbook.js: the paused preview and this frame's thumbnail
  }
  /* A paint colour that would barely show on the asset's backdrop (ink on black, say) gets a contrast ring. */
  const lowContrast = (hex, a) => !!hex && Math.abs(lumaOf(hex) - backdropLuma(backdropFor(a))) < 48;
  /* Centre guides: the sprite's middle row and column, and the centre of each 8 px tile. */
  function drawGuides(ctx, a, z) {
    ctx.fillStyle = 'rgba(245,199,100,.8)';
    ctx.fillRect(Math.round(a.width * z / 2) - 1, 0, 2, a.height * z); ctx.fillRect(0, Math.round(a.height * z / 2) - 1, a.width * z, 2);
    if (z < 4) return;
    ctx.fillStyle = 'rgba(245,199,100,.55)';
    for (let ty = 4; ty < a.height; ty += 8) for (let tx = 4; tx < a.width; tx += 8) { ctx.fillRect(tx * z - 3, ty * z, 6, 1); ctx.fillRect(tx * z, ty * z - 3, 1, 6); }
  }
  function drawSelection(ctx, z) {
    const r = state.sel; if (!r) return;
    ctx.lineWidth = 2; ctx.setLineDash([4, 4]);
    ctx.strokeStyle = '#000'; ctx.lineDashOffset = 0; ctx.strokeRect(r.x * z + 1, r.y * z + 1, r.w * z - 2, r.h * z - 2);
    ctx.strokeStyle = '#fff'; ctx.lineDashOffset = 4; ctx.strokeRect(r.x * z + 1, r.y * z + 1, r.w * z - 2, r.h * z - 2);
    ctx.setLineDash([]);
  }
  function renderReadout() {
    const el = document.getElementById('paint-readout'); if (!el || !view) return;
    const a = view.a, at = state.hover || (state.kbd ? state.cursor : null), parts = [];
    if (at) { const hex = pixel(work(a.key), at[0], at[1]); parts.push(`x ${at[0]}, y ${at[1]} · ${hex ? `${hex} ${label(hex, a)}` : 'transparent'}`); }
    if (state.sel) parts.push(`selection ${state.sel.w}×${state.sel.h} at ${state.sel.x}, ${state.sel.y}${state.float ? ' (floating)' : ''}`);
    if (stroke && !stroke.pointer) parts.push('Enter finishes · Esc cancels');
    el.textContent = parts.join(' · ') || 'Hover or focus the canvas';
  }
  function scrollToCursor() {
    const s = view.scroller, z = view.z, [x, y] = state.cursor;
    if (x * z < s.scrollLeft) s.scrollLeft = x * z - z; else if ((x + 1) * z > s.scrollLeft + s.clientWidth) s.scrollLeft = (x + 2) * z - s.clientWidth;
    if (y * z < s.scrollTop) s.scrollTop = y * z - z; else if ((y + 1) * z > s.scrollTop + s.clientHeight) s.scrollTop = (y + 2) * z - s.clientHeight;
  }
  function moveCursor(dx, dy, e) {
    const a = view.a, [x0, y0] = state.cursor, x = Math.max(0, Math.min(a.width - 1, x0 + dx)), y = Math.max(0, Math.min(a.height - 1, y0 + dy));
    state.kbd = true;
    if (e.shiftKey && !stroke && (state.tool === 'pencil' || state.tool === 'eraser')) press(x0, y0, state.tool, {pointer: false, held: true});
    state.cursor = [x, y]; state.hover = [x, y];
    if (stroke && !stroke.pointer) drag(x, y);
    scrollToCursor(); draw(); renderInspector(a); renderReadout();
    const hex = pixel(work(a.key), x, y); announce(`${x}, ${y} ${hex ? label(hex, a) : 'transparent'}`);
  }
  /* Enter (or Space once the keyboard cursor is in use) applies the tool at the cursor. */
  function applyAtCursor() {
    const [x, y] = state.cursor; state.kbd = true; state.hover = [x, y];
    if (stroke?.pointer) return;
    if (stroke) return release();
    press(x, y, state.tool, {pointer: false});
    if (stroke?.kind === 'free') release();
    else if (stroke) announce(`${stroke.kind === 'move' ? 'Moving' : 'Started'} at ${x}, ${y}; arrows to move, Enter to finish, Escape to cancel`);
    draw(); renderReadout();
  }
  function canvasKey(e) {
    const mod = e.metaKey || e.ctrlKey, dir = ARROWS[e.key];
    /* Arrows move the keyboard cursor while it shows; otherwise Left/Right change the clip frame (animation.js). */
    if (dir && e.altKey && !mod) nudge(...dir);
    else if (dir && !mod && !e.shiftKey && !state.kbd && !stroke && dir[0] && S.stepFrame?.(dir[0])) { /* changed frame */ }
    else if (dir && !mod) moveCursor(...dir, e);
    else if (e.key === 'Enter' && e.altKey && !stroke && state.tool !== 'select') { if (!e.repeat) pick(...state.cursor); }  // Alt+Enter picks without switching
    else if (e.key === 'Enter' || (e.key === ' ' && !e.shiftKey && (state.kbd || stroke))) { if (!e.repeat) applyAtCursor(); }
    else if (e.key === 'Escape' && (stroke || state.sel)) { if (!cancel()) deselect(); }
    else if (e.key === 'Escape' && state.kbd) { state.kbd = false; draw(); renderReadout(); announce(S.stepFrame ? 'Keyboard cursor hidden; Left and Right change frame' : 'Keyboard cursor hidden'); }
    else return;
    e.preventDefault(); e.stopPropagation();
  }
  S.renderStage = (a, z) => {
    const stage = document.getElementById('stage'), old = view;
    const hadFocus = old && document.activeElement === old.c, scroll = old && old.a.key === a.key ? [old.scroller.scrollLeft, old.scroller.scrollTop] : null;
    stage.textContent = '';
    document.getElementById('zoom-label').textContent = `· ${z}×`;
    if (LOCKED.has(a.kind)) { view = null; stage.innerHTML = '<p class="studio-note">The font atlas and backgrounds are not paintable here yet; edit those PNGs in an image editor.</p>'; return; }
    if (selKey !== a.key) {  // a keyboard artist stepping through clip frames keeps the cursor where it was
      const keep = hadFocus && state.kbd && state.cursor[0] < a.width && state.cursor[1] < a.height;
      selKey = a.key; stroke = null; state.sel = state.float = null; state.kbd = keep;
      if (!keep) state.cursor = [a.width >> 1, a.height >> 1];
    }
    ensure(a.key);
    /* Canvas on the left; a side column holds the colours, the flip-book (flipbook.js), the before image and history. */
    const work = document.createElement('div'); work.className = 'paint-work';
    const pane = document.createElement('div'); pane.className = 'pane paint-canvas';
    pane.innerHTML = '<div class="lbl"><span>paint</span><span class="paint-readout" id="paint-readout"></span></div>';
    const scroller = document.createElement('div'); scroller.className = 'scroller';
    const [c, ctx] = makeCanvas(a.width * z, a.height * z); c.style.cursor = 'crosshair'; c.style.touchAction = 'none';
    Object.assign(c, {tabIndex: 0}); c.setAttribute('role', 'application'); c.setAttribute('aria-roledescription', 'pixel canvas');
    c.setAttribute('aria-label', `${a.key}, ${a.width} by ${a.height} pixels`); c.setAttribute('aria-describedby', 'paint-hint');
    scroller.append(c); pane.append(scroller);
    pane.insertAdjacentHTML('beforeend', `<p class="studio-note paint-hint" id="paint-hint">Right-click erases · Alt-click picks · hold Space to peek at before · Ctrl/⌘+wheel zooms, middle-drag pans.
      Keyboard: Tab to the canvas to show the cursor; arrows move it, Enter or Space applies the tool, Shift+arrows draw, Esc hides it. <kbd>?</kbd> lists every shortcut.</p><div id="paint-live" class="vh" aria-live="polite"></div>`);
    const side = document.createElement('div'); side.className = 'paint-side';
    work.append(pane, side); stage.append(work);
    view = {c, ctx, a, z, scroller};
    const colours = document.getElementById('palette-block'); if (colours) side.append(colours);
    S.renderSide?.(side, a);  // flipbook.js: the clip's flip-book and onion skin
    const refs = document.createElement('div'); refs.className = 'paint-refs'; side.append(refs);
    const ref = decoded[a.key].before;
    if (ref) {
      const rz = Math.max(1, Math.min(4, Math.floor(96 / Math.max(a.width, a.height)))), box = document.createElement('div'); box.className = 'pane';
      box.innerHTML = `<div class="lbl"><span>before</span><span>${D.before_label}</span></div>`;
      const [rc, rctx] = makeCanvas(a.width * rz, a.height * rz); fillBackdrop(rctx, a.width, a.height, rz, backdropFor(a)); drawPixels(rctx, ref, rz);
      rc.setAttribute('role', 'img'); rc.setAttribute('aria-label', `${a.key} before (${D.before_label})`);
      box.append(rc); refs.append(box);
    }
    const hist = document.createElement('div'); hist.className = 'pane';
    hist.innerHTML = '<div class="lbl" id="paint-hist-label"><span>history</span><span>click a step to go back</span></div><ol class="paint-hist" id="paint-history" aria-labelledby="paint-hist-label"></ol>';
    hist.querySelector('ol').onclick = e => { const b = e.target.closest('[data-step]'); if (b) restore(Number(b.dataset.step)); };
    refs.append(hist);
    scroller.style.maxHeight = `${Math.max(240, window.innerHeight - Math.max(0, scroller.getBoundingClientRect().top) - 24)}px`;
    wireCanvas(c, scroller, a);
    if (anchor) { scroller.scrollLeft = anchor.px * z - anchor.ox; scroller.scrollTop = anchor.py * z - anchor.oy; anchor = null; }
    else if (scroll) [scroller.scrollLeft, scroller.scrollTop] = scroll;
    if (hadFocus) { pointing = true; c.focus({preventScroll: true}); pointing = false; }  // a rebuild keeps the cursor as it was
    draw(); renderReadout(); renderHistory();
  };
  function wireCanvas(c, scroller, a) {
    c.oncontextmenu = e => e.preventDefault();
    c.onpointerdown = e => {
      e.preventDefault(); pointing = true; c.focus({preventScroll: true}); pointing = false;
      try { c.setPointerCapture(e.pointerId); } catch { /* synthetic or already-released pointers cannot be captured */ }
      if (e.button === 1) { pan = {x: e.clientX, y: e.clientY, l: scroller.scrollLeft, t: scroller.scrollTop}; return; }
      if (stroke) return;
      const [x, y] = cell(e), erase = e.button === 2;
      const tool = erase && !SHAPES.has(state.tool) ? 'eraser' : e.altKey && state.tool !== 'select' ? 'picker' : state.tool;
      state.kbd = false; state.cursor = [x, y];
      press(x, y, tool, {erase, alt: e.altKey});
    };
    c.onpointermove = e => {
      if (pan) { scroller.scrollLeft = pan.l - (e.clientX - pan.x); scroller.scrollTop = pan.t - (e.clientY - pan.y); return; }
      const [x, y] = cell(e);
      if (state.hover?.[0] === x && state.hover?.[1] === y && !stroke) return;
      state.hover = [x, y]; state.kbd = false; state.cursor = [Math.max(0, Math.min(a.width - 1, x)), Math.max(0, Math.min(a.height - 1, y))];
      if (stroke?.pointer) drag(x, y, e.shiftKey); else draw();
      renderInspector(a); renderReadout();
    };
    const finish = () => { if (pan) { pan = null; return; } if (stroke?.pointer) release(); };
    c.onpointerup = finish; c.onpointercancel = () => { pan = null; if (stroke?.pointer) cancel(); };
    c.onpointerleave = () => { if (!stroke && !state.kbd) { state.hover = null; draw(); renderReadout(); } };
    c.onkeydown = canvasKey;
    c.onfocus = () => { if (!pointing && !state.kbd) { state.kbd = true; state.hover = [...state.cursor]; draw(); renderReadout(); } };
    c.onkeyup = e => { if (e.key === 'Shift' && stroke?.held) release(); };
    c.onblur = () => { if (stroke && !stroke.pointer) release(); };
    c.addEventListener('wheel', e => {
      if (!(e.ctrlKey || e.metaKey)) return;
      e.preventDefault(); wheel += e.deltaY; if (Math.abs(wheel) < 30) return;
      const z = view.z, next = Math.max(1, Math.min(32, z + (wheel < 0 ? 1 : -1))); wheel = 0;
      if (next === z) return;
      anchor = {px: e.offsetX / z, py: e.offsetY / z, ox: e.offsetX - scroller.scrollLeft, oy: e.offsetY - scroller.scrollTop};
      setZoom(next);
    }, {passive: false});
  }
  /* Rebuild a list while keeping keyboard focus on the matching control. */
  function keepFocus(el, attr, build, want) {
    const had = el.contains(document.activeElement) ? document.activeElement.getAttribute(attr) : null;
    build();
    if (had !== null) el.querySelector(`[${attr}="${want ?? had}"]`)?.focus({preventScroll: true});
  }
  function renderHistory() {
    const el = document.getElementById('paint-history'), e = edits[state.key]; if (!el || !e) return;
    keepFocus(el, 'data-step', () => {
      el.innerHTML = e.hist.entries.map((h, i) => `<li><button data-step="${i}" aria-current="${i === e.hist.at ? 'step' : 'false'}"${i > e.hist.at ? ' class="redo"' : ''}>${i + 1}. ${h.label}</button></li>`).join('');
    }, e.hist.at);
    const cur = el.querySelector('[aria-current=step]');
    if (cur) el.scrollTop = Math.max(0, cur.offsetTop - el.clientHeight / 2);
  }

  /* ---------- toolbar, header and palette ---------- */
  const btn = (id, text, title, keys, extra = '') => `<button id="${id}" title="${title}${keys ? ` (${keys})` : ''}"${keys ? ` aria-keyshortcuts="${keys.replace(/⌘/g, 'Meta+').replace(/⇧/g, 'Shift+')}"` : ''}${extra}>${text}</button>`;
  S.renderToolbar = extra => {
    if (LOCKED.has(asset().kind)) return;
    /* Everyday tools stay in view; the rest folds into More tools (remembered) so the canvas sits near the top. */
    extra.innerHTML = `<div class="paint-tools" role="toolbar" aria-label="Paint tools">
      <div><div class="lbl" id="tools-label">Tool</div><div class="seg" id="tools" role="group" aria-labelledby="tools-label">${TOOLS.map(([id, name, key, what]) =>
        `<button data-tool="${id}" title="${name} (${key}): ${what}" aria-keyshortcuts="${key}">${name}</button>`).join('')}</div></div>
      <div><div class="lbl">Options</div><div class="seg" role="group" aria-label="Options">${btn('filled', 'Filled', 'Filled rectangles and ellipses', '⇧F')}${btn('perfect', 'Pixel-perfect', 'Pencil strokes drop L-shaped corners', '⇧P')}${btn('mirror', 'Mirror', 'Mirror left/right', 'M')}</div></div>
      <div><div class="lbl">History</div><div class="seg" role="group" aria-label="History">${btn('undo', 'Undo', 'Undo', '⌘Z')}${btn('redo', 'Redo', 'Redo', '⇧⌘Z')}</div></div>
      <div><div class="lbl">File</div><div class="seg" role="group" aria-label="File">${btn('revert', 'Revert', 'Discard unsaved edits', '')}${btn('reset', 'Use before', 'Load the before image into the canvas', '')}${btn('save', 'Save', 'Save', '⌘S', ' class="primary"')}</div></div></div>
      <details class="paint-more" id="paint-more"${store.get('paint-more', false) ? ' open' : ''}><summary>More tools <span class="lbl">selection · transform · nudge · replace colour · guides · tidy</span></summary>
      <div class="paint-tools" role="toolbar" aria-label="More paint tools">
      <div><div class="lbl">Selection</div><div class="seg" role="group" aria-label="Selection">${btn('sel-all', 'All', 'Select the whole sprite', '⌘A')}${btn('sel-copy', 'Copy', 'Copy the selection, or the whole sprite', '⌘C')}${btn('sel-cut', 'Cut', 'Cut the selection', '⌘X')}${btn('sel-paste', 'Paste', 'Paste as a floating selection', '⌘V')}${btn('sel-delete', 'Delete', 'Clear the selected pixels', 'Delete')}${btn('sel-none', 'Deselect', 'Drop the selection', 'Escape')}</div></div>
      <div><div class="lbl">Transform</div><div class="seg" role="group" aria-label="Transform">${btn('flip-h', '⇋', 'Flip left/right: the selection, or the whole sprite', '⇧H', ' aria-label="Flip horizontally"')}${btn('flip-v', '⇅', 'Flip top/bottom: the selection, or the whole sprite', '⇧V', ' aria-label="Flip vertically"')}${btn('rot-cw', '↻', 'Rotate 90° clockwise', '⇧R', ' aria-label="Rotate clockwise"')}${btn('rot-ccw', '↺', 'Rotate 90° anticlockwise', '', ' aria-label="Rotate anticlockwise"')}</div></div>
      <div><div class="lbl">Nudge</div><div class="seg" role="group" aria-label="Nudge">${[['ArrowLeft', '←', 'left'], ['ArrowUp', '↑', 'up'], ['ArrowDown', '↓', 'down'], ['ArrowRight', '→', 'right']].map(([k, g, w]) =>
        btn(`nudge-${w}`, g, `Move the selection (or shift the sprite) 1 px ${w}`, `Alt+${k}`, ` aria-label="Nudge ${w}" data-nudge="${k}"`)).join('')}${btn('wrap', 'Wrap', 'Nudges wrap pixels round inside the selection or sprite', 'W')}</div></div>
      <div><div class="lbl"><label for="replace-from">Replace colour</label></div><div class="seg" role="group" aria-label="Replace colour"><select id="replace-from"></select>${btn('replace', '→ paint colour', 'Replace this colour with the paint colour (or transparent with the eraser), in the selection or the whole sprite', '')}</div></div>
      <div><div class="lbl">View</div><div class="seg" role="group" aria-label="View">${btn('guides', 'Guides', 'Centre and tile-centre guides; on a clip frame also its eye row, ground row and pivot', '⇧G')}${btn('fit-content', 'Fit content', 'Zoom to the drawn pixels plus a 2 px margin', 'Z')}</div></div>
      <div><div class="lbl">Clean up</div>${btn('tidy', 'Tidy outline', 'Closed 1px outline, remove specks, bottom shadow', '')}</div></div></details>`;
    extra.querySelector('#paint-more').ontoggle = e => store.set('paint-more', e.target.open);
    const on = (id, fn) => { extra.querySelector('#' + id).onclick = fn; };
    extra.querySelector('#tools').onclick = e => { const t = e.target.closest('button')?.dataset.tool; if (t) setTool(t); };
    on('filled', () => toggle('filled', 'Filled shapes')); on('perfect', () => toggle('perfect', 'Pixel-perfect')); on('mirror', () => toggle('mirror', 'Mirror'));
    on('sel-all', selectAll); on('sel-copy', copySel); on('sel-cut', cutSel); on('sel-paste', paste); on('sel-delete', deleteSel); on('sel-none', deselect);
    on('flip-h', () => transform('Flip horizontal', T.flipH)); on('flip-v', () => transform('Flip vertical', T.flipV));
    on('rot-cw', () => transform('Rotate clockwise', r => T.rotate(r, true))); on('rot-ccw', () => transform('Rotate anticlockwise', r => T.rotate(r, false)));
    extra.querySelectorAll('[data-nudge]').forEach(b => { b.onclick = () => nudge(...ARROWS[b.dataset.nudge]); });
    on('wrap', () => toggle('wrap', 'Wrap')); on('replace', replaceColour); on('guides', () => toggle('guides', 'Guides')); on('fit-content', fitContent);
    on('undo', undo); on('redo', redo);
    const tidyButton = extra.querySelector('#tidy');
    tidyButton.onclick = tidy;
    if (ownPalette(asset())) { tidyButton.disabled = true; tidyButton.title = 'Tidy inks with the shared palette; this sprite has its own palette'; }
    on('revert', () => { const e = edits[state.key]; if (e && dirty(state.key)) { state.float = null; work(state.key).data.set(e.saved); done('Revert'); } });
    on('reset', () => { const b = decoded[state.key].before; if (b) { state.float = null; work(state.key).data.set(b.data); done('Use before'); } });
    on('save', save);
    renderToolbarState();
  };
  function setTool(tool) {
    if (tool === state.tool) return;
    if (stroke) cancel();
    state.tool = tool; renderToolbarState(); renderPalette(asset()); draw(); renderReadout();
    announce(`${TOOLS.find(t => t[0] === tool)[1]} tool`);
  }
  function renderToolbarState() {
    document.querySelectorAll('#tools button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.tool === state.tool)));
    for (const id of ['mirror', 'filled', 'perfect', 'wrap', 'guides']) document.getElementById(id)?.setAttribute('aria-pressed', String(!!state[id]));
    const e = edits[state.key], isDirty = dirty(state.key), save = document.getElementById('save');
    if (save) { save.disabled = !isDirty; save.textContent = isDirty ? 'Save' : 'Saved'; }
    const able = (id, ok) => { const b = document.getElementById(id); if (b) b.disabled = !ok; };
    able('undo', e && e.hist.at > 0); able('redo', e && e.hist.at < e.hist.entries.length - 1);
    const p = work(state.key), sel = view && selRect();
    for (const id of ['sel-cut', 'sel-delete', 'sel-none']) able(id, !!sel);
    able('sel-paste', !!clipboard); able('rot-cw', sel || p?.w === p?.h); able('rot-ccw', sel || p?.w === p?.h);
    const from = document.getElementById('replace-from'); if (!from || !p) return;
    const a = asset(), keep = from.value, list = T.colours(p, sel || T.whole(p));
    from.innerHTML = list.map(([hex, n]) => `<option value="${hex}">${label(hex, a)} ${hex} · ${n} px</option>`).join('') || '<option value="">no colours</option>';
    if (list.some(([hex]) => hex === keep)) from.value = keep;
    able('replace', list.length > 0);
  }
  S.decorateHeader = (a, items) => {
    if (dirty(a.key)) items.push(['worse', 'unsaved edits']);
    if (a.hand_painted) items.push(['good', 'hand-painted · recipe leaves it alone']);
  };
  const activate = el => { el.onkeydown = e => { if ((e.key === 'Enter' || e.key === ' ') && e.target === el) { e.preventDefault(); e.stopPropagation(); el.click(); } }; };
  S.renderPalette = (el, a) => {
    const own = ownPalette(a), pal = paletteOf(a), where = paletteWhere(a);
    // Arriving at a sprite: keep a custom colour the artist chose, otherwise start from one of its palette colours.
    if (S.paletteKey !== a.key) { S.paletteKey = a.key; if (state.color && !pal.includes(state.color) && !state.custom) state.color = inkOf(a); }
    el.previousElementSibling.innerHTML = own ? `Paint colours <span class="lbl">· ${a.palette_name} palette · click to paint</span>`
      : 'Paint colours <span class="lbl">· click to paint · ✎ edits the shared palette</span>';
    const peers = D.assets.filter(x => own ? x.palette_name === a.palette_name : !ownPalette(x));
    const counts = a.after_metrics.colors, usage = hex => peers.filter(x => x.after_metrics.colors[hex]).length;
    const chip = (hex, title, sub, extra = '', custom = false) => {
      const b = document.createElement('div'); b.className = 'paint-chip' + extra; b.tabIndex = 0; b.title = title; b.dataset.chip = hex || 'eraser';
      const active = hex === null ? state.tool === 'eraser' : state.tool !== 'eraser' && state.color === hex;
      if (active && hex && lowContrast(hex, a)) { b.classList.add('faint'); title += ` · hard to see on the ${backdropFor(a)} backdrop`; b.title = title; }
      b.setAttribute('aria-pressed', String(active)); b.setAttribute('role', 'button'); b.setAttribute('aria-label', title);
      b.innerHTML = `<span class="sw ${hex ? '' : 'eraser-sw'}" style="${hex ? `background:${hex}` : ''}"></span><span>${title.split(' · ')[0]}</span><span>${sub}</span>`;
      b.onclick = e => {
        if (e.shiftKey && hex) { state.isolate = state.isolate === hex ? null : hex; return draw(); }
        if (hex === null) state.tool = 'eraser';
        else { state.color = hex; state.custom = custom; if (['eraser', 'picker', 'select'].includes(state.tool)) state.tool = 'pencil'; }
        S.renderPalette(el, a); renderToolbarState(); draw();
      };
      activate(b); el.append(b); return b;
    };
    keepFocus(el, 'data-chip', () => {
      el.textContent = '';
      pal.forEach((hex, i) => {
        const b = chip(hex, `${slotName(a, i)} · ${hex} · used by ${usage(hex)} assets`, counts[hex] ? `${counts[hex]} px` : '—');
        if (own) return;  // only the shared palette is editable here; a named palette is edited in assets.json
        // The ✎ button is the chip's sibling: a button inside a role=button chip is nested-interactive.
        const slot = document.createElement('div'); slot.className = 'paint-slot'; b.replaceWith(slot); slot.append(b);
        slot.insertAdjacentHTML('beforeend', `<button class="edit" title="Change ${NAMES[i]} everywhere" aria-label="Change ${NAMES[i]} in every sprite">✎</button><input type="color" value="${hex}" hidden>`);
        const input = slot.querySelector('input'); slot.querySelector('.edit').onclick = e => { e.stopPropagation(); input.click(); };
        input.onchange = e => editSlot(i, e.target.value);
      });
      chip(null, 'eraser · transparent', 'right-click');
      for (const hex of Object.keys(counts).filter(h => !pal.includes(h)))
        chip(hex, `custom · ${hex} · not in ${where}; release builds reject it until a slot uses this colour`, `${counts[hex]} px`, ' off', true);
      if (!pal.includes(state.color) && state.color && !counts[state.color])
        chip(state.color, `custom · ${state.color} · not in ${where}`, state.color.slice(1), ' off', true);
      const custom = document.createElement('div'); custom.className = 'paint-chip'; custom.title = 'Paint with any colour, outside the palette';
      custom.tabIndex = 0; custom.setAttribute('role', 'button'); custom.setAttribute('aria-label', 'Choose a custom colour'); custom.dataset.chip = 'custom';
      custom.innerHTML = `<span class="sw" style="background:conic-gradient(#fa8c99,#f5c764,#85e4b6,#a47bdb,#fa8c99)"></span><span>custom</span><span>pick…</span><input type="color" value="${state.color || '#ffffff'}" hidden>`;
      const input = custom.querySelector('input'); custom.onclick = e => { if (e.target !== input) input.click(); };
      input.onchange = e => { state.color = e.target.value.toLowerCase(); state.custom = true; state.tool = 'pencil'; S.renderPalette(el, a); renderToolbarState(); draw(); };
      activate(custom); el.append(custom);
      const note = document.createElement('p'); note.className = 'studio-note';
      note.textContent = own
        ? `Click a colour to paint with it. This sprite uses ${where} from assets.json, so the shared palette and its ✎ do not apply. Tools paint only these colours until you choose custom; custom colours work in the live game only. Shift-click isolates a colour.`
        : 'Click a colour to paint with it. ✎ changes that palette colour in every sprite. Tools paint only palette colours until you choose custom; custom colours work in the live game, and to ship one, put it in a palette slot. Shift-click isolates a colour.';
      el.append(note);
    });
  };

  /* ---------- shortcut docs: the shell's ? overlay lists them; the handlers are S.keydown and canvasKey ---------- */
  window.JelliShell?.registerShortcuts('Paint', [
    {keys: ['P', 'E', 'F', 'C'], description: 'Pencil, eraser, fill, pick colour'}, {keys: ['L', 'R', 'U'], description: 'Line, rectangle, ellipse'},
    {keys: ['V'], description: 'Select'}, {keys: ['Shift+F'], description: 'Filled shapes on/off'}, {keys: ['Shift+P'], description: 'Pixel-perfect pencil on/off'},
    {keys: ['M'], description: 'Mirror left/right'}, {keys: ['Right-click'], description: 'Erase'}, {keys: ['Alt+Click', 'Alt+Enter'], description: 'Pick a colour and keep the current tool (any tool except Select)'},
    {keys: ['Shift+Drag'], description: 'Snap lines to 45°; square rectangles and circles'},
    {keys: ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'], description: 'Canvas focused: move the keyboard cursor'},
    {keys: ['Enter', 'Space'], description: 'Canvas focused: apply the tool; shapes and select take one press to start and one to finish'},
    {keys: ['Shift+ArrowRight'], description: 'Draw with the pencil or eraser while moving the cursor'},
    {keys: ['Escape'], description: 'Cancel the shape, then clear the selection'},
    {keys: ['Mod+A'], description: 'Select all'}, {keys: ['Mod+C', 'Mod+X', 'Mod+V'], description: 'Copy, cut, paste (paste floats; palette-checked)'},
    {keys: ['Delete', 'Backspace'], description: 'Clear the selected pixels'}, {keys: ['Alt+Drag'], description: 'Copy the selection while moving it'},
    {keys: ['Alt+ArrowLeft', 'Alt+ArrowRight', 'Alt+ArrowUp', 'Alt+ArrowDown'], description: 'Move the selection 1 px, or shift the sprite'},
    {keys: ['W'], description: 'Wrap nudges round inside the selection or sprite'},
    {keys: ['Shift+H', 'Shift+V'], description: 'Flip horizontally, vertically (selection or sprite)'}, {keys: ['Shift+R'], description: 'Rotate 90° clockwise'},
    {keys: ['[', ']', '0'], description: 'Zoom out, in, fit'}, {keys: ['Mod+Wheel'], description: 'Zoom at the pointer'}, {keys: ['Middle-drag'], description: 'Pan'},
    {keys: ['Z'], description: 'Fit content: zoom to the drawn pixels'},
    {keys: ['G'], description: 'Pixel grid'}, {keys: ['Shift+G'], description: 'Centre and tile-centre guides (a clip frame adds its eye row, ground row and pivot)'},
    {keys: ['Space'], description: 'Hold to peek at the before image (until the keyboard cursor is in use)'},
    {keys: ['Mod+Z'], description: 'Undo; the history list jumps to any of the last 100 steps'}, {keys: ['Mod+Shift+Z', 'Mod+Y'], description: 'Redo'},
    {keys: ['Mod+S'], description: 'Save the asset'}]);

  /* ---------- server-backed actions ---------- */
  async function save() {
    const a = asset(), p = work(a.key); if (!dirty(a.key)) return;
    if (!p.data.some((v, i) => i % 4 === 3 && v)) return status('An asset needs at least one pixel', 'bad');
    const pixels = []; for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) pixels.push(pixel(p, x, y));
    try {
      const e = edits[a.key], send = base => api('POST', '/api/save', {key: a.key, pixels, artist: state.artist, base});
      const res = await send(e.base ?? a.sha).catch(async err => {
        if (err.status !== 409 || !await askOverwrite(a.key)) throw err;  // Cancel keeps the newer file on disk
        return send(err.body.current);  // the 409 names the hash now on disk
      });
      e.saved = p.data.slice(); e.base = a.sha = res.sha; a.hand_painted = true; D.version = res.version;
      window.JelliDrafts?.drop('paint', a.key);
      renderHeader(a); renderToolbarState(); renderList();
      if (res.git_error) status(`Saved ${a.key}, but the commit failed: ${res.git_error}`, 'bad', true);
      else status(res.commit ? `Saved ${a.key} (commit ${res.commit}).` : `Saved ${a.key}. With make run-live, the game shows it now.`, '', false, {keep: true});
    } catch (err) { status(`Save failed: ${err.message}`, 'bad', true); }
  }
  async function askOverwrite(key) {
    const text = `${key} changed on disk since you started editing it. Overwrite it with your version?`;
    return ask(text, {title: 'File changed on disk', confirmLabel: 'Overwrite', danger: true});
  }
  async function tidy() {
    const a = asset(), p = work(a.key), pixels = [];
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) pixels.push(pixel(p, x, y));
    try {
      const res = await api('POST', '/api/tidy', {key: a.key, pixels}); state.float = null;
      res.pixels.forEach((hex, i) => T.set(p, i % p.w, Math.floor(i / p.w), hex)); done('Tidy outline');
      status('Tidied: closed outline, specks removed. Undo if you prefer the old version.');
    } catch (err) { status(`Tidy failed: ${err.message}`, 'bad', true); }
  }
  async function editSlot(index, color) {
    color = color.toLowerCase(); const old = D.palette[index];
    if (color === old) return;
    if (Object.keys(edits).some(dirty)) return status('Save or revert your edits before changing the palette', 'warn', true);
    const users = count(D.assets.filter(x => x.after_metrics.colors[old]).length, 'asset');
    const text = `Change ${NAMES[index]} from ${old} to ${color} in every sprite?\n\n${users} use it. This rewrites their PNGs and the shared palette; git can undo it.`;
    if (!await ask(text, {title: `Change ${NAMES[index]} in ${users}`, confirmLabel: 'Change colour', danger: true})) return S.renderPalette(document.getElementById('palette'), asset());
    // The dialog does not block the page, so a reload may have changed the slot meanwhile; the server checks `base` too.
    if (D.palette[index] !== old) { rerender(); return status(`${NAMES[index]} changed to ${D.palette[index]} while you were deciding; nothing was rewritten`, 'warn', true); }
    try {
      const res = await api('POST', '/api/palette', {index, color, base: old, artist: state.artist});
      if (state.color === old) state.color = color;
      await reload(); rerender(); status(`${NAMES[index]} is now ${color} in ${count(res.changed.length, 'asset')}`, '', false, {keep: true});
    } catch (err) {
      if (err.status !== 409) return status(`Palette change failed: ${err.message}`, 'bad', true);
      await reload(); rerender();
      status(`${NAMES[index]} is now ${err.body.current} on disk, so nothing was rewritten. Pick the new colour again if you still want it.`, 'warn', true);
    }
  }

  /* ---------- keyboard ---------- */
  const SHIFTED = {h: () => transform('Flip horizontal', T.flipH), v: () => transform('Flip vertical', T.flipV), r: () => transform('Rotate clockwise', r => T.rotate(r, true)),
    f: () => toggle('filled', 'Filled shapes'), p: () => toggle('perfect', 'Pixel-perfect'), g: () => toggle('guides', 'Guides')};
  const MODDED = {a: selectAll, c: copySel, x: cutSel, v: paste};
  S.keydown = e => {
    const mod = e.metaKey || e.ctrlKey, k = e.key.toLowerCase();
    if (e.repeat && !(mod && k === 'z')) return false;
    if (mod && k === 's') { e.preventDefault(); save(); return true; }
    if (mod && k === 'z') { e.preventDefault(); e.shiftKey ? redo() : undo(); return true; }
    if (mod && k === 'y') { e.preventDefault(); redo(); return true; }
    if (state.mode !== 'paint' || state.view !== 'detail' || !view) return false;
    if (mod && !e.altKey && !e.shiftKey && MODDED[k]) { e.preventDefault(); MODDED[k](); return true; }
    if (mod) return false;
    if (e.altKey && ARROWS[e.key]) { e.preventDefault(); nudge(...ARROWS[e.key]); return true; }
    if (e.altKey) return false;
    if (e.key === 'Escape') return cancel() || (state.sel ? (deselect(), true) : false);
    if (e.key === 'Delete' || e.key === 'Backspace') { e.preventDefault(); return deleteSel(); }
    if (e.shiftKey) { if (SHIFTED[k]) { SHIFTED[k](); return true; } return false; }
    const tool = {p: 'pencil', e: 'eraser', f: 'fill', c: 'picker', l: 'line', r: 'rect', u: 'ellipse', v: 'select'}[k];
    if (tool) { setTool(tool); return true; }
    if (k === 'm') { toggle('mirror', 'Mirror'); return true; }
    if (k === 'w') { toggle('wrap', 'Wrap'); return true; }
    if (k === 'z') { fitContent(); return true; }
    return false;
  };
})();

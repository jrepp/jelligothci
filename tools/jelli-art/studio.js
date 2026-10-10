/* Jelli Art: painting, palette editing and live reload for compare.html.
 * Injected only by tools/jelli-art/jelli_art.py; the static review page never loads it.
 * Shares the page's globals (state, D, decoded, renderers) and adds a Paint mode. */
'use strict';
(() => {
  const S = window.Studio = {};
  const OUTLINED = new Set(['creatures', 'icons', 'menus', 'meters', 'health', 'effects', 'prizes', 'props']);
  const LOCKED = new Set(['font', 'backgrounds']);
  const TOOLS = [['pencil', 'Pencil', 'P'], ['eraser', 'Eraser', 'E'], ['fill', 'Fill', 'F'], ['picker', 'Pick colour', 'C']];
  const edits = {};  // key -> {saved, undo: [], redo: []}; the working buffer is decoded[key].after
  let stroke = null, view = null, statusTimer = null;
  Object.assign(state, {tool: 'pencil', color: '#291b35', mirror: false, before: store.get('before', 'HEAD'), artist: store.get('artist', '')});
  MODES.push('paint');
  document.getElementById('modes').insertAdjacentHTML('beforeend', '<button data-mode="paint">Paint</button>');
  document.querySelector('header .refs').insertAdjacentHTML('afterend',
    '<span class="live-badge" id="live-badge">● live</span><label class="lbl" for="before-ref">compare with</label><select id="before-ref"></select>' +
    '<input id="artist" class="artist" placeholder="Your name (for history)" aria-label="Your name, recorded with saves"><span id="git-status" class="studio-status"></span><span id="studio-status" class="studio-status" role="status"></span>');
  document.head.insertAdjacentHTML('beforeend', `<style>
    .live-badge{font:600 11px ui-monospace,monospace;color:var(--accent)}
    select{font:inherit;color:var(--ink);background:var(--raised);border:1px solid var(--line);border-radius:6px;padding:4px 6px;max-width:220px}
    .studio-status{font-size:12px;color:var(--muted)}.studio-status.warn{color:var(--warn)}.studio-status.bad{color:var(--bad)}
    button.primary{background:var(--accent);color:#1b1b1f;border-color:var(--accent);font-weight:600}
    button.primary:disabled{opacity:.45;cursor:default}
    .paint-chip{display:grid;grid-template-columns:auto;justify-items:center;gap:3px;background:var(--raised);border:2px solid transparent;border-radius:8px;padding:6px;cursor:pointer;min-width:62px;font:10px ui-monospace,monospace;color:var(--muted);position:relative}
    .paint-chip .sw{width:34px;height:34px;border-radius:6px;border:1px solid #fff3}
    .paint-chip[aria-pressed=true]{border-color:var(--ink);color:var(--ink)}
    .paint-chip .edit{position:absolute;top:2px;right:2px;padding:0 4px;font-size:11px;line-height:16px;border-radius:4px;background:var(--panel)}
    .paint-chip.off .sw{outline:2px solid var(--bad);outline-offset:1px}
    .eraser-sw{background:repeating-conic-gradient(#555 0 25%,#2b2b31 0 50%) 0 0/10px 10px}
    .studio-note{font-size:12px;color:var(--muted);max-width:760px;margin:6px 0}
    .artist{width:170px;padding:4px 8px}
  </style>`);

  /* ---------- server calls ---------- */
  async function api(method, url, body) {
    const res = await fetch(url, {method, headers: body ? {'Content-Type': 'application/json'} : {}, body: body ? JSON.stringify(body) : undefined});
    const data = await res.json().catch(() => ({error: res.statusText}));
    if (!res.ok) throw new Error(data.error || res.statusText);
    return data;
  }
  function status(text, cls = '', sticky = false) {
    const el = document.getElementById('studio-status'); el.textContent = text; el.className = 'studio-status ' + cls;
    clearTimeout(statusTimer); if (!sticky) statusTimer = setTimeout(() => { el.textContent = ''; }, 4000);
  }

  /* ---------- loading and live reload ---------- */
  async function reload() {
    const keep = Object.entries(edits).filter(([key]) => dirty(key)).map(([key]) => [key, decoded[key].after.data.slice()]);
    await loadPayload(await api('GET', `/api/data?before=${encodeURIComponent(state.before)}`));
    // The server measures with the shared ink; re-measure sprites outlined from their own palette.
    for (const a of D.assets.filter(x => ownPalette(x) && !LOCKED.has(x.kind))) {
      if (decoded[a.key].after) a.after_metrics = measure(decoded[a.key].after, a);
      if (decoded[a.key].before) a.before_metrics = measure(decoded[a.key].before, a);
    }
    for (const key of Object.keys(edits)) {
      if (!decoded[key]?.after) { delete edits[key]; continue; }
      edits[key].saved = decoded[key].after.data.slice();
    }
    for (const [key, data] of keep) if (decoded[key]?.after) { decoded[key].after.data.set(data); refresh(key); }
  }
  S.boot = async () => {
    await reload();
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
  Object.assign(S, {api, status, reload, rerender: () => rerender(), renderGit, paintDirty: key => dirty(key)});
  function renderGit(git) {
    const el = document.getElementById('git-status'); if (!git?.enabled) { el.textContent = ''; return; }
    const push = git.last_push || {}, pending = git.unpushed ? `${git.unpushed} to push` : 'synced';
    el.textContent = `⎇ ${git.branch} · ${push.ok === false ? 'push failing, will retry' : git.push ? pending : 'commits only'}`;
    el.className = 'studio-status' + (push.ok === false ? ' bad' : '');
    el.title = push.error || `Every save is committed to ${git.branch}${git.push ? ' and pushed to GitHub for review' : ''}.`;
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
  const ensure = key => (edits[key] ||= {saved: work(key).data.slice(), undo: [], redo: []});
  function setPixel(p, x, y, hex) {
    if (x < 0 || y < 0 || x >= p.w || y >= p.h) return;
    const i = (y * p.w + x) * 4;
    if (!hex) { p.data[i] = p.data[i + 1] = p.data[i + 2] = p.data[i + 3] = 0; return; }
    p.data[i] = parseInt(hex.slice(1, 3), 16); p.data[i + 1] = parseInt(hex.slice(3, 5), 16); p.data[i + 2] = parseInt(hex.slice(5, 7), 16); p.data[i + 3] = 255;
  }
  /* Assets may carry their own palette (a name in manifest.palettes or an inline list). */
  const ownPalette = a => !!a.palette_name && a.palette_name !== 'shared';
  const paletteOf = a => a.palette || D.palette;
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
  function commit() {
    const a = asset(); refresh(a.key);
    renderHeader(a); renderPalette(a); renderContext(a); renderTotals(); renderList(); draw(); renderToolbarState();
  }
  function begin() { const e = ensure(state.key); e.undo.push(work(state.key).data.slice()); if (e.undo.length > 200) e.undo.shift(); e.redo = []; }
  function history(from, to) {
    const e = edits[state.key]; if (!e || !e[from].length) return;
    const p = work(state.key); e[to].push(p.data.slice()); p.data.set(e[from].pop()); commit();
  }

  /* ---------- tools ---------- */
  function plot(x, y, hex) { const p = work(state.key); setPixel(p, x, y, hex); if (state.mirror) setPixel(p, p.w - 1 - x, y, hex); }
  function line(x0, y0, x1, y1, hex) {
    const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1; let err = dx + dy;
    for (;;) { plot(x0, y0, hex); if (x0 === x1 && y0 === y1) return; const e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
  }
  function flood(x, y, hex) {
    const p = work(state.key), target = pixel(p, x, y); if (target === hex) return;
    const stack = [[x, y]], seen = new Set();
    while (stack.length) {
      const [cx, cy] = stack.pop(), id = cy * p.w + cx;
      if (cx < 0 || cy < 0 || cx >= p.w || cy >= p.h || seen.has(id) || pixel(p, cx, cy) !== target) continue;
      seen.add(id); setPixel(p, cx, cy, hex); stack.push([cx + 1, cy], [cx - 1, cy], [cx, cy + 1], [cx, cy - 1]);
    }
  }
  function pick(x, y) {
    const hex = pixel(work(state.key), x, y);
    if (hex) { state.color = hex; state.tool = 'pencil'; } else state.tool = 'eraser';
    renderPalette(asset()); renderToolbarState();
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
    if (state.mirror) { ctx.fillStyle = 'rgba(133,228,182,.8)'; ctx.fillRect(a.width * z / 2 - 1, 0, 2, a.height * z); }
    if (state.hover) {
      const [x, y] = state.hover;
      if (!['picker'].includes(state.tool) && state.color && state.tool === 'pencil') { ctx.globalAlpha = 0.55; ctx.fillStyle = state.color; ctx.fillRect(x * z, y * z, z, z); ctx.globalAlpha = 1; }
      drawHover(ctx, z);
    }
  }
  S.renderStage = (a, z) => {
    const stage = document.getElementById('stage'); stage.textContent = '';
    document.getElementById('zoom-label').textContent = `· ${z}×`;
    if (LOCKED.has(a.kind)) { view = null; stage.innerHTML = '<p class="studio-note">The font atlas and backgrounds are not paintable here yet; edit those PNGs in an image editor.</p>'; return; }
    const pane = document.createElement('div'); pane.className = 'pane';
    pane.innerHTML = '<div class="lbl"><span>paint</span><span>right-click erases · Alt-click picks · hold Space to peek</span></div>';
    const scroller = document.createElement('div'); scroller.className = 'scroller'; scroller.style.maxHeight = `${window.innerHeight - 200}px`;
    const [c, ctx] = makeCanvas(a.width * z, a.height * z); c.style.cursor = 'crosshair'; c.style.touchAction = 'none';
    scroller.append(c); pane.append(scroller); stage.append(pane);
    view = {c, ctx, a, z};
    const ref = decoded[a.key].before;
    if (ref) {
      const rz = Math.max(2, Math.floor(z / 3)), box = document.createElement('div'); box.className = 'pane';
      box.innerHTML = `<div class="lbl"><span>before</span><span>${D.before_label}</span></div>`;
      const [rc, rctx] = makeCanvas(a.width * rz, a.height * rz); fillBackdrop(rctx, a.width, a.height, rz, backdropFor(a)); drawPixels(rctx, ref, rz);
      box.append(rc); stage.append(box);
    }
    c.oncontextmenu = e => e.preventDefault();
    c.onpointerdown = e => {
      e.preventDefault(); c.setPointerCapture(e.pointerId);
      const [x, y] = cell(e), tool = e.button === 2 ? 'eraser' : e.altKey ? 'picker' : state.tool;
      if (tool === 'picker') return pick(x, y);
      begin();
      if (tool === 'fill') { flood(x, y, state.color); if (state.mirror) flood(a.width - 1 - x, y, state.color); return commit(); }
      stroke = {hex: tool === 'eraser' ? null : state.color, last: [x, y]}; plot(x, y, stroke.hex); draw();
    };
    c.onpointermove = e => {
      const [x, y] = cell(e); state.hover = [x, y];
      if (stroke) { line(stroke.last[0], stroke.last[1], x, y, stroke.hex); stroke.last = [x, y]; }
      draw(); renderInspector(a);
    };
    const finish = () => { if (stroke) { stroke = null; commit(); } };
    c.onpointerup = finish; c.onpointercancel = finish;
    c.onpointerleave = () => { if (!stroke) { state.hover = null; draw(); } };
    draw();
  };

  /* ---------- toolbar, header and palette ---------- */
  S.renderToolbar = extra => {
    if (LOCKED.has(asset().kind)) return;
    extra.innerHTML = `<div style="display:flex;gap:14px;flex-wrap:wrap;align-items:end">
      <div><div class="lbl">Tool</div><div class="seg" id="tools">${TOOLS.map(([id, name, key]) => `<button data-tool="${id}" title="${name} (${key})">${name}</button>`).join('')}</div></div>
      <div><div class="lbl">Symmetry</div><button id="mirror" title="Mirror left/right (M)">Mirror</button></div>
      <div><div class="lbl">History</div><div class="seg"><button id="undo" title="Undo (⌘Z)">Undo</button><button id="redo" title="Redo (⇧⌘Z)">Redo</button></div></div>
      <div><div class="lbl">Clean up</div><button id="tidy" title="Closed 1px outline, remove specks, bottom shadow">Tidy outline</button></div>
      <div><div class="lbl">File</div><div class="seg"><button id="revert" title="Discard unsaved edits">Revert</button><button id="reset" title="Load the before image into the canvas">Use before</button><button id="save" class="primary" title="Save (⌘S)">Save</button></div></div></div>`;
    extra.querySelector('#tools').onclick = e => { const t = e.target.closest('button')?.dataset.tool; if (t) { state.tool = t; renderToolbarState(); draw(); } };
    extra.querySelector('#mirror').onclick = () => { state.mirror = !state.mirror; renderToolbarState(); draw(); };
    extra.querySelector('#undo').onclick = () => history('undo', 'redo');
    extra.querySelector('#redo').onclick = () => history('redo', 'undo');
    const tidyButton = extra.querySelector('#tidy');
    tidyButton.onclick = tidy;
    if (ownPalette(asset())) { tidyButton.disabled = true; tidyButton.title = 'Tidy inks with the shared palette; this sprite has its own palette'; }
    extra.querySelector('#revert').onclick = () => { const e = edits[state.key]; if (e && dirty(state.key)) { begin(); work(state.key).data.set(e.saved); commit(); } };
    extra.querySelector('#reset').onclick = () => { const b = decoded[state.key].before; if (b) { begin(); work(state.key).data.set(b.data); commit(); } };
    extra.querySelector('#save').onclick = save;
    renderToolbarState();
  };
  function renderToolbarState() {
    document.querySelectorAll('#tools button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.tool === state.tool)));
    document.getElementById('mirror')?.setAttribute('aria-pressed', String(state.mirror));
    const e = edits[state.key], isDirty = dirty(state.key), save = document.getElementById('save');
    if (save) { save.disabled = !isDirty; save.textContent = isDirty ? 'Save' : 'Saved'; }
    const undo = document.getElementById('undo'), redo = document.getElementById('redo');
    if (undo) undo.disabled = !e?.undo.length; if (redo) redo.disabled = !e?.redo.length;
  }
  S.decorateHeader = (a, items) => {
    if (dirty(a.key)) items.push(['worse', 'unsaved edits']);
    if (a.hand_painted) items.push(['good', 'hand-painted · recipe leaves it alone']);
  };
  S.renderPalette = (el, a) => {
    el.textContent = '';
    const own = ownPalette(a), pal = paletteOf(a), where = own ? `the ${a.palette_name} palette` : 'the shared palette';
    // Arriving at a sprite with its own palette: start from one of its colours, not the shared ink.
    if (S.paletteKey !== a.key) { S.paletteKey = a.key; if (state.color && !pal.includes(state.color)) state.color = inkOf(a); }
    el.previousElementSibling.innerHTML = own ? `Paint colours <span class="lbl">· ${a.palette_name} palette · click to paint</span>`
      : 'Paint colours <span class="lbl">· click to paint · ✎ edits the shared palette</span>';
    const peers = D.assets.filter(x => own ? x.palette_name === a.palette_name : !ownPalette(x));
    const counts = a.after_metrics.colors, usage = hex => peers.filter(x => x.after_metrics.colors[hex]).length;
    const chip = (hex, title, sub, extra = '') => {
      const b = document.createElement('div'); b.className = 'paint-chip' + extra; b.tabIndex = 0; b.title = title;
      const active = hex === null ? state.tool === 'eraser' : state.tool !== 'eraser' && state.color === hex;
      b.setAttribute('aria-pressed', String(active)); b.setAttribute('role', 'button');
      b.innerHTML = `<span class="sw ${hex ? '' : 'eraser-sw'}" style="${hex ? `background:${hex}` : ''}"></span><span>${title.split(' · ')[0]}</span><span>${sub}</span>`;
      b.onclick = e => {
        if (e.target.closest('.edit')) return;
        if (e.shiftKey && hex) { state.isolate = state.isolate === hex ? null : hex; return draw(); }
        if (hex === null) state.tool = 'eraser'; else { state.color = hex; if (state.tool === 'eraser' || state.tool === 'picker') state.tool = 'pencil'; }
        S.renderPalette(el, a); renderToolbarState(); draw();
      };
      el.append(b); return b;
    };
    pal.forEach((hex, i) => {
      const b = chip(hex, `${slotName(a, i)} · ${hex} · used by ${usage(hex)} assets`, counts[hex] ? `${counts[hex]} px` : '—');
      if (own) return;  // only the shared palette is editable here; a named palette is edited in assets.json
      b.insertAdjacentHTML('beforeend', `<label class="edit" title="Change this palette colour everywhere">✎<input type="color" value="${hex}" hidden></label>`);
      b.querySelector('input').onchange = e => editSlot(i, e.target.value);
    });
    chip(null, 'eraser · transparent', 'right-click');
    for (const hex of Object.keys(counts).filter(h => !pal.includes(h)))
      chip(hex, `custom · ${hex} · not in ${where}; release builds reject it until a slot uses this colour`, `${counts[hex]} px`, ' off');
    if (!pal.includes(state.color) && state.color && !counts[state.color])
      chip(state.color, `custom · ${state.color} · not in ${where}`, state.color.slice(1), ' off');
    const custom = document.createElement('label'); custom.className = 'paint-chip'; custom.title = 'Paint with any colour';
    custom.innerHTML = `<span class="sw" style="background:conic-gradient(#fa8c99,#f5c764,#85e4b6,#a47bdb,#fa8c99)"></span><span>custom</span><span>pick…</span><input type="color" value="${state.color || '#ffffff'}" hidden>`;
    custom.querySelector('input').onchange = e => { state.color = e.target.value.toLowerCase(); state.tool = 'pencil'; S.renderPalette(el, a); renderToolbarState(); draw(); };
    el.append(custom);
    const note = document.createElement('p'); note.className = 'studio-note';
    note.textContent = own
      ? `Click a colour to paint with it. This sprite uses ${where} from assets.json, so the shared palette and its ✎ do not apply. Custom colours work in the live game only. Shift-click isolates a colour.`
      : 'Click a colour to paint with it. ✎ changes that palette colour in every sprite. Custom colours work in the live game; to ship one, put it in a palette slot. Shift-click isolates a colour.';
    el.append(note);
  };

  /* ---------- server-backed actions ---------- */
  async function save() {
    const a = asset(), p = work(a.key); if (!dirty(a.key)) return;
    if (!p.data.some((v, i) => i % 4 === 3 && v)) return status('An asset needs at least one pixel', 'bad');
    const pixels = []; for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) pixels.push(pixel(p, x, y));
    try {
      const res = await api('POST', '/api/save', {key: a.key, pixels, artist: state.artist});
      edits[a.key].saved = p.data.slice(); a.hand_painted = true; D.version = res.version;
      renderHeader(a); renderToolbarState(); renderList();
      if (res.git_error) status(`Saved ${a.key}, but the commit failed: ${res.git_error}`, 'bad', true);
      else status(res.commit ? `Saved ${a.key} (commit ${res.commit}).` : `Saved ${a.key}. With make run-live, the game shows it now.`);
    } catch (err) { status(`Save failed: ${err.message}`, 'bad', true); }
  }
  async function tidy() {
    const a = asset(), p = work(a.key), pixels = [];
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) pixels.push(pixel(p, x, y));
    try {
      const res = await api('POST', '/api/tidy', {key: a.key, pixels}); begin();
      res.pixels.forEach((hex, i) => setPixel(p, i % p.w, Math.floor(i / p.w), hex)); commit();
      status('Tidied: closed outline, specks removed. Undo if you prefer the old version.');
    } catch (err) { status(`Tidy failed: ${err.message}`, 'bad', true); }
  }
  async function editSlot(index, color) {
    color = color.toLowerCase(); const old = D.palette[index];
    if (color === old) return;
    if (Object.keys(edits).some(dirty)) return status('Save or revert your edits before changing the palette', 'warn', true);
    const users = D.assets.filter(x => x.after_metrics.colors[old]).length;
    if (!confirm(`Change ${NAMES[index]} from ${old} to ${color} in every sprite?\n\n${users} assets use it. This rewrites their PNGs and the shared palette; git can undo it.`)) return S.renderPalette(document.getElementById('palette'), asset());
    try {
      const res = await api('POST', '/api/palette', {index, color, artist: state.artist});
      if (state.color === old) state.color = color;
      await reload(); rerender(); status(`${NAMES[index]} is now ${color} in ${res.changed.length} assets`);
    } catch (err) { status(`Palette change failed: ${err.message}`, 'bad', true); }
  }

  /* ---------- keyboard ---------- */
  S.keydown = e => {
    const mod = e.metaKey || e.ctrlKey, k = e.key.toLowerCase();
    if (e.repeat && !(mod && k === 'z')) return false;
    if (mod && k === 's') { e.preventDefault(); save(); return true; }
    if (mod && k === 'z') { e.preventDefault(); history(e.shiftKey ? 'redo' : 'undo', e.shiftKey ? 'undo' : 'redo'); return true; }
    if (mod && k === 'y') { e.preventDefault(); history('redo', 'undo'); return true; }
    if (state.mode !== 'paint' || mod || e.altKey) return false;
    const tool = {p: 'pencil', e: 'eraser', f: 'fill', c: 'picker'}[k];
    if (tool) { state.tool = tool; renderToolbarState(); renderPalette(asset()); draw(); return true; }
    if (k === 'm') { state.mirror = !state.mirror; renderToolbarState(); draw(); return true; }
    return false;
  };
})();

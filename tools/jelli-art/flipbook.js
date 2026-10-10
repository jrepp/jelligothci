/* Jelli Art: the flip-book beside the Paint canvas when the sprite is a frame of a creature clip.
 * Loaded after animation.js. A live preview plays the clip with its real timing from the working
 * pixels, so unsaved strokes animate; a strip of frame thumbnails jumps between frames; onion skin
 * ghosts up to three frames either side. Keys: Left/Right (while the keyboard cursor is hidden)
 * or , and . change frame, Shift+Space plays or pauses, O toggles onion skin and Shift+O switches
 * it between changes only and whole silhouettes. With Guides on, the canvas and the preview show the
 * clip's eye row, ground row and pivot, and frames that move off them are flagged. */
'use strict';
(() => {
  const S = window.Studio, A = window.JelliAnimation, cr = S?.cr, T = window.JelliPaint;
  if (!S || !cr || !A?.paintContext) return;
  /* Amber before, sky after: apart in hue and lightness, and for red-green colour blindness. */
  const GHOST = {prev: [245, 199, 100], next: [108, 182, 255]};
  const PREVIEW = 132, FALLOFF = 0.3;
  /* Guide colours: eye row in coral, ground row in mint, the pivot in white over ink. */
  const GUIDE = {eye: '#fa8c99', ground: '#85e4b6', pivot: '#ffffff'};
  /* Onion modes: 'diff' ghosts only where a neighbour differs; 'full' ghosts its whole silhouette. */
  const ONION_MODES = [['diff', 'Changes', 'Ghost only the pixels that differ from this frame'], ['full', 'Silhouette', 'Ghost every pixel of the neighbouring frames, to judge arcs and volume']];
  Object.assign(state, {onionSkin: store.get('onion-skin', true), onionAlpha: store.get('onion-alpha', 0.5), onionRange: store.get('onion-range', 1),
    onionMode: store.get('onion-mode', 'diff') === 'full' ? 'full' : 'diff'});
  const fb = {playing: false, start: 0, raf: 0, shown: -1};
  const esc = cr.esc;
  const shell = () => window.JelliShell;
  const announce = text => shell()?.announce?.(text);

  document.head.insertAdjacentHTML('beforeend', `<style>
    .fb{background:var(--panel);border:2px solid var(--line);border-radius:10px;padding:10px 12px;display:grid;gap:8px}
    .fb h3{margin:0;font-size:13px}.fb-head{display:flex;justify-content:space-between;align-items:baseline;gap:8px;flex-wrap:wrap}
    .fb-body{display:flex;gap:12px;align-items:flex-start;flex-wrap:wrap}
    .fb-preview{border-radius:8px;background:#000;image-rendering:pixelated;flex:none}
    .fb-controls{display:grid;gap:6px;align-content:start;min-width:0}
    .fb-now{font:12px ui-monospace,monospace;color:var(--muted)}
    .fb-strip{display:flex;flex-wrap:wrap;gap:6px;margin:0;padding:0;list-style:none}
    .fb-strip button{display:grid;justify-items:center;gap:2px;padding:4px;border:2px solid var(--line);border-radius:8px;background:var(--raised);font:10px ui-monospace,monospace;color:var(--muted);min-width:52px}
    .fb-strip button[aria-current=true]{border-color:var(--accent);color:var(--ink)}
    .fb-strip button.playing{box-shadow:0 0 0 2px var(--warn)}
    .fb-strip canvas{image-rendering:pixelated;background:#000;border-radius:4px}
    .fb-onion{display:flex;flex-wrap:wrap;gap:6px 12px;align-items:center;border-top:1px solid var(--line);padding-top:8px}
    .fb-legend{font-size:11px;color:var(--muted);display:flex;gap:10px;flex-wrap:wrap}.fb-legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px;vertical-align:-1px}
    .fb button kbd,.cr-keys kbd{font:10px ui-monospace,monospace;opacity:.8;margin-left:5px;padding:0 3px;border:1px solid currentColor;border-radius:3px;background:transparent}
    .fb .link{background:none;border:0;padding:0;color:var(--accent);text-decoration:underline;justify-self:start;font-size:12px}
    .fb input[type=range]{width:110px}
    .fb-align{font-size:11px;color:var(--muted)}.fb-align.warn{color:var(--warn);font-weight:600}
    .fb-strip button.off{border-color:var(--warn)}.fb-strip button.off span:first-of-type::before{content:'⚠ '}
  </style>`);

  /* ---------- which clip frame is on the canvas ---------- */
  const context = () => {
    const a = asset();
    return a?.kind === 'creatures' && state.mode === 'paint' && state.view === 'detail' ? A.paintContext(a.key) : null;
  };
  /* Frame indices `d` steps away, nearest first, honouring loop wrap; a one-shot stops at its ends. */
  function around(ctx, range) {
    const {c, index} = ctx, n = c.frames.length, out = {prev: [], next: []};
    for (let d = 1; d <= range; d++) {
      for (const [side, at] of [['prev', index - d], ['next', index + d]]) {
        const i = c.loop ? ((at % n) + n) % n : at;
        if (i < 0 || i >= n || i === index || out.prev.includes(i) || out.next.includes(i)) continue;
        out[side].push(i);
      }
    }
    return out;
  }
  function paintFrame(step) {
    const ctx = context(); if (!ctx || ctx.c.frames.length < 2) return false;
    const n = ctx.c.frames.length, i = (ctx.index + step + n) % n;
    A.setPaintClip({key: ctx.clip, index: i}); select(ctx.c.frames[i]);
    announce(`Painting frame ${i + 1} of ${n} in ${ctx.clip}: ${byKey[ctx.c.frames[i]]?.pose || ctx.c.frames[i]}`);
    return true;
  }
  S.stepFrame = dx => paintFrame(dx < 0 ? -1 : 1);

  /* ---------- onion skin on the paint canvas ---------- */
  S.onion = (g, a, z) => {
    if (!state.onionSkin) return;
    const ctx = context(); if (!ctx || ctx.c.frames[ctx.index] !== a.key) return;
    const near = around(ctx, state.onionRange), here = decoded[a.key]?.after;
    const ghosts = [...near.prev.map((i, d) => ['prev', i, d]), ...near.next.map((i, d) => ['next', i, d])].sort((x, y) => y[2] - x[2]);
    for (const [side, i, d] of ghosts) {  // farthest first, so the nearest frames sit on top
      const k = ctx.c.frames[i], p = decoded[k]?.after, other = byKey[k]; if (!p || k === a.key) continue;
      const dx = (a.pivot?.[0] ?? 0) - (other.pivot?.[0] ?? 0), dy = (a.pivot?.[1] ?? 0) - (other.pivot?.[1] ?? 0);
      const [r, gr, b] = GHOST[side], alpha = state.onionAlpha * Math.max(0.25, 1 - d * FALLOFF);
      g.fillStyle = `rgba(${r},${gr},${b},${alpha})`; g.strokeStyle = `rgba(${r},${gr},${b},${Math.min(1, alpha + 0.35)})`; g.lineWidth = 1;
      /* Ghost only where the neighbour differs from this frame, so unchanged pixels stay readable;
       * a bright inner edge keeps each ghost pixel visible on dark backdrops. */
      for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) {
        const o = (y * p.w + x) * 4; if (!p.data[o + 3]) continue;
        const seen = pixel(here, x + dx, y + dy);
        if (state.onionMode === 'diff' && seen && seen === hexOf(p.data, o)) continue;
        /* Silhouette mode ghosts every pixel; over this frame's own pixels it stays faint so they read. */
        const over = state.onionMode === 'full' && seen && seen === hexOf(p.data, o);
        if (over) g.globalAlpha = 0.35;
        g.fillRect((x + dx) * z, (y + dy) * z, z, z);
        if (z >= 6 && !over) g.strokeRect((x + dx) * z + 1.5, (y + dy) * z + 1.5, z - 3, z - 3);
        if (over) g.globalAlpha = 1;
      }
    }
  };
  function toggleOnion() {
    state.onionSkin = !state.onionSkin; store.set('onion-skin', state.onionSkin);
    renderStage(); document.getElementById('fb-onion')?.focus();
    announce(`Onion skin ${state.onionSkin ? 'on' : 'off'}`);
  }

  function setOnionMode(mode) {
    state.onionMode = mode; store.set('onion-mode', mode);
    renderStage(); document.querySelector(`[data-onion-mode="${mode}"]`)?.focus();
    announce(`Onion skin shows ${mode === 'full' ? 'whole silhouettes' : 'changes only'}`);
  }

  /* ---------- eye row, ground row and pivot ---------- */
  /* Each frame's measured rows, in the coordinates of the clip's first frame (frames align on their pivots).
   * The first frame sets the clip's rows; the house style keeps both still across a pose's frames. */
  function alignment(c) {
    const first = byKey[c.frames[0]], [fx, fy] = first?.pivot || [0, 0];
    const rows = c.frames.map(k => {
      const a = byKey[k], p = decoded[k]?.after, b = p && T.bounds(p), [, py] = a?.pivot || [fx, fy];
      if (!b) return null;
      const eye = T.eyeRow(p);
      return {ground: b.y + b.h - 1 - py + fy, eye: eye === null ? null : eye - py + fy};
    });
    const ref = rows[0] || {};
    return {ref, rows, off: rows.map((r, i) => (i && r ? [r.ground !== ref.ground && 'ground', r.eye !== null && ref.eye !== null && r.eye !== ref.eye && 'eye'].filter(Boolean) : []))};
  }
  /* Guide lines over a sprite drawn at scale z with its top-left at (ox, oy); dy maps first-frame rows to this frame. */
  function drawRowGuides(g, a, z, ref, dy, ox = 0, oy = 0) {
    const w = a.width * z, line = (row, colour) => { if (row === null || row === undefined) return; g.fillStyle = colour; g.fillRect(ox, oy + (row + dy) * z + z / 2 - 1, w, 2); };
    g.save(); g.setLineDash([]); g.globalAlpha = 0.85;
    line(ref.eye, GUIDE.eye); line(ref.ground, GUIDE.ground);
    const [px, py] = a.pivot || [a.width >> 1, a.height], cx = ox + px * z, cy = oy + py * z, r = Math.max(3, z);
    g.globalAlpha = 1; g.fillStyle = '#291b35'; g.fillRect(cx - r - 1, cy - 2, 2 * r + 2, 4); g.fillRect(cx - 2, cy - r - 1, 4, 2 * r + 2);
    g.fillStyle = GUIDE.pivot; g.fillRect(cx - r, cy - 1, 2 * r, 2); g.fillRect(cx - 1, cy - r, 2, 2 * r);
    if (z >= 8) {
      g.font = '600 10px ui-monospace,monospace'; g.textBaseline = 'bottom';
      for (const [row, colour, text] of [[ref.eye, GUIDE.eye, 'eye'], [ref.ground, GUIDE.ground, 'ground']]) if (row !== null && row !== undefined) { g.fillStyle = colour; g.fillText(text, ox + 3, oy + (row + dy) * z + z / 2 - 2); }
    }
    g.restore();
  }
  /* The paint canvas, after the general guides: this clip's rows, shifted onto this frame's pivot. */
  S.overlay = (g, a, z) => {
    if (!state.guides) return;
    const ctx = context(); if (!ctx || ctx.c.frames[ctx.index] !== a.key) return;
    const {ref} = alignment(ctx.c), first = byKey[ctx.c.frames[0]];
    drawRowGuides(g, a, z, ref, (a.pivot?.[1] ?? 0) - (first?.pivot?.[1] ?? 0));
  };
  function alignmentNote(c, al) {
    const bad = al.off.map((o, i) => o.length ? `${i + 1} (${o.join(' and ')})` : '').filter(Boolean);
    const rows = [al.ref.eye !== null && al.ref.eye !== undefined ? `eye row ${al.ref.eye}` : 'no eye row found', al.ref.ground !== undefined ? `ground row ${al.ref.ground}` : ''].filter(Boolean).join(' · ');
    return bad.length ? `<span class="fb-align warn" role="note">⚠ Frame${bad.length > 1 ? 's' : ''} ${bad.join(', ')} ${bad.length > 1 ? 'move' : 'moves'} off the first frame's ${rows}</span>`
      : `<span class="fb-align" role="note">${c.frames.length > 1 ? '✓ Frames keep' : 'First frame sets'} ${rows}</span>`;
  }

  /* ---------- the preview: every frame aligned on its pivot ---------- */
  function frameBox(c) {
    let l = 0, t = 0, r = 1, b = 1;
    for (const k of c.frames) {
      const a = byKey[k], [px, py] = a?.pivot || [0, 0]; if (!a) continue;
      l = Math.min(l, -px); t = Math.min(t, -py); r = Math.max(r, a.width - px); b = Math.max(b, a.height - py);
    }
    const w = r - l, h = b - t;
    return {l, t, w, h, s: Math.max(1, Math.floor(Math.min(PREVIEW / w, PREVIEW / h)))};
  }
  function drawFrame(canvas, c, i, box) {
    const k = c.frames[i], a = byKey[k], g = canvas.getContext('2d'), [px, py] = a?.pivot || [0, 0];
    g.setTransform(DPR, 0, 0, DPR, 0, 0); g.imageSmoothingEnabled = false;
    fillBackdrop(g, box.w, box.h, box.s, backdropFor(a || asset()));
    g.setTransform(DPR, 0, 0, DPR, (-px - box.l) * box.s * DPR, (-py - box.t) * box.s * DPR);
    drawPixels(g, decoded[k]?.after, box.s);
    g.setTransform(DPR, 0, 0, DPR, 0, 0);
    if (state.guides && a) {
      const first = byKey[c.frames[0]], dy = py - (first?.pivot?.[1] ?? py);
      drawRowGuides(g, a, box.s, alignment(c).ref, dy, (-px - box.l) * box.s, (-py - box.t) * box.s);
    }
  }
  function showFrame(i) {
    const ctx = context(), canvas = document.getElementById('fb-preview'); if (!ctx || !canvas) return;
    drawFrame(canvas, ctx.c, i, frameBox(ctx.c)); fb.shown = i;
    document.querySelectorAll('#fb-strip button').forEach((b, j) => b.classList.toggle('playing', fb.playing && j === i));
    const now = document.getElementById('fb-now');
    if (now) now.textContent = `${fb.playing ? 'playing' : 'showing'} frame ${i + 1}/${ctx.c.frames.length} · ${ctx.c.durations_ms[i]} ms`;
  }
  function tick(t) {
    fb.raf = 0;
    const ctx = context(); if (!fb.playing || !ctx || !document.getElementById('fb-preview')) { fb.playing = false; return; }
    const total = cr.totalOf(ctx.c), held = 700;  // a one-shot holds its last frame briefly, then replays
    let elapsed = t - fb.start;
    if (!ctx.c.loop && elapsed > total + held) { fb.start = t; elapsed = 0; }
    const i = A.clipFrame({...ctx.c, durations_ms: cr.safeDurations(ctx.c)}, elapsed);
    if (i !== fb.shown) showFrame(i);
    fb.raf = requestAnimationFrame(tick);
  }
  function setPlaying(on) {
    const ctx = context(); if (!ctx) return;
    fb.playing = on && ctx.c.frames.length > 0;
    if (fb.raf) { cancelAnimationFrame(fb.raf); fb.raf = 0; }
    if (fb.playing) { fb.start = performance.now(); fb.shown = -1; fb.raf = requestAnimationFrame(tick); } else showFrame(ctx.index);
    const b = document.getElementById('fb-play');
    if (b) { b.setAttribute('aria-pressed', String(fb.playing)); b.firstChild.textContent = fb.playing ? 'Pause' : 'Play'; }
    announce(fb.playing ? `Playing ${ctx.clip}` : 'Paused');
  }
  /* After every canvas redraw: the paused preview and this frame's thumbnail follow the strokes. */
  S.afterDraw = () => {
    const ctx = context(); if (!ctx) return;
    document.getElementById('fb-guides')?.setAttribute('aria-pressed', String(!!state.guides));
    const note = document.querySelector('.fb .fb-align');  // strokes can move a row, so measure again
    if (note) { const al = alignment(ctx.c); note.outerHTML = alignmentNote(ctx.c, al); document.querySelectorAll('#fb-strip button').forEach((b, i) => b.classList.toggle('off', !!al.off[i]?.length)); }
    if (!fb.playing && document.getElementById('fb-preview')) showFrame(ctx.index);
    const thumb = document.querySelector(`#fb-strip [data-i="${ctx.index}"] canvas`);
    if (thumb) drawThumb(thumb, ctx.c.frames[ctx.index]);
  };
  function drawThumb(canvas, k) {
    const a = byKey[k], p = decoded[k]?.after; if (!a || !p) return;
    const s = Math.max(1, Math.floor(40 / Math.max(a.width, a.height))), g = canvas.getContext('2d');
    g.setTransform(DPR, 0, 0, DPR, 0, 0); g.imageSmoothingEnabled = false; g.clearRect(0, 0, a.width * s, a.height * s); drawPixels(g, p, s);
  }

  /* ---------- the panel in the Paint side column ---------- */
  const kbd = k => `<kbd>${k}</kbd>`;
  S.renderSide = (side, a) => {
    if (a.kind !== 'creatures') return;
    const ctx = A.paintContext(a.key), box = document.createElement('section');
    box.className = 'fb'; box.setAttribute('aria-labelledby', 'fb-title');
    if (!ctx) {
      box.innerHTML = `<h3 id="fb-title">Flip-book</h3><p class="studio-note" style="margin:0">This frame is not in any clip yet. Add it to a pose with <b>+ clip</b> in the Creature tab's frame library.</p>`;
      side.append(box); return;
    }
    const {c, index, clip} = ctx, n = c.frames.length, many = n > 1, near = around(ctx, state.onionRange), al = alignment(c);
    const names = list => list.map(i => esc(byKey[c.frames[i]]?.pose || c.frames[i])).join(', ');
    box.innerHTML = `<div class="fb-head"><h3 id="fb-title">Flip-book</h3><span class="lbl">${esc(clip)} · ${n} frame${n === 1 ? '' : 's'} · ${c.loop ? 'loops' : 'plays once'}</span></div>
      <div class="fb-head">${alignmentNote(c, al)}<button id="fb-guides" aria-pressed="${!!state.guides}" title="Draw the eye row, ground row and pivot on the canvas and the preview" aria-keyshortcuts="Shift+G">Guides${kbd('⇧G')}</button></div>
      <div class="fb-body"><canvas id="fb-preview" class="fb-preview" role="img" aria-label="${esc(clip)} preview, with unsaved paint"></canvas>
        <div class="fb-controls"><div class="seg" role="group" aria-label="Flip-book">
          <button id="fb-prev" title="Paint the previous frame" aria-keyshortcuts="ArrowLeft ," ${many ? '' : 'disabled'}>◀ Frame${kbd('←')}</button>
          <button id="fb-play" aria-pressed="${fb.playing}" title="Play the clip in the preview while you paint" aria-keyshortcuts="Shift+Space" ${n ? '' : 'disabled'}>${fb.playing ? 'Pause' : 'Play'}${kbd('⇧Space')}</button>
          <button id="fb-next" title="Paint the next frame" aria-keyshortcuts="ArrowRight ." ${many ? '' : 'disabled'}>Frame ▶${kbd('→')}</button></div>
          <span class="fb-now" id="fb-now" aria-live="off"></span>
          <span class="studio-note" style="margin:0">Painting frame <b>${index + 1}</b> of ${n}. ← and → change frame while the keyboard cursor is hidden; , and . always do.</span></div></div>
      <ol class="fb-strip" id="fb-strip" aria-label="Frames of ${esc(clip)}">${c.frames.map((k, i) => `<li><button data-i="${i}" aria-current="${i === index}"${al.off[i].length ? ' class="off"' : ''} aria-label="Paint frame ${i + 1}: ${esc(byKey[k]?.pose || k)}, ${c.durations_ms[i]} ms${al.off[i].length ? `, ${al.off[i].join(' and ')} row moved` : ''}"><canvas></canvas><span>${i + 1} · ${esc(byKey[k]?.pose || k)}</span><span>${c.durations_ms[i]} ms</span></button></li>`).join('')}</ol>
      <div class="fb-onion"><button id="fb-onion" aria-pressed="${state.onionSkin}" title="Ghost the neighbouring frames where they differ from this one" aria-keyshortcuts="O" ${many ? '' : 'disabled'}>Onion skin${kbd('O')}</button>
        <div class="seg" role="group" aria-label="Onion skin shows">${ONION_MODES.map(([m, label, what]) => `<button data-onion-mode="${m}" aria-pressed="${state.onionMode === m}" title="${what} (Shift+O switches)" ${many && state.onionSkin ? '' : 'disabled'}>${label}</button>`).join('')}</div>
        <div class="seg" role="group" aria-label="Frames each side">${[1, 2, 3].map(r => `<button data-range="${r}" aria-pressed="${state.onionRange === r}" title="Ghost ${r} frame${r > 1 ? 's' : ''} each side" ${many && state.onionSkin ? '' : 'disabled'}>±${r}</button>`).join('')}</div>
        <label class="lbl">Ghost <input type="range" id="fb-alpha" min="0.15" max="0.9" step="0.05" value="${state.onionAlpha}" aria-label="Onion skin opacity" ${many && state.onionSkin ? '' : 'disabled'}></label>
        ${state.onionSkin && many ? `<span class="fb-legend">${near.prev.length ? `<span><i style="background:rgb(${GHOST.prev})"></i>before: ${names(near.prev)}</span>` : ''}${near.next.length ? `<span><i style="background:rgb(${GHOST.next})"></i>after: ${names(near.next)}</span>` : ''}</span>` : ''}</div>
      <button class="link" id="fb-timeline" title="Switch to the Creature tab with this clip's timeline">Edit order and timing in the Creature tab ↗</button>`;
    side.append(box);
    const b = frameBox(c), preview = box.querySelector('#fb-preview'), [sized] = makeCanvas(b.w * b.s, b.h * b.s);
    preview.width = sized.width; preview.height = sized.height; preview.style.width = sized.style.width; preview.style.height = sized.style.height;
    box.querySelectorAll('#fb-strip button').forEach(btn => {
      const k = c.frames[Number(btn.dataset.i)], a2 = byKey[k], cv = btn.querySelector('canvas');
      if (a2) { const s = Math.max(1, Math.floor(40 / Math.max(a2.width, a2.height))), [m] = makeCanvas(a2.width * s, a2.height * s); cv.width = m.width; cv.height = m.height; cv.style.width = m.style.width; cv.style.height = m.style.height; drawThumb(cv, k); }
      btn.onclick = () => { const i = Number(btn.dataset.i); if (i !== index) { A.setPaintClip({key: clip, index: i}); select(k); } };
    });
    box.querySelector('#fb-prev').onclick = () => paintFrame(-1);
    box.querySelector('#fb-next').onclick = () => paintFrame(1);
    box.querySelector('#fb-play').onclick = () => setPlaying(!fb.playing);
    box.querySelector('#fb-onion').onclick = toggleOnion;
    box.querySelector('#fb-guides').onclick = () => { S.toggleGuides?.(); document.getElementById('fb-guides')?.focus(); };
    box.querySelectorAll('[data-onion-mode]').forEach(b => { b.onclick = () => setOnionMode(b.dataset.onionMode); });
    box.querySelectorAll('[data-range]').forEach(r => { r.onclick = () => { state.onionRange = Number(r.dataset.range); store.set('onion-range', state.onionRange); renderStage(); document.querySelector(`[data-range="${state.onionRange}"]`)?.focus(); }; });
    box.querySelector('#fb-alpha').oninput = e => { state.onionAlpha = Number(e.target.value); store.set('onion-alpha', state.onionAlpha); S.redraw?.(); };
    box.querySelector('#fb-timeline').onclick = () => A.backToTimeline();
    if (fb.playing && !fb.raf) fb.raf = requestAnimationFrame(tick); else if (!fb.playing) showFrame(index);
  };

  /* ---------- keys ---------- */
  const baseKeydown = S.keydown;
  S.keydown = e => {
    const ctx = context(), mod = e.metaKey || e.ctrlKey || e.altKey;
    if (ctx && !mod) {
      const k = e.key.toLowerCase(), widget = e.target.closest?.('[role=tablist], [role=slider], [role=listbox], select, input, textarea');
      if (k === ' ' && e.shiftKey) { e.preventDefault(); if (!e.repeat) setPlaying(!fb.playing); return true; }
      if (k === 'o' && !e.shiftKey) { toggleOnion(); return true; }
      if (k === 'o' && e.shiftKey && state.onionSkin) { setOnionMode(state.onionMode === 'full' ? 'diff' : 'full'); return true; }
      if (k === ',' || k === '.') { e.preventDefault(); paintFrame(k === ',' ? -1 : 1); return true; }
      if (!widget && !e.shiftKey && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) { e.preventDefault(); paintFrame(e.key === 'ArrowLeft' ? -1 : 1); return true; }
    }
    return baseKeydown(e);
  };
  shell()?.registerShortcuts?.('Paint · clip frame', [
    {keys: ['ArrowLeft', 'ArrowRight'], description: 'Paint the previous or next frame (while the keyboard cursor is hidden; Esc hides it)'},
    {keys: [',', '.'], description: 'Paint the previous or next frame, at any time'},
    {keys: ['Shift+Space'], description: 'Play or pause the flip-book preview'},
    {keys: ['O'], description: 'Onion skin on or off'}, {keys: ['Shift+O'], description: 'Onion skin: changes only or whole silhouettes'},
    {keys: ['Shift+G'], description: 'Guides: also the clip\'s eye row, ground row and pivot'}]);
})();

/* Jelli Art: the flip-book under the Paint canvas when the sprite is a frame of a creature clip.
 * Loaded after animation.js. Docked under the Paint canvas (UX M2-B). A live preview plays the clip with its real timing from the working
 * pixels, so unsaved strokes animate; a strip of frame thumbnails jumps between frames; onion skin
 * ghosts up to three frames either side. Keys: Left/Right (while the keyboard cursor is hidden)
 * or , and . change frame, Shift+Space plays or pauses, O toggles onion skin and Shift+O switches
 * it between changes only and whole silhouettes. Frames are checked the way core/pet_actor.c places
 * them: each by its own ground anchor (cr.groundAnchor). A frame whose anchor or eye height differs
 * from the first frame's is flagged, and the flip-book's Guides draw both on the canvas and preview. */
'use strict';
(() => {
  const S = window.Studio, A = window.JelliAnimation, cr = S?.cr, T = window.JelliPaint;
  if (!S || !cr || !A?.paintContext) return;
  /* Amber before, sky after: apart in hue and lightness, and for red-green colour blindness. */
  const GHOST = {prev: [245, 199, 100], next: [108, 182, 255]};
  const PREVIEW = 132, FALLOFF = 0.3;
  /* Guide colours: the expected eye row in coral, the ground line in mint, ground anchors in white. */
  const GUIDE = {eye: '#fa8c99', ground: '#85e4b6', anchor: '#ffffff'};
  /* Onion modes: 'diff' ghosts only where a neighbour differs; 'full' ghosts its whole silhouette. */
  const ONION_MODES = [['diff', 'Changes', 'Ghost only the pixels that differ from this frame'], ['full', 'Silhouette', 'Ghost every pixel of the neighbouring frames, to judge arcs and volume']];
  Object.assign(state, {onionSkin: store.get('onion-skin', true), onionAlpha: store.get('onion-alpha', 0.5), onionRange: store.get('onion-range', 1),
    onionMode: store.get('onion-mode', 'diff') === 'full' ? 'full' : 'diff', animGuides: store.get('anim-guides', false)});
  const fb = {open: false, playing: false, start: 0, raf: 0, shown: -1, measured: new Map(), drawn: null};
  const esc = cr.esc;
  const attrOf = t => String(t).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');  // an attribute value that serialises back unchanged, so afterDraw can compare
  const shell = () => window.JelliShell;
  const announce = text => shell()?.announce?.(text);

  document.head.insertAdjacentHTML('beforeend', `<style>
    /* The dock under the Paint canvas (D1): one row, at most about 140 px tall, so the canvas, the dock and the palette are all in view. */
    .fb{position:relative;background:var(--panel);border:2px solid var(--line);border-radius:10px;padding:8px 12px;display:grid;grid-template-columns:auto fit-content(290px) minmax(0,1fr) auto;gap:6px 14px;align-items:center}
    .fb.fb-empty{display:block}
    .fb h3{margin:0;font-size:13px}.fb-head{display:flex;justify-content:space-between;align-items:baseline;gap:4px 10px;flex-wrap:wrap}
    .fb-preview{border-radius:8px;background:#000;image-rendering:pixelated;flex:none;max-width:96px;max-height:96px}
    .fb-controls{display:grid;gap:4px;align-content:center;min-width:0}
    .fb-now{font:12px ui-monospace,monospace;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
    .fb-strip{display:flex;gap:6px;margin:0;padding:2px 2px 6px;list-style:none;overflow-x:auto;min-width:0;position:relative;scrollbar-width:thin}
    .fb-strip li{flex:none}
    .fb-strip button{display:grid;justify-items:center;gap:2px;padding:4px;border:2px solid var(--line);border-radius:8px;background:var(--raised);font:10px ui-monospace,monospace;color:var(--muted);min-width:52px}
    .fb-strip button[aria-current=true]{border-color:var(--accent);color:var(--ink)}
    .fb-strip button.playing{box-shadow:0 0 0 2px var(--warn)}
    .fb-strip canvas{image-rendering:pixelated;background:#000;border-radius:4px}
    .fb-more>summary{cursor:pointer;font-weight:600;font-size:12px;padding:6px 4px;white-space:nowrap}
    .fb-panel{position:absolute;right:8px;bottom:calc(100% + 6px);z-index:20;width:min(460px,calc(100% - 16px));display:grid;gap:10px;background:var(--panel);border:2px solid var(--line);border-radius:10px;padding:10px 12px;box-shadow:0 6px 24px var(--shadow)}
    .fb-onion{display:flex;flex-wrap:wrap;gap:6px 12px;align-items:center}
    .fb-legend{font-size:11px;color:var(--muted);display:flex;gap:10px;flex-wrap:wrap}.fb-legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px;vertical-align:-1px}
    .fb button kbd,.cr-keys kbd{font:10px ui-monospace,monospace;opacity:.8;margin-left:5px;padding:0 3px;border:1px solid currentColor;border-radius:3px;background:transparent}
    .fb .link{background:none;border:0;padding:0;color:var(--accent);text-decoration:underline;justify-self:start;font-size:12px}
    .fb input[type=range]{width:110px}
    .fb-align{font-size:11px;color:var(--muted);display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;min-width:0}.fb-align.warn{color:var(--warn);font-weight:600}
    @media (any-pointer: coarse){
      .fb,.fb-strip,.fb-onion,.fb .seg{gap:var(--tap-gap)}
      .fb-strip button{min-width:var(--tap);min-height:var(--tap)}.fb .link,.fb-more>summary{min-height:var(--tap);display:inline-flex;align-items:center}
    }
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
    g.save();
    for (const [side, i, d] of ghosts) {  // farthest first, so the nearest frames sit on top
      const k = ctx.c.frames[i], p = decoded[k]?.after, other = byKey[k]; if (!p || k === a.key) continue;
      const dx = (a.pivot?.[0] ?? 0) - (other.pivot?.[0] ?? 0), dy = (a.pivot?.[1] ?? 0) - (other.pivot?.[1] ?? 0);
      const [r, gr, b] = GHOST[side], alpha = state.onionAlpha * Math.max(0.25, 1 - d * FALLOFF);
      g.fillStyle = `rgba(${r},${gr},${b},${alpha})`; g.strokeStyle = `rgba(${r},${gr},${b},${Math.min(1, alpha + 0.35)})`; g.lineWidth = 1;
      /* Ghost only where the neighbour differs from this frame, so unchanged pixels stay readable;
       * a bright inner edge keeps each ghost pixel visible on dark backdrops. */
      for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) {
        const o = (y * p.w + x) * 4; if (!p.data[o + 3]) continue;
        const seen = pixel(here, x + dx, y + dy), same = seen && seen === hexOf(p.data, o);
        if (same && state.onionMode === 'diff') continue;
        /* Silhouette mode ghosts the whole shape. Over this frame's matching pixels only the ghost's
         * outline shows, faintly, so even ±3 stays readable. */
        if (same && [[1, 0], [-1, 0], [0, 1], [0, -1]].every(([ex, ey]) => pixel(p, x + ex, y + ey))) continue;
        g.globalAlpha = same ? 0.35 : 1;
        g.fillRect((x + dx) * z, (y + dy) * z, z, z);
        if (z >= 6 && !same) g.strokeRect((x + dx) * z + 1.5, (y + dy) * z + 1.5, z - 3, z - 3);
      }
    }
    g.restore();
  };
  function toggleOnion() {
    state.onionSkin = !state.onionSkin; store.set('onion-skin', state.onionSkin);
    renderStage(); document.getElementById('fb-onion')?.focus();
    announce(`Onion skin ${state.onionSkin ? 'on' : 'off'}`);
  }

  function setOnionMode(mode) {
    const inPanel = !!document.activeElement?.closest?.('.fb');  // a key press keeps the canvas focused
    state.onionMode = mode; store.set('onion-mode', mode);
    renderStage(); if (inPanel) document.querySelector(`[data-onion-mode="${mode}"]`)?.focus();
    announce(`Onion skin shows ${mode === 'full' ? 'whole silhouettes' : 'changes only'}`);
  }
  function toggleAnimGuides() {
    state.animGuides = !state.animGuides; store.set('anim-guides', state.animGuides);
    S.redraw?.(); announce(`Eye and ground guides ${state.animGuides ? 'on' : 'off'}`);
  }

  /* ---------- eye height and ground anchor, as the game places each frame ---------- */
  /* One frame, from its working pixels: the bottom row edge and anchor x (both from cr.groundAnchor,
   * q8 like the engine) and the eye's height above that bottom edge. Null for an empty frame. */
  function measure(k) {
    const p = decoded[k]?.after, anchor = p && cr.groundAnchor(p); if (!anchor) return null;
    const bottom = anchor[1] / 256, eye = T.eyeRow(p);
    return {k, ax: anchor[0], bottom, eyeAbove: eye === null ? null : bottom - eye};
  }
  /* Every frame against the first. Only `fresh` (the frame being painted) is measured again; the others
   * keep their measurement until the flip-book is rebuilt. */
  function alignment(c, fresh = null) {
    const ms = c.frames.map(k => {
      if (k === fresh || !fb.measured.has(k)) fb.measured.set(k, measure(k));
      return fb.measured.get(k);
    });
    const ref = ms[0], scale = cr.scaleFor?.(byKey[c.frames[0]]?.form) || null;  // the form's actor scale (content/creatures.json)
    const screenX = m => Math.round(m.ax * scale / 256);  // where core/pet_actor.c puts the frame, in panel pixels
    const off = ms.map((m, i) => {
      if (!i || !m || !ref) return [];
      const out = [], slide = scale ? screenX(ref) - screenX(m) : 0;
      // Under half a source pixel the slide is too small to see, so only larger ones count.
      if (scale && Math.abs(slide) * 2 >= scale) out.push(`slides ${Math.abs(slide)} px ${slide > 0 ? 'right' : 'left'}`);
      if (m.eyeAbove !== null && ref.eyeAbove !== null && m.eyeAbove !== ref.eyeAbove) {
        const d = m.eyeAbove - ref.eyeAbove;
        out.push(`eye ${Math.abs(d)} px ${d > 0 ? 'higher' : 'lower'}`);
      }
      return out;
    });
    return {ref, ms, off, scale};
  }
  /* Guides over frame m drawn at scale z with its top-left at (ox, oy): the ground line on its bottom edge,
   * the eye row where the first frame's eye height puts it, and the ground anchors (this frame's, and the
   * first frame's when they differ) as ticks on the ground line. */
  function drawGuides(g, a, z, m, ref, ox = 0, oy = 0) {
    if (!m || !ref) return;
    const w = a.width * z, floor = oy + m.bottom * z;
    g.save(); g.setLineDash([]); g.globalAlpha = 0.85;
    g.fillStyle = GUIDE.ground; g.fillRect(ox, floor - 1, w, 2);
    const eye = ref.eyeAbove === null ? null : m.bottom - ref.eyeAbove;
    if (eye !== null) { g.fillStyle = GUIDE.eye; g.fillRect(ox, oy + (eye + 0.5) * z - 1, w, 2); }
    g.globalAlpha = 1;
    const tick = (ax, colour) => { const x = ox + ax / 256 * z, r = Math.max(3, z); g.fillStyle = '#291b35'; g.fillRect(x - 2, floor - r - 1, 4, r + 2); g.fillStyle = colour; g.fillRect(x - 1, floor - r, 2, r); };
    if (ref.ax !== m.ax) tick(ref.ax, GUIDE.ground);
    tick(m.ax, GUIDE.anchor);
    if (z >= 8) {
      g.font = '600 10px ui-monospace,monospace'; g.textBaseline = 'bottom';
      g.lineWidth = 3; g.lineJoin = 'round'; g.strokeStyle = '#291b35';  // an ink outline keeps labels readable on light backdrops
      const label = (text, colour, y) => { g.strokeText(text, ox + 3, y); g.fillStyle = colour; g.fillText(text, ox + 3, y); };
      label('ground', GUIDE.ground, floor - 3);
      if (eye !== null) label('eye', GUIDE.eye, oy + (eye + 0.5) * z - 3);
    }
    g.restore();
  }
  /* The paint canvas: measure this frame again (strokes move it) and draw its guides. */
  S.overlay = (g, a, z) => {
    const ctx = context(); if (!ctx || ctx.c.frames[ctx.index] !== a.key) return;
    fb.drawn = alignment(ctx.c, a.key);
    if (state.animGuides) drawGuides(g, a, z, fb.drawn.ms[ctx.index], fb.drawn.ref);
  };
  function alignmentNote(c, al) {
    if (!al.ref) return '<span class="fb-align warn" role="note" title="First frame is empty, so nothing is compared">First frame is empty, so nothing is compared</span>';
    const eye = (al.ref.eyeAbove === null ? 'no eye found' : `eye ${al.ref.eyeAbove} px above the ground`) + (al.scale ? '' : ' · scale unknown, slides not checked');
    const bad = al.off.map((o, i) => (o.length ? `frame ${i + 1} ${o.join(', ')}` : '')).filter(Boolean);
    if (bad.length) { const t = `⚠ Against frame 1 (${eye}): ${bad.join('; ')}`; return `<span class="fb-align warn" role="note" title="${attrOf(t)}">${t}</span>`; }
    const t = `${c.frames.length > 1 ? '✓ Frames keep frame 1\'s ground anchor and' : 'Frame 1 sets the ground anchor and'} ${eye}`;
    return `<span class="fb-align" role="note" title="${attrOf(t)}">${t}</span>`;
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
    if (state.animGuides && a) { const al = alignment(c); drawGuides(g, a, box.s, al.ms[i], al.ref, (-px - box.l) * box.s, (-py - box.t) * box.s); }
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
    document.getElementById('fb-guides')?.setAttribute('aria-pressed', String(!!state.animGuides));
    const al = fb.drawn || alignment(ctx.c, ctx.c.frames[ctx.index]), note = document.querySelector('.fb .fb-align'), html = alignmentNote(ctx.c, al);
    fb.drawn = null;
    if (note && note.outerHTML !== html) note.outerHTML = html;  // strokes can move the anchor or the eye
    document.querySelectorAll('#fb-strip button').forEach((b, i) => b.classList.toggle('off', !!al.off[i]?.length));
    if (!fb.playing && document.getElementById('fb-preview')) showFrame(ctx.index);
    const thumb = document.querySelector(`#fb-strip [data-i="${ctx.index}"] canvas`);
    if (thumb) drawThumb(thumb, ctx.c.frames[ctx.index]);
  };
  function drawThumb(canvas, k) {
    const a = byKey[k], p = decoded[k]?.after; if (!a || !p) return;
    const s = Math.max(1, Math.floor(40 / Math.max(a.width, a.height))), g = canvas.getContext('2d');
    g.setTransform(DPR, 0, 0, DPR, 0, 0); g.imageSmoothingEnabled = false; g.clearRect(0, 0, a.width * s, a.height * s); drawPixels(g, p, s);
  }

  const kbd = k => `<kbd>${k}</kbd>`;
  /* ---------- the dock under the Paint canvas ---------- */
  S.renderDock = (dock, a) => {
    dock.textContent = '';
    if (a.kind !== 'creatures') return false;
    const ctx = A.paintContext(a.key), box = document.createElement('section');
    box.className = 'fb'; box.setAttribute('aria-labelledby', 'fb-title');
    dock.append(box);
    if (!ctx) {
      box.className = 'fb fb-empty';
      box.innerHTML = `<h3 id="fb-title">Flip-book</h3><p class="studio-note" style="margin:0">This frame is not in any clip yet. Add it to a pose with <b>+ clip</b> in the Creature tab's frame library.</p>`;
      return true;
    }
    fb.measured.clear();
    const {c, index, clip} = ctx, n = c.frames.length, many = n > 1, near = around(ctx, state.onionRange), al = alignment(c);
    const names = list => list.map(i => esc(byKey[c.frames[i]]?.pose || c.frames[i])).join(', ');
    /* One row: preview, the transport, the frames (scrolls sideways) and a "Onion & guides" disclosure whose panel floats over the canvas,
     * so opening it never changes the canvas box. */
    box.innerHTML = `<canvas id="fb-preview" class="fb-preview" role="img" aria-label="${esc(clip)} preview, with unsaved paint"></canvas>
      <div class="fb-controls"><div class="fb-head"><h3 id="fb-title">Flip-book</h3><span class="lbl">${esc(clip)} · ${n} frame${n === 1 ? '' : 's'} · ${c.loop ? 'loops' : 'plays once'}</span></div>
        <div class="seg" role="group" aria-label="Flip-book">
          <button id="fb-prev" title="Paint the previous frame (Left arrow or ,)" aria-keyshortcuts="ArrowLeft ," ${many ? '' : 'disabled'}>◀ Frame</button>
          <button id="fb-play" aria-pressed="${fb.playing}" title="Play the clip in the preview while you paint (Shift+Space)" aria-keyshortcuts="Shift+Space" ${n ? '' : 'disabled'}>${fb.playing ? 'Pause' : 'Play'}</button>
          <button id="fb-next" title="Paint the next frame (Right arrow or .)" aria-keyshortcuts="ArrowRight ." ${many ? '' : 'disabled'}>Frame ▶</button><button id="fb-duplicate" title="Copy the saved frame to a new PNG and insert it after this frame when the clip has room">${window.JelliIcons.svg("duplicate")}Duplicate frame</button></div>
        <span class="fb-now" id="fb-now" aria-live="off"></span>${alignmentNote(c, al)}</div>
      <ol class="fb-strip" id="fb-strip" aria-label="Frames of ${esc(clip)}">${c.frames.map((k, i) => `<li><button data-i="${i}" aria-current="${i === index}"${al.off[i].length ? ' class="off"' : ''} aria-label="${i + 1} · ${esc(byKey[k]?.pose || k)} ${c.durations_ms[i]} ms${al.off[i].length ? `, ${al.off[i].join(', ')}` : ''}" title="Paint frame ${i + 1}"><canvas></canvas><span>${i + 1} · ${esc(byKey[k]?.pose || k)}</span><span>${c.durations_ms[i]} ms</span></button></li>`).join('')}</ol>
      <details class="fb-more" id="fb-more"${fb.open ? ' open' : ''}><summary>Onion &amp; guides</summary><div class="fb-panel">
        <div class="fb-onion"><button id="fb-onion" aria-pressed="${state.onionSkin}" title="Ghost the neighbouring frames (Shift+O: changes only or whole silhouettes)" aria-keyshortcuts="O" ${many ? '' : 'disabled'}>Onion skin${kbd('O')}</button>
          <div class="seg" role="group" aria-label="Onion skin shows">${ONION_MODES.map(([m, label, what]) => `<button data-onion-mode="${m}" aria-pressed="${state.onionMode === m}" title="${what} (Shift+O switches)" ${many && state.onionSkin ? '' : 'disabled'}>${label}</button>`).join('')}</div>
          <div class="seg" role="group" aria-label="Frames each side">${[1, 2, 3].map(r => `<button data-range="${r}" aria-pressed="${state.onionRange === r}" title="Ghost ${r} frame${r > 1 ? 's' : ''} each side" ${many && state.onionSkin ? '' : 'disabled'}>±${r}</button>`).join('')}</div>
          <label class="lbl">Ghost <input type="range" id="fb-alpha" min="0.15" max="0.9" step="0.05" value="${state.onionAlpha}" aria-label="Onion skin opacity" ${many && state.onionSkin ? '' : 'disabled'}></label>
          ${state.onionSkin && many ? `<span class="fb-legend">${near.prev.length ? `<span><i style="background:rgb(${GHOST.prev})"></i>before: ${names(near.prev)}</span>` : ''}${near.next.length ? `<span><i style="background:rgb(${GHOST.next})"></i>after: ${names(near.next)}</span>` : ''}</span>` : ''}</div>
        <div class="fb-onion"><button id="fb-guides" aria-pressed="${!!state.animGuides}" title="Draw each frame's ground line and ground anchor, and where frame 1's eye height puts the eye, on the canvas and the preview">Eye &amp; ground</button>
          ${window.JelliTouch?.coarse() ? `${window.JelliTouch.info('Onion skin ghosts the neighbouring frames: Changes shows only the pixels that differ, Silhouette shows every pixel. Frames each side sets how many frames to ghost. Eye & ground draws each frame\'s ground line and anchor, and where frame 1\'s eye height puts the eye.', 'About onion skin and guides')}<span class="studio-note" style="margin:0">What the onion and guide buttons do</span>` : ''}</div>
        <span class="studio-note" style="margin:0">Painting frame <b>${index + 1}</b> of ${n}. ← and → change frame while the keyboard cursor is hidden; , and . always do.</span>
        <button class="link" id="fb-timeline" title="Switch to the Creature tab with this clip's timeline">Edit order and timing in the Creature tab ↗</button></div></details>`;
    const b = frameBox(c), preview = box.querySelector('#fb-preview'), [sized] = makeCanvas(b.w * b.s, b.h * b.s);
    preview.width = sized.width; preview.height = sized.height; preview.style.width = sized.style.width; preview.style.height = sized.style.height;
    const strip = box.querySelector('#fb-strip');
    box.querySelectorAll('#fb-strip button').forEach(btn => {
      const k = c.frames[Number(btn.dataset.i)], a2 = byKey[k], cv = btn.querySelector('canvas');
      if (a2) { const s = Math.max(1, Math.floor(40 / Math.max(a2.width, a2.height))), [m] = makeCanvas(a2.width * s, a2.height * s); cv.width = m.width; cv.height = m.height; cv.style.width = m.style.width; cv.style.height = m.style.height; drawThumb(cv, k); }
      btn.onclick = () => { const i = Number(btn.dataset.i); if (i !== index) { A.setPaintClip({key: clip, index: i}); select(k); } };
    });
    box.querySelector('#fb-prev').onclick = () => paintFrame(-1);
    box.querySelector('#fb-next').onclick = () => paintFrame(1);
    box.querySelector('#fb-play').onclick = () => setPlaying(!fb.playing);
    /** @type {HTMLButtonElement} */ (box.querySelector('#fb-duplicate')).onclick = () => A.duplicatePaintFrame();
    box.querySelector('#fb-onion').onclick = toggleOnion;
    box.querySelector('#fb-guides').onclick = toggleAnimGuides;
    box.querySelectorAll('[data-onion-mode]').forEach(b => { b.onclick = () => setOnionMode(b.dataset.onionMode); });
    box.querySelectorAll('[data-range]').forEach(r => { r.onclick = () => { state.onionRange = Number(r.dataset.range); store.set('onion-range', state.onionRange); renderStage(); document.querySelector(`[data-range="${state.onionRange}"]`)?.focus(); }; });
    box.querySelector('#fb-alpha').oninput = e => { state.onionAlpha = Number(e.target.value); store.set('onion-alpha', state.onionAlpha); S.redraw?.(); };
    box.querySelector('#fb-timeline').onclick = () => A.backToTimeline();
    const more = /** @type {HTMLDetailsElement} */ (box.querySelector('#fb-more'));
    more.ontoggle = () => { fb.open = more.open; };
    more.onkeydown = e => { if (e.key === 'Escape' && more.open) { e.stopPropagation(); more.open = false; more.querySelector('summary').focus(); } };
    const here = /** @type {HTMLElement | null} */ (strip.querySelector('[aria-current=true]'));
    if (here) strip.scrollLeft = here.parentElement.offsetLeft - (strip.clientWidth - here.offsetWidth) / 2;  // the painted frame stays in view without scrolling the page
    if (fb.playing && !fb.raf) fb.raf = requestAnimationFrame(tick); else if (!fb.playing) showFrame(index);
    return true;
  };

  /* ---------- keys ---------- */
  const baseKeydown = S.keydown;
  S.keydown = e => {
    const ctx = context(), mod = e.metaKey || e.ctrlKey || e.altKey;
    if (ctx && !mod) {
      const k = e.key.toLowerCase(), widget = e.target.closest?.('[role=tablist], [role=slider], [role=listbox], select, input, textarea');
      if (k === ' ' && e.shiftKey) { e.preventDefault(); if (!e.repeat) setPlaying(!fb.playing); return true; }
      if (k === 'o' && !e.shiftKey) { toggleOnion(); return true; }
      if (k === 'o' && e.shiftKey) {
        if (state.onionSkin) setOnionMode(state.onionMode === 'full' ? 'diff' : 'full'); else announce('Turn on onion skin first (O)');
        return true;
      }
      if (k === ',' || k === '.') { e.preventDefault(); paintFrame(k === ',' ? -1 : 1); return true; }
      if (!widget && !e.shiftKey && (e.key === 'ArrowLeft' || e.key === 'ArrowRight')) { e.preventDefault(); paintFrame(e.key === 'ArrowLeft' ? -1 : 1); return true; }
    }
    return baseKeydown(e);
  };
  shell()?.registerShortcuts?.('Paint · clip frame', [
    {keys: ['ArrowLeft', 'ArrowRight'], description: 'Paint the previous or next frame (while the keyboard cursor is hidden; Esc hides it)'},
    {keys: [',', '.'], description: 'Paint the previous or next frame, at any time'},
    {keys: ['Shift+Space'], description: 'Play or pause the flip-book preview'},
    {keys: ['O'], description: 'Onion skin on or off'}, {keys: ['Shift+O'], description: 'Onion skin: changes only or whole silhouettes'}]);
})();

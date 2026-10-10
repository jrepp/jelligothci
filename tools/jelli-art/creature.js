/* Jelli Art: the Creature view. Edit each form's per-pose clips (frames, durations, loop)
 * and preview them as the game draws them: the form's profile scale (6x without one),
 * ground anchor at (233, 256) on the 466 px round panel, frame chosen from elapsed real
 * time. Loaded after studio.js and served only by jelli_art.py; uses the review page's
 * globals and window.Studio, and exposes S.cr for behaviour.js. */
'use strict';
(() => {
  const S = window.Studio;
  if (!S) return;
  const PANEL = 466, SCALE = 6, CENTER = 233, FLOOR = 256, STRIP_SCALE = 2, HOLD_MS = 900;
  const POSE_HELP = {
    idle: 'The resting loop and the default pose.',
    'idle-alt': 'An occasional variation played over idle, such as a blink.',
    curious: 'An occasional idle posture.', content: 'An occasional idle posture.',
    eating: 'Shown while the pet eats.', happy: 'Touch, play and gift reactions.',
    asleep: 'Shown while the pet sleeps.', unwell: 'Shown while the pet is unwell or recovering.'};
  const reduced = window.matchMedia?.('(prefers-reduced-motion: reduce)');
  const working = {};  // clip key -> unsaved {frames, durations_ms, loop}
  const SPEEDS = [0.25, 0.5, 1, 2];
  const play = {on: !reduced?.matches, start: performance.now(), frame: 0, raf: null, drawn: {},
    speed: SPEEDS.includes(store.get('creature-speed', 1)) ? store.get('creature-speed', 1) : 1};
  Object.assign(state, {cForm: store.get('creature-form', null), cPose: store.get('creature-pose', 'idle'),
    cRepeat: store.get('creature-repeat', true), cBackground: store.get('creature-background', true), cGuides: false});
  const esc = s => String(s).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
  /* Extension points for behaviour.js: profile scale, simulated panel source, per-tick and play hooks. */
  const cr = S.cr = {tickHooks: [], playHooks: [], scaleFor: null, panelSource: null, step: null,
    renderBehaviour: null, formDirty: null, save: null};
  const scaleOf = form => cr.scaleFor?.(form) || SCALE;

  document.getElementById('views').insertAdjacentHTML('beforeend', '<button data-view="creature" aria-pressed="false">Creature</button>');
  document.querySelector('main').insertAdjacentHTML('beforeend', '<div id="creature" class="hidden"></div>');
  document.head.insertAdjacentHTML('beforeend', `<style>
    .cr-layout{display:grid;grid-template-columns:auto minmax(320px,1fr);gap:20px;align-items:start}
    @media(max-width:1100px){.cr-layout{grid-template-columns:1fr}}
    .cr-panel{display:grid;gap:8px;justify-items:start}.cr-panel canvas{border-radius:50%}
    .cr-readout{font:12px ui-monospace,monospace;color:var(--muted);min-height:18px}
    .cr-msgs{margin:6px 0;padding:0;list-style:none;font-size:12px}.cr-msgs li{color:var(--bad)}.cr-msgs li.ok{color:var(--muted)}
    .cr-strip{display:flex;flex-wrap:wrap;gap:10px}
    .cr-cell{display:grid;gap:4px;justify-items:center;cursor:pointer;background:var(--panel);border:2px solid var(--line);border-radius:8px;padding:6px;font:11px ui-monospace,monospace;color:var(--muted)}
    .cr-cell[aria-current=true]{border-color:var(--accent);color:var(--ink)}
    .cr-dirty{color:var(--warn)}button.cr-fallback{border-style:dashed;color:var(--muted)}
    .cr-pose-help{font-size:12px;color:var(--muted);margin:4px 0 0}
  </style>`);

  /* ---------- clip data ---------- */
  const forms = () => D.creature_forms?.length ? D.creature_forms
    : [...new Set(D.assets.filter(a => a.kind === 'creatures' && a.form).map(a => a.form))].map(art => ({art, name: null}));
  /* The 8 base poses, then the manifest's state poses: optional per form, falling back to a base pose. */
  const statePoses = () => D.state_poses || [];
  const poses = () => [...(D.creature_poses || []), ...statePoses().map(p => p.name)];
  const fallbackOf = pose => statePoses().find(p => p.name === pose)?.fallback || null;
  const cap = () => D.clip_frame_cap || 6, maxMs = () => D.clip_duration_max_ms || 10000;
  const clipOf = key => (D.clips || []).find(c => c.key === key);
  /* working[key] is an unsaved clip, or {removed: true} for a state pose clip that will be deleted. */
  const current = key => working[key]?.removed ? undefined : working[key] || clipOf(key);
  const fallbackKey = key => { const [form, pose] = key.split('.'); const f = fallbackOf(pose); return f ? `${form}.${f}` : null; };
  /* The clip the game plays for a pose: its own, else the state pose's fallback. */
  const resolved = key => current(key) || (fallbackKey(key) && current(fallbackKey(key)));
  const copy = c => ({frames: [...c.frames], durations_ms: [...c.durations_ms], loop: c.loop});
  const sameClip = (a, b) => JSON.stringify(copy(a)) === JSON.stringify(copy(b));
  const clipDirty = key => {
    const w = working[key], saved = clipOf(key);
    if (!w) return false;
    if (w.removed) return !!saved;
    return !saved || !sameClip(w, saved);
  };
  const dirtyKeys = () => Object.keys(working).filter(clipDirty);
  S.clipsDirty = () => dirtyKeys().length > 0;
  const selectedKey = () => `${state.cForm}.${state.cPose}`;
  const formLabel = f => f.name ? `${f.name} · ${f.art}` : f.art;
  const formFrames = form => D.assets.filter(a => a.kind === 'creatures' && a.form === form).sort((a, b) => a.id - b.id);

  function problems(key, c) {
    const out = [], form = key.split('.')[0];
    if (!c.frames.length) out.push('Add at least one frame.');
    if (c.frames.length > cap()) out.push(`Use at most ${cap()} frames; that is the runtime clip cap.`);
    c.durations_ms.forEach((n, i) => { if (!Number.isInteger(n) || n < 1 || n > maxMs()) out.push(`Frame ${i + 1}: duration must be a whole number of milliseconds from 1 to ${maxMs()}.`); });
    c.frames.forEach((k, i) => { if (byKey[k]?.form !== form) out.push(`Frame ${i + 1}: ${k} does not belong to the ${form} form.`); });
    return out;
  }
  function change(fn, rerender = true) {
    const key = selectedKey(), base = current(key); if (!base) return;
    const w = working[key] ||= copy(base); fn(w);
    if (!clipDirty(key)) delete working[key];
    restart();
    if (rerender) renderEditor(); else renderSaveState();
  }

  /* ---------- timing: frame from elapsed milliseconds, never from frame counts ---------- */
  const safeDurations = c => c.durations_ms.map(n => Number.isFinite(n) && n > 0 ? n : 1);
  const totalOf = c => safeDurations(c).reduce((n, v) => n + v, 0);
  /* The clip's own time: loops wrap, one-shots hold the last frame (or replay after HOLD_MS when repeating). */
  function clipTime(c, elapsed, repeat) {
    const total = totalOf(c);
    if (c.loop) return elapsed % total;
    return repeat ? elapsed % (total + HOLD_MS) : elapsed;
  }
  /* animation.js clipFrame mirrors core/creature.c jelli_clip_frame. */
  function frameAt(c, elapsed, repeat) {
    if (!c.frames.length || !totalOf(c)) return -1;
    return window.JelliAnimation.clipFrame({...c, durations_ms: safeDurations(c)}, clipTime(c, elapsed, repeat));
  }
  const offsetOf = (c, i) => safeDurations(c).slice(0, i).reduce((n, v) => n + v, 0);
  const elapsedAt = now => (now - play.start) * play.speed;  // playback speed scales the game's elapsed time
  function restart() { play.start = performance.now(); play.frame = 0; play.drawn = {}; schedule(); }
  function setSpeed(speed) {
    if (!SPEEDS.includes(speed)) return;
    const now = performance.now(), elapsed = elapsedAt(now);
    play.speed = speed; play.start = now - elapsed / speed; store.set('creature-speed', speed);
    play.drawn = {}; schedule();
  }
  function seek(i) {  // pause on frame i of the selected clip
    const c = resolved(selectedKey()); if (!c?.frames.length) return;
    if (play.on) setPlaying(false);
    play.frame = Math.max(0, Math.min(i, c.frames.length - 1)); play.drawn = {}; schedule();
  }
  function setPlaying(on) {
    const c = resolved(selectedKey());
    if (on && c) play.start = performance.now() - offsetOf(c, Math.max(0, play.frame)) / play.speed;
    if (!on && c) play.frame = Math.max(0, frameAt(c, elapsedAt(performance.now()), state.cRepeat));
    play.on = on; play.drawn = {};
    for (const hook of cr.playHooks) hook(on);
    renderTransport(); schedule();
  }
  function stepFrame(n) {
    if (cr.step?.(n)) { play.drawn = {}; return schedule(); }  // the simulation steps by idle beat
    const c = resolved(selectedKey()); if (!c?.frames.length) return;
    if (play.on) setPlaying(false);
    play.frame = (play.frame + n + c.frames.length) % c.frames.length; play.drawn = {}; schedule();
  }

  /* ---------- drawing ---------- */
  function groundAnchor(p) {  // tools/assets/sprite_geometry.ground_anchor_q8, from the working pixels
    let bottom = 0;
    for (let y = p.h - 1; y >= 0 && !bottom; y--) for (let x = 0; x < p.w; x++) if (p.data[(y * p.w + x) * 4 + 3]) { bottom = y + 1; break; }
    if (!bottom) return null;
    let sum = 0, n = 0;
    for (let y = Math.max(0, bottom - 3); y < bottom; y++) for (let x = 0; x < p.w; x++) if (p.data[(y * p.w + x) * 4 + 3]) { sum += 2 * x + 1; n++; }
    return [Math.floor((sum * 128 + Math.floor(n / 2)) / n), bottom * 256];
  }
  function drawSprite(ctx, key, scale, cx, floor) {
    const p = decoded[key]?.after, anchor = p && groundAnchor(p); if (!anchor) return;
    const x = cx - Math.floor((anchor[0] * scale + 128) / 256), y = floor - Math.floor((anchor[1] * scale + 128) / 256);
    ctx.drawImage(p.img, x, y, p.w * scale, p.h * scale);
  }
  function drawPanel(view, key) {
    const {ctx, css} = view, k = DPR * css / PANEL;
    ctx.setTransform(k, 0, 0, k, 0, 0); ctx.imageSmoothingEnabled = false; ctx.clearRect(0, 0, PANEL, PANEL);
    ctx.save(); ctx.beginPath(); ctx.arc(CENTER, CENTER, CENTER, 0, 7); ctx.clip();
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, PANEL, PANEL);
    const bg = decoded['backgrounds.home']?.after;
    if (bg && state.cBackground) ctx.drawImage(bg.img, 0, 0, PANEL, PANEL);
    if (key) drawSprite(ctx, key, scaleOf(state.cForm), CENTER, FLOOR);
    if (state.cGuides) {
      ctx.fillStyle = 'rgba(133,228,182,.85)'; ctx.fillRect(0, FLOOR, PANEL, 1); ctx.fillRect(CENTER, FLOOR - 12, 1, 24);
    }
    ctx.restore();
  }
  function drawCell(view, key) {
    const {ctx, w, h} = view;
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0); ctx.imageSmoothingEnabled = false;
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, w, h); ctx.fillStyle = '#49334f'; ctx.fillRect(0, h - 10, w, 1);
    if (key) drawSprite(ctx, key, STRIP_SCALE, w / 2, h - 10);
  }

  /* One animation loop for the panel and the pose strip, scheduled only while visible. */
  let panelView = null, cells = [];
  function schedule() { if (play.raf === null) play.raf = requestAnimationFrame(tick); }
  function tick(now) {
    play.raf = null;
    if (state.view !== 'creature' || document.hidden || !panelView) return;
    const elapsed = elapsedAt(now), scale = scaleOf(state.cForm);
    const sim = cr.panelSource?.(now);  // {key, text} while simulating behaviour
    let frameKey = null, text = '', index = -1;
    if (sim) ({key: frameKey, text} = sim);
    else {
      const c = resolved(selectedKey());
      index = c ? (play.on ? frameAt(c, elapsed, state.cRepeat) : Math.min(play.frame, c.frames.length - 1)) : -1;
      frameKey = index >= 0 ? c.frames[index] : null;
      const fallback = current(selectedKey()) ? '' : ` · fallback ${fallbackOf(state.cPose)}`;
      text = index < 0 ? 'No frames' : `frame ${index + 1}/${c.frames.length} · ${c.durations_ms[index]} ms · ${c.loop ? 'loops' : 'plays once, holds last frame'}${fallback}${play.speed === 1 ? '' : ` · ${play.speed}× speed`}${play.on ? '' : ' · paused'}`;
      if (index >= 0) cr.onFrame?.(index, Math.min(play.on ? clipTime(c, elapsed, state.cRepeat) : offsetOf(c, index), totalOf(c)), c);
    }
    const signature = `${frameKey}|${text}|${scale}`;
    if (play.drawn.panel !== signature) {
      drawPanel(panelView, frameKey);
      document.querySelectorAll('.cr-frame').forEach(el => el.classList.toggle('now', Number(el.dataset.index) === index));
      const readout = document.getElementById('cr-readout'); if (readout) readout.textContent = text;
      const label = document.getElementById('cr-scale'); if (label) label.textContent = `${scale}×`;
      play.drawn.panel = signature;
    }
    for (const hook of cr.tickHooks) hook(now);
    for (const cell of cells) {
      const cc = resolved(cell.key), i = cc ? (play.on ? frameAt(cc, elapsed, true) : 0) : -1;
      if (play.drawn[cell.key] !== i) { drawCell(cell, i >= 0 ? cc.frames[i] : null); play.drawn[cell.key] = i; }
    }
    if (play.on) schedule();
  }
  document.addEventListener('visibilitychange', () => { if (!document.hidden && state.view === 'creature') { play.drawn = {}; schedule(); } });
  reduced?.addEventListener?.('change', e => { if (e.matches && play.on) setPlaying(false); });

  /* ---------- rendering ---------- */
  function renderCreature() {
    const el = document.getElementById('creature'), list = forms();
    if (!list.length || !poses().length) { el.innerHTML = '<p class="studio-note">This manifest has no creature forms or clips.</p>'; panelView = null; return; }
    if (!list.some(f => f.art === state.cForm)) state.cForm = list[0].art;
    if (!poses().includes(state.cPose)) state.cPose = poses()[0];
    const css = Math.min(PANEL, Math.max(240, window.innerWidth - 360));
    el.innerHTML = `<div class="title"><h2>Creature</h2><div class="seg" id="cr-forms" role="group" aria-label="Creature form"></div></div>
      <div class="cr-layout">
        <div class="cr-panel"><div class="lbl"><span>On the panel · <span id="cr-scale">${scaleOf(state.cForm)}×</span> · ground at (${CENTER}, ${FLOOR})</span></div><div id="cr-canvas"></div>
          <div class="cr-readout" id="cr-readout" aria-live="off"></div>
          <div class="bar" id="cr-transport" style="max-width:${css}px"></div></div>
        <div id="cr-editor"></div>
      </div>
      <section class="block"><h3>All poses <span class="lbl">· click one to edit it</span></h3><div class="cr-strip" id="cr-strip"></div></section>
      <section class="block" id="cr-behaviour"></section>
      <div class="kbd"><kbd>Space</kbd> play/pause · <kbd>,</kbd>/<kbd>.</kbd> step frames (idle beats while simulating) · <kbd>J</kbd>/<kbd>K</kbd> next/previous pose · <kbd>1</kbd>–<kbd>${poses().length}</kbd> pick a pose · <kbd>⌘S</kbd> save clips and behaviour</div>`;
    const [c, ctx] = makeCanvas(css, css); document.getElementById('cr-canvas').append(c);
    c.setAttribute('role', 'img'); c.setAttribute('aria-label', 'Animated preview of the selected clip on the round panel');
    panelView = {c, ctx, css};
    renderForms(); renderTransport(); renderEditor(); renderStrip();
    cr.renderBehaviour?.(document.getElementById('cr-behaviour'));
    play.drawn = {}; schedule();
  }
  function renderForms() {
    const el = document.getElementById('cr-forms'); if (!el) return;
    el.textContent = '';
    for (const f of forms()) {
      const b = document.createElement('button'), dirty = dirtyKeys().some(k => k.startsWith(f.art + '.'));
      b.innerHTML = `${esc(formLabel(f))}${dirty ? ' <span class="cr-dirty" title="unsaved clip edits">●</span>' : ''}`;
      b.setAttribute('aria-pressed', String(f.art === state.cForm));
      if (cr.formDirty?.(f.art) && !dirty) b.insertAdjacentHTML('beforeend', ' <span class="cr-dirty" title="unsaved behaviour or size edits">●</span>');
      b.onclick = () => { state.cForm = f.art; store.set('creature-form', f.art); restart(); renderCreature(); };
      el.append(b);
    }
  }
  function renderTransport() {
    const el = document.getElementById('cr-transport'); if (!el) return;
    el.innerHTML = `<div class="seg cr-keys"><button id="cr-play" aria-keyshortcuts="Space">${play.on ? 'Pause' : 'Play'}<kbd>Space</kbd></button><button id="cr-prev" title="Previous frame" aria-keyshortcuts="ArrowLeft ,">◀ Step<kbd>←</kbd></button><button id="cr-next" title="Next frame" aria-keyshortcuts="ArrowRight .">Step ▶<kbd>→</kbd></button><button id="cr-restart" title="Play from the first frame">Restart</button></div>
      <div class="seg"><button id="cr-repeat" aria-pressed="${state.cRepeat}" title="The game plays a one-shot clip once and holds its last frame; this replays it after a pause">Replay one-shots</button><button id="cr-bg" aria-pressed="${state.cBackground}">Home background</button><button id="cr-guides" aria-pressed="${state.cGuides}" title="Ground line and centre">Guides</button></div>
      <label class="lbl" for="cr-speed">Speed</label><select id="cr-speed" title="Playback speed; the game always plays at 1×">${SPEEDS.map(s => `<option value="${s}"${s === play.speed ? ' selected' : ''}>${s}×</option>`).join('')}</select>
      ${reduced?.matches ? '<span class="studio-note">Reduced motion is on, so the preview starts paused. Step through frames or press Play.</span>' : ''}`;
    el.querySelector('#cr-play').onclick = () => setPlaying(!play.on);
    el.querySelector('#cr-prev').onclick = () => stepFrame(-1);
    el.querySelector('#cr-next').onclick = () => stepFrame(1);
    el.querySelector('#cr-restart').onclick = () => { restart(); cr.restartSimulation?.(); if (!play.on) setPlaying(true); };
    el.querySelector('#cr-repeat').onclick = () => { state.cRepeat = !state.cRepeat; store.set('creature-repeat', state.cRepeat); restart(); renderTransport(); };
    el.querySelector('#cr-bg').onclick = () => { state.cBackground = !state.cBackground; store.set('creature-background', state.cBackground); play.drawn = {}; renderTransport(); schedule(); };
    el.querySelector('#cr-guides').onclick = () => { state.cGuides = !state.cGuides; play.drawn = {}; renderTransport(); schedule(); };
    el.querySelector('#cr-speed').onchange = e => setSpeed(Number(e.target.value));
  }
  function renderEditor() {
    const el = document.getElementById('cr-editor'); if (!el) return;
    const key = selectedKey(), c = current(key), fallback = fallbackOf(state.cPose);
    const tab = (p, i) => {
      const f = fallbackOf(p), own = current(`${state.cForm}.${p}`);
      const title = f ? `State pose · ${own ? 'own clip' : `uses fallback ${f}`}` : POSE_HELP[p] || p;
      return `<button data-pose="${esc(p)}" aria-pressed="${p === state.cPose}" title="${esc(title)} (${i + 1})"${f && !own ? ' class="cr-fallback"' : ''}>${esc(p)}${clipDirty(`${state.cForm}.${p}`) ? ' <span class="cr-dirty">●</span>' : ''}</button>`;
    };
    const base = (D.creature_poses || []).length, list = poses();
    const tabs = `${list.slice(0, base).map(tab).join('')}</div><div class="lbl" style="margin-top:6px">State poses</div><div class="seg" style="flex-wrap:wrap">${list.slice(base).map((p, i) => tab(p, i + base)).join('')}`;
    if (!c) {
      const msg = fallback
        ? `<p class="cr-pose-help">State pose with no ${esc(state.cForm)} clip: it <b>uses fallback: ${esc(fallback)}</b>, which the preview plays.</p>
           <div class="bar"><button id="cr-add-clip" title="Start from a copy of the ${esc(fallback)} clip">Add clip</button>
           ${clipDirty(key) ? '<button id="cr-revert">Keep the clip</button>' : ''}</div><ul class="cr-msgs" id="cr-msgs"></ul>
           <div class="bar"><div class="seg"><button id="cr-revert-all">Revert all</button><button id="cr-save" class="primary" title="Save clips (⌘S)">Save clips</button></div></div>`
        : `<p class="studio-note">No clip ${esc(key)} in assets.json.</p>`;
      el.innerHTML = `<div class="lbl">Pose</div><div class="seg" id="cr-poses" style="flex-wrap:wrap">${tabs}</div>${msg}`;
      wirePoses(el);
      el.querySelector('#cr-add-clip')?.addEventListener('click', () => {
        const from = resolved(key); if (!from) return;
        if (working[key]?.removed) delete working[key]; else working[key] = copy(from);
        restart(); renderEditor();
      });
      el.querySelector('#cr-revert')?.addEventListener('click', () => { delete working[key]; restart(); renderEditor(); });
      wireSaveBar(el);
      return;
    }
    el.innerHTML = `<div class="lbl">Pose</div><div class="seg" id="cr-poses" style="flex-wrap:wrap">${tabs}</div>
      <p class="cr-pose-help">${esc(fallback ? `State pose; without this clip it falls back to ${fallback}.` : POSE_HELP[state.cPose] || '')} Clip <b>${esc(key)}</b> · ${clipOf(key) ? `ID ${clipOf(key).id}` : 'new, ID assigned on save'}
        ${fallback ? `<button id="cr-remove-clip" title="Delete this clip so the ${esc(fallback)} clip plays">Remove clip (use ${esc(fallback)})</button>` : ''}</p>
      <div id="cr-timeline"></div>
      <div class="bar"><button id="cr-loop" aria-pressed="${c.loop}" title="Loop cycles the frames; otherwise the clip plays once and holds the last frame">Loop</button>
        <span class="studio-note" style="margin:0">${c.loop ? 'Cycles while the pose lasts.' : 'Plays once, then holds the last frame.'}</span></div>
      <ul class="cr-msgs" id="cr-msgs"></ul>
      <div id="cr-library"></div>
      <div class="bar" style="margin-top:12px"><div class="seg"><button id="cr-revert">Revert pose</button><button id="cr-revert-all">Revert all</button><button id="cr-save" class="primary" title="Save clips (⌘S)">Save clips</button></div></div>`;
    wirePoses(el);
    el.querySelector('#cr-loop').onclick = () => change(w => { w.loop = !w.loop; });
    cr.renderTimeline?.(el.querySelector('#cr-timeline'), el.querySelector('#cr-library'), key, c);
    el.querySelector('#cr-revert').onclick = () => { delete working[key]; restart(); renderEditor(); };
    el.querySelector('#cr-remove-clip')?.addEventListener('click', () => {
      if (clipOf(key)) working[key] = {removed: true}; else delete working[key];
      restart(); renderEditor();
    });
    wireSaveBar(el);
  }
  function wireSaveBar(el) {
    el.querySelector('#cr-revert-all').onclick = async () => {
      const n = dirtyKeys().length, clips = `${n} clip${n === 1 ? '' : 's'}`, text = `Discard unsaved edits to ${clips}?`;
      if (n && !await (window.JelliShell?.confirm ? window.JelliShell.confirm(text, {title: 'Discard clip edits', confirmLabel: `Discard ${clips}`, danger: true}) : confirm(text))) return;
      for (const k of Object.keys(working)) delete working[k];
      restart(); renderEditor();
    };
    el.querySelector('#cr-save').onclick = saveClips;
    renderSaveState();
  }
  function wirePoses(el) {
    el.querySelectorAll('button[data-pose]').forEach(b => { b.onclick = () => selectPose(b.dataset.pose); });
  }
  function selectPose(pose) {
    state.cPose = pose; store.set('creature-pose', pose); restart(); renderEditor(); renderStripState();
  }
  /* Messages, save button, form and strip markers: updated without rebuilding inputs mid-typing. */
  function renderSaveState() {
    const key = selectedKey(), c = current(key), msgs = document.getElementById('cr-msgs'), save = document.getElementById('cr-save');
    const issues = c ? problems(key, c) : [], blocked = dirtyKeys().filter(k => current(k) && problems(k, current(k)).length);
    if (msgs) {
      const others = blocked.filter(k => k !== key);
      msgs.innerHTML = issues.map(m => `<li>${esc(m)}</li>`).join('') + (others.length ? `<li>Fix ${others.map(esc).join(', ')} before saving.</li>` : '')
        + (!issues.length && !others.length ? `<li class="ok">${dirtyKeys().length ? `${dirtyKeys().length} clip${dirtyKeys().length > 1 ? 's' : ''} with unsaved edits.` : 'No unsaved clip edits.'}</li>` : '');
    }
    if (save) { save.disabled = !dirtyKeys().length || blocked.length > 0; save.textContent = dirtyKeys().length ? `Save clips (${dirtyKeys().length})` : 'Saved'; }
    const revert = document.getElementById('cr-revert'); if (revert) revert.disabled = !clipDirty(key);
    const all = document.getElementById('cr-revert-all'); if (all) all.disabled = !dirtyKeys().length;
    renderForms(); renderStripState();
  }
  function renderStrip() {
    const el = document.getElementById('cr-strip'); el.textContent = ''; cells = [];
    const size = Math.max(...formFrames(state.cForm).map(a => Math.max(a.width, a.height)), 32) * STRIP_SCALE + 16;
    poses().forEach((pose, i) => {
      const key = `${state.cForm}.${pose}`, cell = document.createElement('div'); cell.className = 'cr-cell'; cell.dataset.pose = pose;
      cell.setAttribute('role', 'button'); cell.tabIndex = 0; cell.title = POSE_HELP[pose] || pose;
      const [c, ctx] = makeCanvas(size, size + 6); cell.append(c);
      cell.insertAdjacentHTML('beforeend', `<span>${i + 1}. ${esc(pose)}</span><span class="sub"></span>`);
      cell.onclick = () => selectPose(pose); cell.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); e.stopPropagation(); selectPose(pose); } };
      el.append(cell); cells.push({key, ctx, w: size, h: size + 6, el: cell});
    });
    renderStripState();
  }
  function renderStripState() {
    for (const cell of cells) {
      const c = current(cell.key), sub = cell.el.querySelector('.sub'), dot = clipDirty(cell.key) ? ' <span class="cr-dirty">●</span>' : '';
      cell.el.setAttribute('aria-current', String(cell.key === selectedKey()));
      const f = fallbackOf(cell.key.split('.')[1]);
      if (sub) sub.innerHTML = c ? `${c.frames.length}f · ${c.loop ? 'loop' : 'once'}${dot}` : f ? `uses ${esc(f)}${dot}` : 'missing';
    }
    play.drawn = {}; schedule();
  }
  function paint(key) {
    state.view = 'detail'; state.mode = 'paint'; select(key);
  }

  /* ---------- save ---------- */
  async function saveClips() {
    const keys = dirtyKeys(); if (!keys.length) return;
    const blocked = keys.filter(k => current(k) && problems(k, current(k)).length);
    if (blocked.length) return S.status(`Fix ${blocked.join(', ')} before saving`, 'bad', true);
    try {
      const clips = keys.map(k => working[k].removed ? {key: k, remove: true} : {key: k, ...copy(working[k])});
      const res = await S.api('POST', '/api/clips', {clips, artist: state.artist});
      const added = keys.some(k => !clipOf(k)), removed = keys.some(k => working[k].removed);
      for (const k of keys) { if (clipOf(k) && !working[k].removed) Object.assign(clipOf(k), copy(working[k])); delete working[k]; }
      D.version = res.version;  // before reloading, so the disk poll does not reload a second time
      if (added || removed) await S.reload();  // new clips get IDs from the server; removed ones leave D.clips
      else D.version = res.version;
      renderEditor(); renderStrip();
      if (res.git_error) S.status(`Saved clips, but the commit failed: ${res.git_error}`, 'bad', true);
      else if (res.warning) S.status(res.warning, 'warn', true);
      else {
        const what = res.changed.length === 1 ? res.changed[0] : `${res.changed.length} clips`;
        S.status(res.commit ? `Saved ${what} (commit ${res.commit}).` : `Saved ${what} to assets.json.`, '', false, {keep: true});
      }
    } catch (err) { S.status(`Clip save failed: ${err.message}`, 'bad', true); }
  }

  /* ---------- hooks into the review page ---------- */
  const baseRenderView = renderView, baseSelect = select, baseKeydown = S.keydown;
  renderView = () => {  // eslint-disable-line no-global-assign
    const on = state.view === 'creature';
    document.getElementById('creature').classList.toggle('hidden', !on);
    if (!on) { panelView = null; cells = []; return baseRenderView(); }
    if (!panelView) document.querySelector('main').scrollTop = 0;  // arriving from a scrolled detail view
    document.getElementById('detail').classList.add('hidden'); document.getElementById('sheet').classList.add('hidden');
    document.querySelectorAll('#views button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.view === 'creature')));
    renderCreature();
  };
  select = key => {  // eslint-disable-line no-global-assign
    if (state.view === 'creature') {
      const a = byKey[key];
      if (a?.kind === 'creatures' && a.form) { state.cForm = a.form; store.set('creature-form', a.form); }
      else state.view = 'detail';
    }
    baseSelect(key);
  };
  S.keydown = e => {
    const mod = e.metaKey || e.ctrlKey, k = e.key.toLowerCase();
    if (state.view !== 'creature') return baseKeydown(e);
    if (mod && k === 's') { e.preventDefault(); saveClips(); cr.save?.(); return true; }
    if (mod || e.altKey) return false;
    /* Space activates a focused button, and arrows adjust a focused slider or menu, instead of driving playback. */
    if (k === ' ' && e.target.closest?.('button, [role=button], a, summary')) return true;
    if (k.startsWith('arrow') && e.target.closest?.('[role=slider], select')) return true;
    if (k === ' ') { e.preventDefault(); if (!e.repeat) setPlaying(!play.on); return true; }
    if (k === ',' || e.key === 'ArrowLeft') { e.preventDefault(); stepFrame(-1); return true; }
    if (k === '.' || e.key === 'ArrowRight') { e.preventDefault(); stepFrame(1); return true; }
    const list = poses(), i = list.indexOf(state.cPose);
    if (k === 'j' || e.key === 'ArrowDown') { e.preventDefault(); selectPose(list[(i + 1) % list.length]); return true; }
    if (k === 'k' || e.key === 'ArrowUp') { e.preventDefault(); selectPose(list[(i - 1 + list.length) % list.length]); return true; }
    if (/^[1-9]$/.test(k) && Number(k) <= list.length) { selectPose(list[Number(k) - 1]); return true; }
    return ['a', 'x', 'g', 'i', 'b', 'd', '0', '[', ']', '-', '=', '+'].includes(k);  // detail-view keys do nothing here
  };
  Object.assign(cr, {esc, poses, statePoses, fallbackOf, resolved, forms, formLabel, formFrames, clipOf, current, frameAt, drawSprite, groundAnchor,
    selectedKey, setPlaying, playing: () => play.on, redraw: () => { play.drawn = {}; schedule(); }, rerender: () => renderCreature(),
    renderForms: () => renderForms(), PANEL, CENTER, FLOOR, reduced,
    change, restart, seek, paint, cap, maxMs, clipDirty, copy, offsetOf, totalOf, safeDurations, renderEditor: () => renderEditor(),
    pausedFrame: () => play.on ? null : play.frame});
  let resizeTimer = null;
  window.addEventListener('resize', () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(() => { if (state.view === 'creature') renderCreature(); }, 150); });
})();

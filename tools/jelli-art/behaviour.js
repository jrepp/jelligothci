/* Jelli Art: the "Behaviour & size" panel of the Creature view, for content/creatures.json.
 * A form's profile sets its actor, icon and portrait scales and names a behaviour:
 * first-match-wins condition -> pose rules, an idle beat schedule (one pose per 900 ms
 * beat) and a quiet cycle that holds curious/content beats as idle every Nth cycle,
 * offset by pet ID. This mirrors core/creature.c and core/pet_actor.c for the preview;
 * the checkout's tools/assets/creature_data.py validates saves. Loaded after creature.js. */
'use strict';
(() => {
  const S = window.Studio, cr = S?.cr;
  if (!cr) return;
  const esc = cr.esc;
  const CONDITION_HELP = {
    asleep: 'The pet is asleep.', wake_groggy: 'Just woken after too little rest.',
    wake_surprised: 'Just woken by a touch.', wake_happy: 'Just woken well rested.',
    unwell: 'Unwell or recovering.', eating: 'Eating.', playing: 'Playing, giving or exercising.',
    touch_happy: 'A pleasant touch reaction.', touch_upset: 'An upset touch reaction.'};
  const GESTURES = new Set(['curious', 'content']);
  const clone = v => JSON.parse(JSON.stringify(v));
  let work = null, base = null, seen = undefined, stale = false;
  const sim = {on: store.get('creature-simulate', false), conds: new Set(), petId: 1, start: performance.now(), pausedAt: null};
  if (!cr.playing()) sim.pausedAt = sim.start;
  let timeline = null;  // {rows, count, cells: [{ctx, w, h, el, label}]}

  document.head.insertAdjacentHTML('beforeend', `<style>
    .bh-grid{display:grid;grid-template-columns:minmax(260px,360px) 1fr;gap:24px;align-items:start}
    @media(max-width:1100px){.bh-grid{grid-template-columns:1fr}}
    #cr-behaviour fieldset{border:0;padding:0;margin:0;min-width:0}
    .bh-row{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:4px 0}
    .bh-row select,.bh-row input[type=number]{width:auto}
    .bh-size{font:11px ui-monospace,monospace;color:var(--muted)}.bh-size.bad{color:var(--bad)}
    .bh-rules{list-style:none;margin:6px 0;padding:0;display:grid;gap:4px}
    .bh-rules li{display:flex;gap:6px;align-items:center;flex-wrap:wrap;background:var(--raised);border:1px solid var(--line);border-radius:6px;padding:4px 6px}
    .bh-rules li.hit{border-color:var(--accent)}
    .bh-rules .n{font:11px ui-monospace,monospace;color:var(--muted);width:20px}
    .bh-beats{display:flex;flex-wrap:wrap;gap:4px;margin:6px 0}
    .bh-beats select{font:11px ui-monospace,monospace;padding:3px 4px}
    .bh-warn{color:var(--warn);font-size:12px;margin:6px 0}.bh-bad{color:var(--bad);font-size:12px;margin:6px 0}
    .bh-cond{display:flex;flex-wrap:wrap;gap:4px}
    .bh-timeline{display:grid;gap:4px;margin-top:8px;overflow-x:auto}
    .bh-tl-row{display:flex;gap:3px;align-items:end}
    .bh-tl-row .cyc{font:10px ui-monospace,monospace;color:var(--muted);width:46px}
    .bh-cell{display:grid;justify-items:center;gap:1px;border:2px solid var(--line);border-radius:6px;padding:2px;background:#000;font:9px ui-monospace,monospace;color:var(--muted)}
    .bh-cell.now{border-color:var(--accent);color:var(--ink)}.bh-cell.quiet{border-style:dashed}
  </style>`);

  /* ---------- data ---------- */
  const L = () => D.creature_limits || {};
  const dirty = () => !!work && !!D.creature_data && JSON.stringify(work) !== JSON.stringify(D.creature_data);
  const baseClipsDirty = S.clipsDirty;
  S.clipsDirty = () => baseClipsDirty() || dirty();  // also guards the page against closing with edits
  function sync() {
    if (D.creature_data_sha === seen) return;
    seen = D.creature_data_sha;
    if (!dirty() || !work) { work = D.creature_data ? clone(D.creature_data) : null; base = D.creature_data_sha; stale = false; }
    else stale = true;
  }
  const profileOf = art => work?.profiles?.find(p => p.art === art);
  const behaviourOf = name => work?.behaviors?.find(b => b.name === name);
  const usersOf = name => (work?.profiles || []).filter(p => p.behavior === name).map(p => p.art);
  const savedProfile = art => D.creature_data?.profiles?.find(p => p.art === art);
  cr.scaleFor = art => profileOf(art)?.scale;
  cr.formDirty = art => {
    const p = profileOf(art), s = savedProfile(art);
    if (JSON.stringify(p) !== JSON.stringify(s)) return true;
    const b = p && behaviourOf(p.behavior), sb = p && D.creature_data?.behaviors?.find(x => x.name === p.behavior);
    return JSON.stringify(b) !== JSON.stringify(sb);
  };

  function sizeReport(art, p) {
    const frames = cr.formFrames(art).filter(a => a.bounds), lim = L(), out = {issues: []};
    let wide = null, tall = null;
    for (const a of frames) {
      const [l, t, r, b] = a.bounds, w = (r - l) * p.scale, h = (b - t) * p.scale;
      if (!wide || w > wide.w) wide = {w, key: a.key, src: r - l};
      if (!tall || h > tall.h) tall = {h, key: a.key, src: b - t};
    }
    const width = Math.max(0, ...frames.map(a => a.width));
    out.actor = wide && tall ? `${wide.w}×${tall.h} px on the panel (widest ${wide.key.split('.')[1]} ${wide.src} px, tallest ${tall.key.split('.')[1]} ${tall.src} px); limit ${lim.max_actor_width}×${lim.max_actor_height}` : 'no frames';
    if (wide && wide.w > lim.max_actor_width) out.issues.push(`${art}: ${wide.key} is ${wide.w} px wide at ${p.scale}×; the panel allows ${lim.max_actor_width}.`);
    if (tall && tall.h > lim.max_actor_height) out.issues.push(`${art}: ${tall.key} is ${tall.h} px tall at ${p.scale}×; it would overlap the heading above ${lim.max_actor_height} px.`);
    out.icon = `${width} px frame → ${width * p.icon_scale} px icon; limit ${lim.max_icon}`;
    if (width * p.icon_scale > lim.max_icon) out.issues.push(`${art}: the ${width * p.icon_scale} px icon exceeds a ${lim.max_icon} px collection cell.`);
    out.portrait = `${width} px frame → ${width * p.portrait_scale} px portrait`;
    for (const f of ['scale', 'icon_scale', 'portrait_scale'])
      if (!Number.isInteger(p[f]) || p[f] < lim.scale_min || p[f] > lim.scale_max) out.issues.push(`${art}: ${f} must be ${lim.scale_min}–${lim.scale_max}.`);
    return out;
  }
  function behaviourIssues(b) {
    const lim = L(), out = [], seenWhen = new Set(), name = b.name || '(unnamed)';
    if (!b.name) out.push('A behaviour needs a name.');
    if (!b.pose_rules.length || b.pose_rules.length > lim.rule_capacity) out.push(`${name}: use 1–${lim.rule_capacity} pose rules.`);
    for (const r of b.pose_rules) {
      if (!lim.conditions.includes(r.when)) out.push(`${name}: unknown condition ${r.when}.`);
      if (!cr.poses().includes(r.pose)) out.push(`${name}: unknown pose ${r.pose}.`);
      if (seenWhen.has(r.when)) out.push(`${name}: ${r.when} is listed twice; only the first can ever match.`);
      seenWhen.add(r.when);
    }
    if (!b.idle_beats.length || b.idle_beats.length > lim.beat_capacity) out.push(`${name}: use 1–${lim.beat_capacity} idle beats.`);
    if (b.idle_beats.some(p => !lim.idle_poses.includes(p))) out.push(`${name}: idle beats use ${lim.idle_poses.join(', ')}.`);
    if (!Number.isInteger(b.quiet_cycle) || b.quiet_cycle < 0 || b.quiet_cycle > lim.quiet_max) out.push(`${name}: quiet cycle must be 0–${lim.quiet_max}.`);
    return out;
  }
  function allIssues() {
    if (!work) return [];
    const out = [], names = new Set();
    for (const b of work.behaviors) { if (names.has(b.name)) out.push(`Two behaviours are named ${b.name}.`); names.add(b.name); out.push(...behaviourIssues(b)); }
    for (const p of work.profiles) {
      if (!behaviourOf(p.behavior)) out.push(`${p.art}: unknown behaviour ${p.behavior}.`);
      out.push(...sizeReport(p.art, p).issues);
    }
    for (const f of cr.forms()) if (f.id !== null && f.id !== undefined && !profileOf(f.art)) out.push(`${f.name || f.art} has no profile.`);
    return out;
  }
  function edit(fn) { fn(work); render(); cr.renderForms(); cr.redraw(); }

  /* ---------- simulation: mirrors creature_pose() and jelli_creature_idle_pose() ---------- */
  const simNow = now => (sim.pausedAt ?? now) - sim.start;
  function restartSim() { sim.start = performance.now(); sim.pausedAt = cr.playing() ? null : sim.start; cr.redraw(); }
  cr.restartSimulation = restartSim;
  cr.playHooks.push(on => {
    const now = performance.now();
    if (on && sim.pausedAt !== null) { sim.start += now - sim.pausedAt; sim.pausedAt = null; }
    if (!on && sim.pausedAt === null) sim.pausedAt = now;
  });
  function idlePose(b, beat) {
    const count = b.idle_beats.length; if (!count) return {pose: 'idle', quiet: false};
    const pose = b.idle_beats[beat % count], gesture = GESTURES.has(pose);
    if (gesture && b.quiet_cycle && (Math.floor(beat / count) + sim.petId) % b.quiet_cycle === 0) return {pose: 'idle', quiet: true};
    return {pose: L().idle_poses.includes(pose) ? pose : 'idle', quiet: false};
  }
  const ruleHit = b => b.pose_rules.findIndex(r => sim.conds.has(r.when));
  /* The pose, clip and frame the game would show after `elapsed` ms in the simulated state. */
  function simulate(art, elapsed) {
    const p = profileOf(art), b = p && behaviourOf(p.behavior); if (!b) return null;
    const beatMs = L().beat_ms || 900, hit = ruleHit(b);
    let pose, clipStart = 0, info;
    if (hit >= 0) { pose = b.pose_rules[hit].pose; info = `rule ${hit + 1}: ${b.pose_rules[hit].when} → ${pose}`; }
    else {
      const beat = Math.floor(elapsed / beatMs), count = b.idle_beats.length || 1, picked = idlePose(b, beat);
      pose = picked.pose;
      let first = beat;  // a run of equal poses keeps one clip running; each change restarts it
      while (first > 0 && beat - first < 256 && idlePose(b, first - 1).pose === pose) first--;
      clipStart = first * beatMs;
      info = `idle beat ${beat % count + 1}/${count} · cycle ${Math.floor(beat / count) + 1} · ${pose}${picked.quiet ? ' (quiet cycle)' : ''}`;
    }
    const clip = cr.current(`${art}.${pose}`), i = clip ? cr.frameAt(clip, elapsed - clipStart, false) : -1;
    return {pose, info, hit, key: i >= 0 ? clip.frames[i] : null, frame: i, frames: clip?.frames.length || 0};
  }
  cr.panelSource = now => {
    if (!sim.on || !work) return null;
    const r = simulate(state.cForm, simNow(now)); if (!r) return null;
    return {key: r.key, text: `simulating · ${r.info} · ${state.cForm}.${r.pose} frame ${r.frame + 1}/${r.frames}${cr.playing() ? '' : ' · paused'}`};
  };
  cr.step = n => {
    if (!sim.on) return false;
    const now = performance.now(), beatMs = L().beat_ms || 900;
    if (cr.playing()) cr.setPlaying(false);
    sim.pausedAt ??= now;
    const target = Math.max(0, (Math.floor(simNow(now) / beatMs) + n) * beatMs);
    sim.start = sim.pausedAt - target;
    return true;
  };
  cr.tickHooks.push(now => {
    const res = document.getElementById('bh-result');
    const r = work && simulate(state.cForm, simNow(now));
    if (res && r) {
      const text = r.hit >= 0 ? `Rule ${r.hit + 1} matches first: ${r.info.split(': ')[1]} (clip ${state.cForm}.${r.pose}).`
        : `No rule matches, so the idle schedule picks the pose: ${r.info}.`;
      if (res.textContent !== text) res.textContent = text;
      document.querySelectorAll('.bh-rules li').forEach((li, i) => li.classList.toggle('hit', i === r.hit));
    }
    drawTimeline(now);
  });

  /* ---------- idle timeline: rows of beats at 900 ms each, current beat animated ---------- */
  function buildTimeline(el, b) {
    el.textContent = '';
    const rows = b.quiet_cycle ? Math.min(b.quiet_cycle, 4) : 1, count = b.idle_beats.length, size = 52, cells = [];
    for (let r = 0; r < rows; r++) {
      const row = document.createElement('div'); row.className = 'bh-tl-row';
      row.innerHTML = `<span class="cyc"></span>`;
      for (let c = 0; c < count; c++) {
        const cell = document.createElement('div'); cell.className = 'bh-cell';
        const [cv, ctx] = makeCanvas(size, size); cell.append(cv);
        const label = document.createElement('span'); cell.append(label); row.append(cell);
        cells.push({ctx, w: size, h: size, el: cell, label, row: r, col: c, drawn: null});
      }
      el.append(row);
    }
    timeline = {rows, count, cells, el};
  }
  function drawTimeline(now) {
    const p = profileOf(state.cForm), b = p && behaviourOf(p.behavior);
    if (!timeline || !b || !timeline.el.isConnected) return;
    const beatMs = L().beat_ms || 900, elapsed = simNow(now), beat = Math.floor(elapsed / beatMs);
    const cycle = Math.floor(beat / timeline.count), first = Math.floor(cycle / timeline.rows) * timeline.rows;
    const live = simulate(state.cForm, elapsed), idle = live && live.hit < 0;
    timeline.el.querySelectorAll('.cyc').forEach((s, r) => { s.textContent = `cycle ${first + r + 1}`; });
    for (const cell of timeline.cells) {
      const n = (first + cell.row) * timeline.count + cell.col, picked = idlePose(b, n), current = idle && n === beat;
      const clip = cr.current(`${state.cForm}.${picked.pose}`);
      const key = current ? live.key : clip?.frames[0];
      const sig = `${key}|${picked.pose}|${current}|${picked.quiet}`;
      if (cell.drawn === sig) continue;
      cell.drawn = sig;
      const {ctx, w, h} = cell;
      ctx.setTransform(DPR, 0, 0, DPR, 0, 0); ctx.imageSmoothingEnabled = false; ctx.fillStyle = '#000'; ctx.fillRect(0, 0, w, h);
      if (key) cr.drawSprite(ctx, key, 1, w / 2, h - 2);
      cell.el.classList.toggle('now', current); cell.el.classList.toggle('quiet', picked.quiet);
      cell.label.textContent = picked.pose; cell.el.title = `beat ${cell.col + 1}: ${b.idle_beats[cell.col]}${picked.quiet ? ' → idle (quiet cycle)' : ''}`;
    }
  }

  /* ---------- rendering ---------- */
  const options = (list, value) => list.map(v => `<option value="${esc(v)}"${v === value ? ' selected' : ''}>${esc(v)}</option>`).join('');
  const ordinal = n => `${n}${[11, 12, 13].includes(n % 100) ? 'th' : ({1: 'st', 2: 'nd', 3: 'rd'})[n % 10] || 'th'}`;
  const range = (lo, hi) => Array.from({length: hi - lo + 1}, (_, i) => lo + i);
  let host = null;
  cr.renderBehaviour = el => { host = el; render(); };
  function render() {
    if (!host || !host.isConnected) return;
    sync();
    const lim = L(), art = state.cForm;
    if (!work) { host.innerHTML = '<h3>Behaviour &amp; size</h3><p class="studio-note">This checkout has no content/creatures.json, or it is not valid JSON.</p>'; timeline = null; return; }
    const p = profileOf(art), editable = D.creature_data_editable && !stale;
    const head = `<h3>Behaviour &amp; size <span class="lbl">· content/creatures.json${dirty() ? ' · <span class="cr-dirty">unsaved</span>' : ''}</span></h3>
      ${stale ? '<p class="bh-bad">content/creatures.json changed on disk while you were editing. Revert to load it, then reapply your changes.</p>' : ''}
      ${D.creature_data_editable ? '' : '<p class="bh-warn">Read-only here: the checkout has no creature_data.py validator, or this trial runs without --content.</p>'}`;
    if (!p) {
      host.innerHTML = `${head}<p class="studio-note">${esc(art)} has no profile yet.</p><fieldset ${editable ? '' : 'disabled'}><button id="bh-add-profile">Add a profile for ${esc(art)}</button></fieldset>`;
      host.querySelector('#bh-add-profile').onclick = () => edit(w => w.profiles.push({art, behavior: w.behaviors[0]?.name || 'idle', scale: 6, icon_scale: 2, portrait_scale: 4}));
      timeline = null; return;
    }
    const b = behaviourOf(p.behavior), size = sizeReport(art, p), users = usersOf(p.behavior), others = users.filter(u => u !== art);
    const ruleRows = b ? b.pose_rules.map((r, i) => `<li data-i="${i}"><span class="n">${i + 1}</span>
        <select data-f="when" aria-label="Rule ${i + 1} condition" title="${esc(CONDITION_HELP[r.when] || '')}">${options(lim.conditions, r.when)}</select> → <select data-f="pose" aria-label="Rule ${i + 1} pose">${options(cr.poses(), r.pose)}</select>
        <button data-act="up" ${i ? '' : 'disabled'} title="Check earlier">↑</button><button data-act="down" ${i < b.pose_rules.length - 1 ? '' : 'disabled'} title="Check later">↓</button><button data-act="remove" title="Remove rule">✕</button></li>`).join('') : '';
    const beats = b ? b.idle_beats.map((pose, i) => `<select data-beat="${i}" aria-label="Idle beat ${i + 1}">${options(lim.idle_poses, pose)}</select>`).join('') : '';
    const issues = allIssues();
    host.innerHTML = `${head}<fieldset id="bh-fields" ${editable ? '' : 'disabled'}><div class="bh-grid">
      <div><div class="lbl">Size · ${esc(art)}</div>
        <div class="bh-row"><label for="bh-scale">Actor scale</label><select id="bh-scale">${options(range(lim.scale_min, lim.scale_max), p.scale)}</select></div>
        <div class="bh-size ${size.issues.some(m => m.includes('wide') || m.includes('tall')) ? 'bad' : ''}">${esc(size.actor)}</div>
        <div class="bh-row"><label for="bh-icon">Icon scale</label><select id="bh-icon">${options(range(lim.scale_min, lim.scale_max), p.icon_scale)}</select></div>
        <div class="bh-size ${size.issues.some(m => m.includes('icon')) ? 'bad' : ''}">${esc(size.icon)}</div>
        <div class="bh-row"><label for="bh-portrait">Portrait scale</label><select id="bh-portrait">${options(range(lim.scale_min, lim.scale_max), p.portrait_scale)}</select></div>
        <div class="bh-size">${esc(size.portrait)}</div>
        <p class="studio-note">The panel preview above draws at the actor scale.</p></div>
      <div><div class="lbl">Behaviour</div>
        <div class="bh-row"><select id="bh-behaviour" aria-label="Behaviour this form uses">${options(work.behaviors.map(x => x.name), p.behavior)}</select>
          <button id="bh-copy" title="Copy this behaviour under a new name and use it for ${esc(art)}">Copy as new…</button><button id="bh-rename">Rename…</button>
          <button id="bh-delete" ${users.length ? 'disabled title="In use"' : ''}>Delete</button></div>
        ${others.length ? `<p class="bh-warn">Shared with ${others.map(esc).join(', ')}: edits here change ${others.length > 1 ? 'those forms' : 'that form'} too. Use Copy as new to change only ${esc(art)}.</p>` : ''}
        ${b ? `<div class="lbl" style="margin-top:8px">Pose rules · first match wins · ${b.pose_rules.length}/${lim.rule_capacity}</div>
        <ol class="bh-rules" id="bh-rules">${ruleRows}</ol>
        <button id="bh-add-rule" ${b.pose_rules.length >= lim.rule_capacity ? 'disabled' : ''}>Add rule</button>
        <div class="lbl" style="margin-top:12px">Idle schedule · ${b.idle_beats.length}/${lim.beat_capacity} beats · ${lim.beat_ms} ms each · used when no rule matches</div>
        <div class="bh-beats" id="bh-beats">${beats}</div>
        <div class="bh-row"><button id="bh-beat-add" ${b.idle_beats.length >= lim.beat_capacity ? 'disabled' : ''}>Add beat</button><button id="bh-beat-remove" ${b.idle_beats.length <= 1 ? 'disabled' : ''}>Remove last beat</button>
          <label for="bh-quiet">Quiet cycle</label><select id="bh-quiet">${options(range(0, lim.quiet_max), b.quiet_cycle)}</select></div>
        <p class="studio-note">${b.quiet_cycle ? `Every ${b.quiet_cycle === 1 ? '' : ordinal(b.quiet_cycle) + ' '}cycle (offset by pet ID) holds curious and content beats as idle.` : 'Quiet cycle 0: curious and content beats always play.'}</p>` : `<p class="bh-bad">Unknown behaviour ${esc(p.behavior)}.</p>`}
      </div></div></fieldset>
      <div class="lbl" style="margin-top:14px">Simulate</div>
      <div class="bh-row"><button id="bh-sim" aria-pressed="${sim.on}" title="Show what the rules pick in the panel preview above">Simulate on panel</button>
        <div class="bh-cond" id="bh-conds" role="group" aria-label="Conditions that hold">${lim.conditions.map(c => `<button data-cond="${esc(c)}" aria-pressed="${sim.conds.has(c)}" title="${esc(CONDITION_HELP[c] || c)}">${esc(c)}</button>`).join('')}</div>
        <button id="bh-idle" title="No condition holds">idle</button>
        <label for="bh-pet">Pet ID</label><input type="number" id="bh-pet" min="0" max="999" value="${sim.petId}" style="width:70px"></div>
      <p class="studio-note" id="bh-result" aria-live="polite"></p>
      <div class="lbl">Idle timeline · ${lim.beat_ms} ms per beat · dashed beats are quieted</div>
      <div class="bh-timeline" id="bh-timeline"></div>
      <ul class="cr-msgs">${issues.map(m => `<li>${esc(m)}</li>`).join('') || `<li class="ok">${dirty() ? 'Unsaved behaviour or size edits.' : 'No unsaved behaviour or size edits.'}</li>`}</ul>
      <div class="seg"><button id="bh-revert" ${dirty() || stale ? '' : 'disabled'}>Revert</button><button id="bh-save" class="primary" ${dirty() && !issues.length && editable ? '' : 'disabled'} title="Save behaviour and size (⌘S)">${dirty() ? 'Save behaviour & size' : 'Saved'}</button></div>`;
    wire(p, b);
    if (b) buildTimeline(host.querySelector('#bh-timeline'), b); else timeline = null;
    cr.redraw();
  }
  function wire(p, b) {
    const $ = s => host.querySelector(s);
    $('#bh-scale').onchange = e => edit(() => { p.scale = Number(e.target.value); });
    $('#bh-icon').onchange = e => edit(() => { p.icon_scale = Number(e.target.value); });
    $('#bh-portrait').onchange = e => edit(() => { p.portrait_scale = Number(e.target.value); });
    $('#bh-behaviour').onchange = e => edit(() => { p.behavior = e.target.value; });
    $('#bh-copy').onclick = () => {
      const name = prompt('Name for the copied behaviour', `${p.art}`)?.trim(); if (!name) return;
      if (behaviourOf(name)) return S.status(`A behaviour named ${name} already exists`, 'warn');
      edit(w => { w.behaviors.push({...clone(b || {pose_rules: [], idle_beats: ['idle'], quiet_cycle: 0}), name}); p.behavior = name; });
    };
    $('#bh-rename').onclick = () => {
      const name = prompt(`Rename ${p.behavior} (used by ${usersOf(p.behavior).join(', ')})`, p.behavior)?.trim();
      if (!name || name === p.behavior || !b) return;
      if (behaviourOf(name)) return S.status(`A behaviour named ${name} already exists`, 'warn');
      edit(w => { const old = b.name; b.name = name; w.profiles.forEach(x => { if (x.behavior === old) x.behavior = name; }); });
    };
    $('#bh-delete').onclick = () => edit(w => { w.behaviors = w.behaviors.filter(x => x !== b); });
    if (b) {
      $('#bh-rules').onchange = e => { const li = e.target.closest('li'), f = e.target.dataset.f; if (li && f) edit(() => { b.pose_rules[Number(li.dataset.i)][f] = e.target.value; }); };
      $('#bh-rules').onclick = e => {
        const act = e.target.closest('button')?.dataset.act, i = Number(e.target.closest('li')?.dataset.i); if (!act) return;
        edit(() => {
          const r = b.pose_rules, j = act === 'up' ? i - 1 : i + 1;
          if (act === 'remove') r.splice(i, 1); else [r[i], r[j]] = [r[j], r[i]];
        });
      };
      $('#bh-add-rule').onclick = () => edit(() => {
        const used = new Set(b.pose_rules.map(r => r.when)), when = L().conditions.find(c => !used.has(c)) || L().conditions[0];
        b.pose_rules.push({when, pose: 'happy'});
      });
      $('#bh-beats').onchange = e => edit(() => { b.idle_beats[Number(e.target.dataset.beat)] = e.target.value; });
      $('#bh-beat-add').onclick = () => edit(() => b.idle_beats.push('idle'));
      $('#bh-beat-remove').onclick = () => edit(() => b.idle_beats.pop());
      $('#bh-quiet').onchange = e => edit(() => { b.quiet_cycle = Number(e.target.value); });
    }
    $('#bh-sim').onclick = () => { sim.on = !sim.on; store.set('creature-simulate', sim.on); restartSim(); render(); };
    $('#bh-conds').onclick = e => {
      const c = e.target.closest('button')?.dataset.cond; if (!c) return;
      sim.conds.has(c) ? sim.conds.delete(c) : sim.conds.add(c);
      restartSim(); render();
    };
    $('#bh-idle').onclick = () => { sim.conds.clear(); restartSim(); render(); };
    $('#bh-pet').oninput = e => { const v = Number(e.target.value); if (Number.isInteger(v) && v >= 0) { sim.petId = v; timeline?.cells.forEach(c => { c.drawn = null; }); cr.redraw(); } };
    $('#bh-revert').onclick = () => { work = D.creature_data ? clone(D.creature_data) : null; base = D.creature_data_sha; stale = false; render(); cr.renderForms(); cr.redraw(); };
    $('#bh-save').onclick = save;
  }

  /* ---------- save ---------- */
  async function save() {
    if (!dirty()) return;
    const issues = allIssues();
    if (issues.length) return S.status(`Fix the behaviour and size messages before saving: ${issues[0]}`, 'bad', true);
    if (stale) return S.status('content/creatures.json changed on disk; revert first', 'bad', true);
    try {
      const sent = clone(work), res = await S.api('POST', '/api/creatures', {data: sent, base, artist: state.artist});
      D.creature_data = sent; D.creature_data_sha = seen = base = res.sha; D.version = res.version;
      render(); cr.renderForms();
      if (res.git_error) S.status(`Saved creature data, but the commit failed: ${res.git_error}`, 'bad', true);
      else S.status(res.commit ? `Saved content/creatures.json (commit ${res.commit}).` : 'Saved content/creatures.json.');
    } catch (err) { S.status(`Behaviour save failed: ${err.message}`, 'bad', true); }
  }
  cr.save = () => { if (dirty()) save(); };
})();

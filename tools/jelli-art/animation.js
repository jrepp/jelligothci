/* Jelli Art: the animation timeline for a creature clip, loaded after creature.js.
 * A frame strip with thumbnails (drag or Alt+arrow to reorder), per-frame durations (number
 * field or a draggable/arrow-key handle on the duration track), a playhead, new frames (blank
 * or duplicated, appended with the next free ID through POST /api/frames), retiring unused
 * frames, and onion skinning (previous/next clip frame ghosted) while painting a frame.
 * Playback timing lives in creature.js and follows clipFrame below, which mirrors
 * core/creature.c jelli_clip_frame: loops wrap; a one-shot holds its last frame. */
'use strict';
(() => {
  function clipFrame(clip, elapsed) {
    const count = Math.min(clip.frames.length, 6);
    if (!count) return 0;
    const d = clip.durations_ms.slice(0, count), total = d.reduce((n, v) => n + v, 0);
    if (!total) return 0;
    if (clip.loop) elapsed %= total;
    else if (elapsed >= total) return count - 1;
    for (let i = 0; i < count; i++) { if (elapsed < d[i]) return i; elapsed -= d[i]; }
    return count - 1;
  }
  const A = window.JelliAnimation = {clipFrame};
  const S = window.Studio, cr = S?.cr;
  if (!cr) return;
  const GHOST = {prev: [250, 140, 153], next: [133, 228, 182]};
  Object.assign(state, {onionSkin: store.get('onion-skin', true), onionAlpha: store.get('onion-alpha', 0.35)});
  const anim = {overview: null, focus: null, drag: null, paintClip: null};
  const esc = cr.esc;

  document.head.insertAdjacentHTML('beforeend', `<style>
    .an-strip{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0;padding:0;list-style:none}
    .an-card{background:var(--raised);border:2px solid var(--line);border-radius:8px;padding:6px;display:grid;gap:4px;justify-items:center;width:98px;font:10px ui-monospace,monospace;color:var(--muted);cursor:grab}
    .an-card.now{border-color:var(--accent)}.an-card.drop-before{box-shadow:-5px 0 0 -1px var(--warn)}.an-card.drop-after{box-shadow:5px 0 0 -1px var(--warn)}
    .an-card.dragging{opacity:.45}
    .an-card img,.an-lib img{image-rendering:pixelated;width:56px;height:56px;background:#000;border-radius:4px;object-fit:contain}
    .an-card .thumb-btn{padding:0;border:0;background:none;border-radius:4px}
    .an-card input{width:72px;padding:3px 5px}.an-card .row{display:flex;gap:3px;flex-wrap:wrap;justify-content:center}.an-card .row button{padding:1px 5px;min-width:24px;min-height:24px}
    .an-card .name{max-width:86px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
    .an-track{position:relative;display:flex;height:34px;margin:4px 0 10px;border:1px solid var(--line);border-radius:6px;background:var(--panel);overflow:hidden;max-width:760px}
    .an-seg{position:relative;flex:1 1 0;min-width:44px;border-right:1px solid var(--line);display:flex}
    .an-seg button.body{flex:1;border:0;border-radius:0;background:transparent;font:10px ui-monospace,monospace;color:var(--muted);padding:0 22px 0 4px;text-align:left;white-space:nowrap;overflow:hidden}
    .an-seg.now button.body{background:#85e4b622;color:var(--ink)}
    .an-handle{position:absolute;right:-1px;top:0;bottom:0;width:24px;cursor:ew-resize;background:linear-gradient(90deg,transparent 14px,var(--muted) 14px 16px,transparent 16px 18px,var(--muted) 18px 20px,transparent 20px);opacity:.7;touch-action:none}
    .an-handle:hover,.an-handle:focus-visible{opacity:1;background-color:#ffffff14}
    .an-handle:focus-visible,.an-card:focus-visible,[role=slider]:focus-visible{outline:2px solid var(--accent);outline-offset:-2px}
    .an-playhead{position:absolute;top:0;bottom:0;width:2px;background:var(--warn);pointer-events:none;left:0}
    .an-new{display:flex;flex-wrap:wrap;gap:8px 12px;align-items:end;margin:6px 0 4px}
    .an-new input[type=text]{width:180px;background:var(--bg);border:1px solid var(--line);color:var(--ink);border-radius:6px;padding:4px 8px;font:inherit}
    .an-libs{display:flex;flex-wrap:wrap;gap:6px}
    .an-lib{display:grid;justify-items:center;gap:3px;padding:6px;background:var(--panel);border:1px solid var(--line);border-radius:8px;font:10px ui-monospace,monospace;color:var(--muted);width:118px}
    .an-lib img{width:48px;height:48px}.an-lib .name{max-width:106px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
    .an-lib .row{display:flex;gap:3px;flex-wrap:wrap;justify-content:center}.an-lib .row button{padding:1px 6px;min-width:24px;min-height:24px}
    .an-lib.unused{border-style:dashed}
    .an-sr{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}
    .an-onion{display:flex;gap:8px;align-items:end;flex-wrap:wrap}
    @media (prefers-reduced-motion: reduce){.an-card,.an-handle{transition:none}}
  </style>`);
  document.body.insertAdjacentHTML('beforeend', '<div id="an-live" class="an-sr" role="status" aria-live="polite"></div>');
  const shell = () => window.JelliShell;
  const announce = text => {
    if (shell()?.announce) return shell().announce(text);
    const el = document.getElementById('an-live'); el.textContent = ''; setTimeout(() => { el.textContent = text; }, 30);
  };
  /* Status for a result: the page shell's toast (which also announces) when loaded, else the studio status line. */
  const tell = (text, tone = '', sticky = false) => {
    if (shell()?.notify) return shell().notify(text, {tone: tone || 'info', sticky});
    S.status(text, tone, sticky); announce(text);
  };
  const ask = (message, opts) => shell()?.confirm ? shell().confirm(message, opts) : Promise.resolve(window.confirm(message));

  /* ---------- clip helpers ---------- */
  const clipKeys = () => [...new Set([...(D.clips || []).map(c => c.key), ...cr.poses().map(p => `${state.cForm}.${p}`)])];
  /* Clips (saved or unsaved) that use a frame, by key. */
  const usersOf = key => clipKeys().filter(k => cr.current(k)?.frames.includes(key));
  const otherUsers = key => (anim.overview?.[byKey[key]?.form]?.frames?.[key] || []).filter(u => !u.startsWith('clip '));
  async function loadOverview() {
    try { anim.overview = (await S.api('GET', '/api/frames')).frames; } catch { anim.overview = null; }
  }
  function move(from, to) {
    const c = cr.current(cr.selectedKey()); if (!c || from === to || to < 0 || to >= c.frames.length) return;
    const k = c.frames[from];
    cr.change(w => {
      const [f] = w.frames.splice(from, 1), [d] = w.durations_ms.splice(from, 1);
      w.frames.splice(to, 0, f); w.durations_ms.splice(to, 0, d);
    });
    announce(`Moved ${byKey[k]?.pose || k} to position ${to + 1} of ${c.frames.length}.`);
  }
  function setDuration(i, ms, rerender) {
    const value = Math.max(1, Math.min(cr.maxMs(), Math.round(ms)));
    cr.change(w => { w.durations_ms[i] = value; }, rerender);
    return value;
  }
  const currentIndex = () => {
    const c = cr.current(cr.selectedKey()), now = document.querySelector('.an-card.now');
    const i = cr.pausedFrame() ?? (now ? Number(now.dataset.index) : 0);
    return c ? Math.max(0, Math.min(i, c.frames.length - 1)) : 0;
  };

  /* ---------- timeline ---------- */
  cr.renderTimeline = (el, lib, key, c) => {
    const total = cr.totalOf(c), mine = cr.clipDirty(key);
    el.innerHTML = `<div class="lbl" style="margin-top:12px" id="an-strip-label">Timeline · ${c.frames.length}/${cr.cap()} frames · ${total} ms ${c.loop ? 'per loop' : 'once'}</div>
      <ol class="an-strip" id="an-strip" aria-labelledby="an-strip-label" aria-describedby="an-strip-help"></ol>
      <p class="an-sr" id="an-strip-help">Drag a frame, or focus it and press Alt+Left or Alt+Right, to reorder.</p>
      <div class="lbl">Durations · drag a handle, or focus it and use the arrow keys (Shift: 100 ms)</div>
      <div class="an-track" id="an-track" role="group" aria-label="Frame durations"></div>
      <div class="an-new" role="group" aria-label="New frame">
        <label class="lbl" style="text-transform:none">New frame name<br><input type="text" id="an-name" placeholder="automatic" autocomplete="off" spellcheck="false" aria-describedby="an-new-help"></label>
        <label class="lbl" style="text-transform:none"><input type="checkbox" id="an-insert" checked> insert after the current frame</label>
        <div class="seg"><button id="an-dup" title="Copy the current frame to a new PNG and asset">Duplicate current frame</button><button id="an-blank" title="A new frame with one ink pixel at the pivot, ready to paint">Add blank frame</button></div>
      </div>
      <p class="studio-note" id="an-new-help"></p>`;
    renderCards(el.querySelector('#an-strip'), key, c);
    renderTrack(el.querySelector('#an-track'), c);
    renderNewHelp(key, mine);
    el.querySelector('#an-dup').onclick = () => addFrame(true);
    el.querySelector('#an-blank').onclick = () => addFrame(false);
    renderLibrary(lib, key, c);
    if (!anim.overview) loadOverview().then(() => { if (document.getElementById('an-new-help')) { renderNewHelp(key, mine); renderLibrary(document.getElementById('cr-library'), key, cr.current(key)); } });
    restoreFocus();
  };
  function renderNewHelp(key, dirty) {
    const el = document.getElementById('an-new-help'); if (!el) return;
    const info = anim.overview?.[state.cForm];
    const id = info?.next_id ? `It gets ID ${info.next_id}` : 'It gets the next free ID';
    el.textContent = `${id} in the ${state.cForm} range${info?.import_spec ? ` (appended to ${info.import_spec})` : ''}, with the form's size, pivot and palette. `
      + (dirty ? 'This clip has unsaved edits, so the frame is inserted into your working copy; save clips to keep it.' : 'Inserting saves this clip too, in the same commit.');
  }
  function renderCards(list, key, c) {
    c.frames.forEach((k, i) => {
      const a = byKey[k], li = document.createElement('li'), name = a?.pose || k;
      li.className = 'an-card cr-frame'; li.dataset.index = i; li.draggable = true;
      li.setAttribute('aria-label', `Frame ${i + 1} of ${c.frames.length}: ${name}, ${c.durations_ms[i]} ms`);
      li.innerHTML = `<button class="thumb-btn" data-act="show" aria-label="Show frame ${i + 1} (${esc(name)}) on the panel"><img alt="" src="${a?.after || ''}"></button>
        <span class="name" title="${esc(k)}">${i + 1}. ${esc(name)}</span>
        <label class="lbl" style="text-transform:none">ms <input type="number" min="1" max="${cr.maxMs()}" step="10" value="${Number.isFinite(c.durations_ms[i]) ? c.durations_ms[i] : ''}" aria-label="Frame ${i + 1} duration in milliseconds"></label>
        <div class="row"><button data-act="left" aria-label="Move frame ${i + 1} earlier" title="Move earlier (Alt+←)" ${i ? '' : 'disabled'}>←</button><button data-act="right" aria-label="Move frame ${i + 1} later" title="Move later (Alt+→)" ${i < c.frames.length - 1 ? '' : 'disabled'}>→</button><button data-act="dup" aria-label="Duplicate frame ${i + 1} as a new frame" title="Duplicate as a new frame">⧉</button><button data-act="remove" aria-label="Remove frame ${i + 1} from this clip" title="Remove from this clip (the frame stays in the library)">✕</button><button data-act="paint" aria-label="Paint frame ${i + 1} with onion skin" title="Paint with onion skin">✎</button></div>`;
      const input = li.querySelector('input');
      input.oninput = () => { cr.change(w => { w.durations_ms[i] = input.value === '' ? NaN : Number(input.value); }, false); updateTrack(); };
      input.onchange = () => { anim.focus = {index: i, sel: 'input'}; cr.renderEditor(); };
      li.querySelector('.row').onclick = e => act(e.target.closest('button')?.dataset.act, i, k);
      li.querySelector('.thumb-btn').onclick = () => cr.seek(i);
      li.onkeydown = e => {
        if (!e.altKey || !['ArrowLeft', 'ArrowRight'].includes(e.key)) return;
        e.preventDefault(); e.stopPropagation();
        const to = i + (e.key === 'ArrowLeft' ? -1 : 1);
        if (to < 0 || to >= c.frames.length) return;
        anim.focus = {index: to, sel: e.target.dataset?.act ? `[data-act="${e.target.dataset.act}"]` : '.thumb-btn'}; move(i, to);
      };
      wireDrag(li, i);
      list.append(li);
    });
  }
  function act(what, i, k) {
    if (!what) return;
    if (what === 'paint') { anim.paintClip = {key: cr.selectedKey(), index: i}; return cr.paint(k); }
    if (what === 'dup') return addFrame(true, i);
    if (what === 'left' || what === 'right') { const to = what === 'left' ? i - 1 : i + 1; anim.focus = {index: to, sel: `[data-act="${what}"]`}; return move(i, to); }
    if (what === 'remove') {
      anim.focus = {index: Math.max(0, i - 1), sel: '.thumb-btn'};
      cr.change(w => { w.frames.splice(i, 1); w.durations_ms.splice(i, 1); });
      announce(`Removed frame ${i + 1} from the clip. The frame stays in the library.`);
    }
  }
  function wireDrag(li, i) {
    li.ondragstart = e => { anim.drag = i; li.classList.add('dragging'); e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', String(i)); };
    li.ondragend = () => { anim.drag = null; document.querySelectorAll('.an-card').forEach(x => x.classList.remove('dragging', 'drop-before', 'drop-after')); };
    li.ondragover = e => {
      if (anim.drag === null) return;
      e.preventDefault(); const after = e.offsetX > li.offsetWidth / 2;
      li.classList.toggle('drop-before', !after); li.classList.toggle('drop-after', after);
    };
    li.ondragleave = () => li.classList.remove('drop-before', 'drop-after');
    li.ondrop = e => {
      e.preventDefault(); if (anim.drag === null) return;
      const from = anim.drag, after = e.offsetX > li.offsetWidth / 2;
      let to = i + (after ? 1 : 0); if (from < to) to--;
      anim.drag = null; anim.focus = {index: to, sel: '.thumb-btn'}; move(from, to);
    };
  }
  function restoreFocus() {
    const f = anim.focus; anim.focus = null; if (!f) return;
    const card = document.querySelector(`.an-card[data-index="${f.index}"]`);
    const target = card?.querySelector(f.sel); (target && !target.disabled ? target : card?.querySelector('.thumb-btn'))?.focus();
    if (f.handle !== undefined) document.querySelector(`.an-handle[data-index="${f.handle}"]`)?.focus();
  }

  /* ---------- duration track ---------- */
  function renderTrack(track, c) {
    track.innerHTML = c.frames.map((k, i) => `<div class="an-seg" data-index="${i}" style="flex-grow:${cr.safeDurations(c)[i]}">
      <button class="body" tabindex="-1" aria-hidden="true">${i + 1} · <span class="ms">${c.durations_ms[i]}</span></button>
      <div class="an-handle" data-index="${i}" role="slider" tabindex="0" aria-label="Frame ${i + 1} duration" aria-valuemin="1" aria-valuemax="${cr.maxMs()}" aria-valuenow="${c.durations_ms[i]}" aria-valuetext="${c.durations_ms[i]} milliseconds"></div></div>`).join('')
      + '<div class="an-playhead" id="an-playhead" aria-hidden="true"></div>';
    track.querySelectorAll('.an-seg button.body').forEach((b, i) => { b.onclick = () => cr.seek(i); });
    track.querySelectorAll('.an-handle').forEach(h => wireHandle(h, Number(h.dataset.index)));
  }
  function updateTrack() {
    const c = cr.current(cr.selectedKey()), track = document.getElementById('an-track'); if (!c || !track) return;
    track.querySelectorAll('.an-seg').forEach((seg, i) => {
      seg.style.flexGrow = cr.safeDurations(c)[i]; seg.querySelector('.ms').textContent = c.durations_ms[i];
      const h = seg.querySelector('.an-handle'); h.setAttribute('aria-valuenow', c.durations_ms[i]); h.setAttribute('aria-valuetext', `${c.durations_ms[i]} milliseconds`);
    });
    document.querySelectorAll('.an-card input').forEach((input, i) => { if (document.activeElement !== input) input.value = c.durations_ms[i]; });
  }
  function wireHandle(h, i) {
    h.onkeydown = e => {
      const c = cr.current(cr.selectedKey()), now = c.durations_ms[i] || 1, big = e.shiftKey ? 100 : 10;
      const next = {ArrowRight: now + big, ArrowUp: now + big, ArrowLeft: now - big, ArrowDown: now - big,
        PageUp: now + 100, PageDown: now - 100, Home: 1, End: cr.maxMs()}[e.key];
      if (next === undefined) return;
      e.preventDefault(); e.stopPropagation();
      const value = setDuration(i, next, false); updateTrack(); announce(`${value} ms`);
    };
    h.onkeyup = e => { if (e.key.startsWith('Arrow') || e.key.startsWith('Page') || e.key === 'Home' || e.key === 'End') { anim.focus = {handle: i}; cr.renderEditor(); } };
    h.onpointerdown = e => {
      e.preventDefault(); h.setPointerCapture(e.pointerId); h.focus();
      const c = cr.current(cr.selectedKey()), track = document.getElementById('an-track');
      const start = {x: e.clientX, ms: c.durations_ms[i] || 1, perMs: track.clientWidth / cr.totalOf(c)};
      h.onpointermove = m => {
        const step = m.shiftKey ? 1 : 10, raw = start.ms + (m.clientX - start.x) / start.perMs;
        setDuration(i, Math.round(raw / step) * step || 1, false); updateTrack();
      };
      h.onpointerup = h.onpointercancel = () => { h.onpointermove = null; anim.focus = {handle: i}; cr.renderEditor(); };
    };
  }
  cr.onFrame = (index, t, c) => {
    const track = document.getElementById('an-track'), head = document.getElementById('an-playhead'); if (!track || !head) return;
    const segs = track.querySelectorAll('.an-seg'), seg = segs[index]; if (!seg) return;
    const d = cr.safeDurations(c), frac = Math.min(1, Math.max(0, (t - cr.offsetOf(c, index)) / d[index]));
    head.style.left = `${seg.offsetLeft + frac * seg.offsetWidth}px`;
    segs.forEach((s, i) => s.classList.toggle('now', i === index));
  };

  /* ---------- frame library: add to clip, duplicate, retire ---------- */
  function renderLibrary(el, key, c) {
    if (!el || !c) return;
    const frames = cr.formFrames(state.cForm);
    const retired = anim.overview?.[state.cForm]?.retired || [];
    el.innerHTML = `<div class="lbl" style="margin-top:12px">Frame library · ${esc(state.cForm)} · ${frames.length} frames${retired.length ? ` · ${retired.length} retired` : ''}</div><div class="an-libs" role="list"></div>`;
    const list = el.querySelector('.an-libs');
    for (const a of frames) {
      const users = usersOf(a.key), other = otherUsers(a.key), used = users.length + other.length;
      const card = document.createElement('div'); card.className = `an-lib${used ? '' : ' unused'}`; card.setAttribute('role', 'listitem');
      card.innerHTML = `<img alt="" src="${a.after}"><span class="name" title="${esc(a.key)}">${esc(a.pose || a.key)}</span><span>ID ${a.id} · ${used ? `${used} use${used > 1 ? 's' : ''}` : 'unused'}</span>
        <div class="row"><button data-act="add" aria-label="Append ${esc(a.pose || a.key)} to this clip" title="Append to this clip">+ clip</button><button data-act="dup" aria-label="Duplicate ${esc(a.pose || a.key)} as a new frame" title="Duplicate as a new frame">⧉</button><button data-act="retire" aria-label="Retire ${esc(a.pose || a.key)}" title="${used ? `Used by ${esc([...users, ...other].join(', '))}` : 'Retire this unused frame; its ID stays reserved'}">Retire</button></div>`;
      card.querySelector('.row').onclick = e => {
        const what = e.target.closest('button')?.dataset.act;
        if (what === 'add') {
          if (cr.current(key).frames.length >= cr.cap()) return tell(`A clip holds at most ${cr.cap()} frames`, 'warn');
          const last = cr.current(key).durations_ms.at(-1);
          cr.change(w => { w.frames.push(a.key); w.durations_ms.push(Number.isInteger(last) ? last : 450); });
          announce(`Appended ${a.pose || a.key} to ${key}.`);
        } else if (what === 'dup') addFrame(true, null, a.key);
        else if (what === 'retire') retire(a);
      };
      list.append(card);
    }
  }
  async function addFrame(duplicate, index = null, from = null) {
    const key = cr.selectedKey(), c = cr.current(key); if (!c) return;
    const at = index ?? currentIndex();
    const source = duplicate ? from || c.frames[at] : null;
    if (source && S.paintDirty(source)) return tell(`Save or revert your paint edits to ${source} before duplicating it`, 'warn', true);
    const insert = document.getElementById('an-insert')?.checked !== false;
    if (insert && c.frames.length >= cr.cap()) return tell(`${key} already has ${cr.cap()} frames, the runtime cap; untick insert or remove a frame first`, 'warn', true);
    const dirty = cr.clipDirty(key), pos = Math.min(at + 1, c.frames.length), name = document.getElementById('an-name')?.value.trim();
    const body = {action: 'add', form: state.cForm, artist: state.artist, ...(source ? {from: source} : {}), ...(name ? {pose: name} : {})};
    if (insert && !dirty) Object.assign(body, {clip: key, index: pos, duration_ms: Number.isInteger(c.durations_ms[at]) ? c.durations_ms[at] : 450});
    try {
      const res = await S.api('POST', '/api/frames', body);
      D.version = res.version; await S.reload(); await loadOverview();
      if (insert && dirty) cr.change(w => { w.frames.splice(pos, 0, res.key); w.durations_ms.splice(pos, 0, Number.isInteger(w.durations_ms[at]) ? w.durations_ms[at] : 450); }, false);
      if (insert) { anim.focus = {index: pos, sel: '.thumb-btn'}; cr.seek(pos); }
      cr.rerender();
      const how = `${source ? `Duplicated ${source} as` : 'Added blank frame'} ${res.key} (ID ${res.id})`;
      const where = insert ? (dirty ? ` and inserted it into your unsaved ${key}` : ` in ${key}`) : '';
      if (res.git_error) tell(`${how}, but the commit failed: ${res.git_error}`, 'bad', true);
      else tell(`${how}${where}${res.commit ? ` (commit ${res.commit})` : ''}.${res.warning ? ` ${res.warning}` : ''}`, res.warning ? 'warn' : 'ok', !!res.warning);
    } catch (err) { tell(`New frame failed: ${err.message}`, 'bad', true); }
  }
  async function retire(a) {
    const users = [...usersOf(a.key), ...otherUsers(a.key)];
    if (users.length) {
      const msg = `Cannot retire ${a.key}: it is used by ${users.join(', ')}. Remove it from ${users.length > 1 ? 'those' : 'that'} first${usersOf(a.key).some(k => cr.clipDirty(k)) ? ' (including unsaved clip edits)' : ''}.`;
      tell(msg, 'bad', true); return;
    }
    const saved = (D.clips || []).filter(c => c.frames.includes(a.key)).map(c => c.key);
    if (saved.length) {
      const msg = `Save your clip edits first: the saved ${saved.join(', ')} still ${saved.length > 1 ? 'use' : 'uses'} ${a.key}.`;
      tell(msg, 'warn', true); return;
    }
    if (S.paintDirty(a.key)) return tell(`${a.key} has unsaved paint edits; save or revert them first`, 'warn', true);
    const sure = await ask(`Retire ${a.key}?\n\nIts PNG and manifest entry are removed. ID ${a.id} and the name stay reserved in source/studio-frames.json, so they are never reused. Git can bring it back.`,
      {title: 'Retire this frame?', confirmLabel: 'Retire', danger: true});
    if (!sure) return;
    try {
      const res = await S.api('POST', '/api/frames', {action: 'retire', key: a.key, artist: state.artist});
      D.version = res.version; await S.reload(); await loadOverview(); cr.rerender();
      const text = `Retired ${a.key}; ID ${res.id} stays reserved${res.commit ? ` (commit ${res.commit})` : ''}.`;
      res.git_error ? tell(`${text} The commit failed: ${res.git_error}`, 'bad', true) : tell(text, 'ok');
    } catch (err) { tell(`Retire failed: ${err.message}`, 'bad', true); }
  }

  /* ---------- onion skin while painting a clip frame ---------- */
  /* The clip a painted frame belongs to: the one it was opened from, the selected pose's, or any that uses it. */
  function paintContext(key) {
    const p = anim.paintClip, at = p && cr.current(p.key);
    if (at && at.frames[p.index] === key) return {clip: p.key, c: at, index: p.index};
    const keys = [`${byKey[key]?.form}.${state.cPose}`, ...clipKeys()];
    for (const k of keys) { const c = cr.current(k), i = c?.frames.indexOf(key); if (c && i >= 0) return {clip: k, c, index: i}; }
    return null;
  }
  function neighbours(ctx) {
    const {c, index} = ctx, n = c.frames.length, wrap = c.loop;
    const prev = index > 0 ? index - 1 : wrap && n > 1 ? n - 1 : null, next = index < n - 1 ? index + 1 : wrap && n > 1 ? 0 : null;
    return {prev, next};
  }
  S.onion = (g, a, z) => {
    if (!state.onionSkin || a.kind !== 'creatures' || state.mode !== 'paint') return;
    const ctx = paintContext(a.key); if (!ctx) return;
    const {prev, next} = neighbours(ctx);
    for (const [which, i] of [['prev', prev], ['next', next]]) {
      if (i === null) continue;
      const k = ctx.c.frames[i], p = decoded[k]?.after, other = byKey[k]; if (!p || k === a.key) continue;
      const dx = (a.pivot?.[0] ?? 0) - (other.pivot?.[0] ?? 0), dy = (a.pivot?.[1] ?? 0) - (other.pivot?.[1] ?? 0);
      const [r, gr, b] = GHOST[which], here = decoded[a.key]?.after;
      g.fillStyle = `rgba(${r},${gr},${b},${state.onionAlpha})`;
      /* Ghost only where the neighbour differs from this frame, so unchanged pixels stay readable. */
      for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) {
        const i = (y * p.w + x) * 4; if (!p.data[i + 3]) continue;
        const seen = pixel(here, x + dx, y + dy);
        if (seen && seen === hexOf(p.data, i)) continue;
        g.fillRect((x + dx) * z, (y + dy) * z, z, z);
      }
    }
  };
  function paintFrame(step) {
    const a = asset(), ctx = a && paintContext(a.key); if (!ctx) return;
    const n = ctx.c.frames.length, i = (ctx.index + step + n) % n;
    anim.paintClip = {key: ctx.clip, index: i}; select(ctx.c.frames[i]);
    announce(`Painting frame ${i + 1} of ${n} in ${ctx.clip}: ${ctx.c.frames[i]}`);
  }
  function backToTimeline() {
    const a = asset(), ctx = a && paintContext(a.key);
    if (ctx) { const [form, pose] = ctx.clip.split('.'); state.cForm = form; state.cPose = pose; store.set('creature-form', form); store.set('creature-pose', pose); }
    state.view = 'creature'; renderView(); savePrefs();
    if (ctx) { cr.seek(ctx.index); anim.focus = {index: ctx.index, sel: '[data-act="paint"]'}; cr.renderEditor(); }
  }
  const baseToolbar = S.renderToolbar;
  S.renderToolbar = extra => {
    baseToolbar(extra);
    const a = asset(); if (!a || a.kind !== 'creatures') return;
    const ctx = paintContext(a.key), {prev, next} = ctx ? neighbours(ctx) : {};
    const where = ctx ? `frame ${ctx.index + 1}/${ctx.c.frames.length} of ${ctx.clip}` : 'not in any clip';
    const box = document.createElement('div'); box.className = 'an-onion';
    box.innerHTML = `<div><div class="lbl">Animation · ${esc(where)}</div><div class="seg">
        <button id="an-onion" aria-pressed="${state.onionSkin}" title="Ghost where the previous (rose) and next (mint) clip frames differ from this one (O)" ${ctx ? '' : 'disabled'}>Onion skin</button>
        <button id="an-prev" title="Paint the previous clip frame (,)" ${ctx && ctx.c.frames.length > 1 ? '' : 'disabled'}>◀ Frame</button><button id="an-next" title="Paint the next clip frame (.)" ${ctx && ctx.c.frames.length > 1 ? '' : 'disabled'}>Frame ▶</button>
        <button id="an-back" title="Back to the clip timeline">Timeline</button></div></div>
      <label class="lbl">Ghost opacity <input type="range" id="an-alpha" min="0.1" max="0.8" step="0.05" value="${state.onionAlpha}" aria-label="Onion skin opacity" ${ctx ? '' : 'disabled'}></label>
      ${ctx && state.onionSkin ? `<span class="studio-note" style="margin:0">${prev !== null ? `<span style="color:rgb(${GHOST.prev})">◼</span> ${esc(byKey[ctx.c.frames[prev]]?.pose || '')}` : ''} ${next !== null ? `<span style="color:rgb(${GHOST.next})">◼</span> ${esc(byKey[ctx.c.frames[next]]?.pose || '')}` : ''}</span>` : ''}`;
    extra.firstElementChild?.append(box);
    box.querySelector('#an-onion').onclick = () => toggleOnion();
    box.querySelector('#an-prev').onclick = () => paintFrame(-1);
    box.querySelector('#an-next').onclick = () => paintFrame(1);
    box.querySelector('#an-back').onclick = backToTimeline;
    box.querySelector('#an-alpha').oninput = e => { state.onionAlpha = Number(e.target.value); store.set('onion-alpha', state.onionAlpha); renderStage(); };
  };
  function toggleOnion() {
    state.onionSkin = !state.onionSkin; store.set('onion-skin', state.onionSkin);
    renderLegend(); renderStage(); document.getElementById('an-onion')?.focus();
  }
  const baseKeydown = S.keydown;
  S.keydown = e => {
    const a = asset();
    if (state.view === 'detail' && state.mode === 'paint' && a?.kind === 'creatures' && !(e.metaKey || e.ctrlKey || e.altKey)) {
      const k = e.key.toLowerCase();
      if (k === 'o') { toggleOnion(); return true; }
      if (k === ',' || k === '.') { e.preventDefault(); paintFrame(k === ',' ? -1 : 1); return true; }
    }
    return baseKeydown(e);
  };
  /* The page shell's ? overlay (shell.js), when it is loaded. */
  shell()?.registerShortcuts?.('Creature', [{keys: ['Space'], description: 'Play or pause'}, {keys: [',', 'ArrowLeft'], description: 'Previous frame'},
    {keys: ['.', 'ArrowRight'], description: 'Next frame'}, {keys: ['J', 'ArrowDown'], description: 'Next pose'}, {keys: ['K', 'ArrowUp'], description: 'Previous pose'},
    {keys: ['1 … 9'], description: 'Choose a pose'}, {keys: ['Alt+ArrowLeft', 'Alt+ArrowRight'], description: 'Move the focused timeline frame'},
    {keys: ['ArrowLeft', 'ArrowRight'], description: 'On a duration handle: 10 ms shorter or longer'},
    {keys: ['Shift+ArrowLeft', 'Shift+ArrowRight', 'PageDown', 'PageUp'], description: 'On a duration handle: 100 ms'},
    {keys: ['Mod+S'], description: 'Save clips and behaviour & size'}]);
  shell()?.registerShortcuts?.('Paint · clip frame', [{keys: ['O'], description: 'Onion skin on or off'},
    {keys: [',', '.'], description: 'Paint the previous or next frame of the clip'}]);
  Object.assign(A, {paintContext, loadOverview});
})();

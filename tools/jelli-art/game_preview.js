/* Jelli Art: the Test in game view. Sends a scenario to /api/game-preview, which builds
 * tools/game-preview/preview.c against the real engine from the served art and content
 * and returns PNG frames rendered by jelli_pet_render. Nothing here imitates the engine:
 * the page only picks the scenario and shows frames, the engine's report and the
 * generators' errors. Loaded last; follows the view pattern of reactions.js. */
'use strict';
(() => {
  const S = window.Studio;
  if (!S) return;
  const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'})[c]);
  const {help} = /** @type {any} */ (window).JelliDesign;
  const GAME_HELP = `Render in engine builds the real C engine from the working copy and draws the frames (also Enter in a field, or Cmd or Ctrl Enter). ◀ Frame and Frame ▶ step through them, like the , and . keys. It renders the saved files: edits you have not saved yet are not included.`;
  const reduced = window.matchMedia?.('(prefers-reduced-motion: reduce)');
  const STAGES = {scenario: 'Scenario', content: 'Content check (cmake/JelliBehaviors and friends)', configure: 'CMake configure',
    art: 'Art check (build_slice / embed_slice)', compile: 'C compile', build: 'Build', render: 'Render', timeout: 'Timed out',
    tools: 'Build tools', unavailable: 'Unavailable'};
  const SAVE_URLS = new Set(['/api/save', '/api/clips', '/api/creatures', '/api/content', '/api/palette', '/api/create', '/api/scenes']);
  const defaults = {scene:'', tilt_x:0, tilt_y:0, travel_speed:0, location: '', present: '', present_state: 'none', completion: 'running', pet: '', form: '', page: 'home', menu: false, behavior: '', moment: '', asleep: false, mess: false, potty: 0,
    needs: {satiety: 700, energy: 700, hygiene: 700, amusement: 700, social: 700}, clock: false, minute: 540,
    start_ms: 0, frames: 12, step_ms: 200, advance_s: 0, live: false};
  const sc = Object.assign(structuredClone(defaults), store.get('game-scenario', {}));
  sc.needs = Object.assign({}, defaults.needs, sc.needs);
  let cat = null, result = null, frame = 0, playing = false, timer = null, busy = false, stale = false;
  let autoRender = store.get('game-auto', true), seenVersion = null;

  document.getElementById('views').insertAdjacentHTML('beforeend', '<button data-view="game" aria-pressed="false">Test in game</button>');
  document.querySelector('main').insertAdjacentHTML('beforeend', '<div id="game" class="hidden"><p id="gp-status" class="studio-status" role="status" aria-live="polite"></p><div id="gp-body"></div></div>');
  /** @type {any} */ (window).JelliDesign.watch(document.getElementById('game'));
  window.JelliShell?.registerViewRoot('game');
  document.head.insertAdjacentHTML('beforeend', `<style>
    #game{container:gp/inline-size}
    .gp-grid{display:grid;grid-template-columns:minmax(260px,340px) minmax(0,1fr);gap:22px;align-items:start}
    #gp-status{min-height:18px;margin:0 0 4px}
    /* The view's own width decides (RFC-007): under 800 px one column, with the rendered frame and its controls
     * pinned on top and the form below them. .gp-out dissolves so its parts order against the form. */
    @container gp (max-width:800px){
      .gp-grid{display:flex;flex-direction:column;align-items:stretch;gap:12px}.gp-grid>*{min-width:0}
      .gp-out{display:contents}.gp-out>*{order:0}.gp-grid>div:first-child{order:-1}.gp-out>.gp-lead{order:-2}
      .gp-out>.gp-pin{order:-3;position:sticky;top:0;z-index:5;background:var(--bg);padding:6px 0}
      .gp-pin .gp-big{width:min(100%,200px);min-width:0;margin:0 auto}
    }
    .gp-form fieldset{border:1px solid var(--line);border-radius:8px;margin:0 0 10px;padding:8px 10px}
    .gp-form legend{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;padding:0 4px}
    .gp-row{display:grid;grid-template-columns:110px 1fr;gap:6px;align-items:center;margin:4px 0}
    .gp-row label{font-size:12px;color:var(--muted)}.gp-row input[type=number]{width:96px}
    .gp-row select{max-width:none;width:100%}.gp-check{display:flex;gap:6px;align-items:center;margin:4px 0;font-size:12px}
    .gp-form [aria-invalid=true]{outline:2px solid var(--bad);outline-offset:1px}
    .gp-actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}
    .gp-unsaved{margin:6px 0;color:var(--warn);font-size:12px}.gp-actions .gp-unsaved{flex:1 1 100%;margin:0}
    .gp-errors{border:1px solid var(--bad);border-radius:8px;padding:8px 12px;margin:0 0 12px;background:#fa8c9914}
    .gp-errors h3{margin:0 0 4px;font-size:13px;color:var(--bad)}.gp-errors ul{margin:4px 0;padding-left:18px}
    .gp-errors pre{max-height:220px;overflow:auto;font:11px ui-monospace,monospace;color:var(--muted);white-space:pre-wrap}
    .gp-big{width:min(466px,100%,calc(100vh - 300px));min-width:233px;aspect-ratio:1;border-radius:50%;background:#000;image-rendering:pixelated;display:block}
    .gp-strip{display:flex;flex-wrap:wrap;gap:6px;margin:10px 0}
    .gp-strip button{padding:2px;border-radius:50%;line-height:0}.gp-strip button[aria-current=true]{border-color:var(--accent);outline:2px solid var(--accent)}
    .gp-strip img{width:72px;height:72px;border-radius:50%;image-rendering:pixelated}
    .gp-report{font:11px ui-monospace,monospace;color:var(--muted);display:flex;flex-wrap:wrap;gap:4px 14px;margin:8px 0}
    .gp-report b{color:var(--ink);font-weight:500}.gp-note{font-size:11px;color:var(--muted);max-width:640px}
  </style>`);

  /* ---------- server ---------- */
  async function loadCatalogue() {
    try { cat = await S.api('GET', '/api/game-preview'); } catch (err) { cat = {available: false, reason: err.message, pages: [], entries: [], states: [], moments: [], needs: []}; }
  }
  function scenario() {
    const out = {pet: sc.pet, form: sc.form, page: sc.page, menu: sc.menu && !!pageOf(sc.page)?.ring, behavior: sc.behavior,
      scene:sc.scene, tilt_x:sc.tilt_x, tilt_y:sc.tilt_y, travel_speed:sc.travel_speed,
      location: sc.location, present: sc.present, present_state: sc.present_state, completion: sc.completion,
      moment: sc.asleep ? '' : sc.moment, asleep: sc.asleep, mess: sc.mess, potty: sc.potty, needs: sc.needs,
      minute: sc.clock ? sc.minute : -1, start_ms: sc.start_ms, frames: sc.frames, step_ms: sc.step_ms, advance_s: sc.advance_s, live: sc.live};
    return out;
  }
  async function renderGame(reason = '') {
    if (busy || !cat?.available) return;
    busy = true; stop(); paint();
    await loadCatalogue();
    const what = unsaved().length ? 'the saved files' : 'the working copy';
    say(reason ? `${reason}: building the engine from ${what} and rendering…` : `Building the engine from ${what} and rendering…`);
    const started = performance.now();
    try {
      const res = await fetch('/api/game-preview', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({scenario: scenario()})});
      const data = await res.json().catch(() => ({ok: false, stage: 'render', error: res.statusText}));
      result = data; frame = 0; stale = false; seenVersion = D.version;
      const secs = ((performance.now() - started) / 1000).toFixed(1);
      if (data.ok) {
        const open = unsaved(), frames = `${data.frames.length} engine frame${data.frames.length === 1 ? '' : 's'}`;
        // Only saved files reach the engine: with unsaved edits, say so rather than claim a rebuild from "your changes".
        say(open.length ? `Rendered ${frames} in ${secs} s from the saved files. ${unsavedNote(open)}`
          : `Rendered ${frames} in ${secs} s${data.rebuilt ? ' (engine rebuilt from your changes)' : ''}.`);
        if (!reduced?.matches && data.frames.length > 1) playing = true;
      } else say(`${STAGES[data.stage] || 'Render'} failed; see the errors above the preview.`, 'bad');
    } catch (err) {
      result = {ok: false, stage: 'render', error: err.message}; say(`Render failed: ${err.message}`, 'bad');
    }
    busy = false; paint(); if (playing) play();
  }

  /* ---------- unsaved edits: the engine reads the files on disk, never the page's working copy ---------- */
  const unsaved = () => window.JelliShell?.dirtyItems?.() || [];
  const unsavedNote = (items = unsaved()) => {
    const n = items.reduce((sum, i) => sum + i.n, 0);
    return items.length ? `${S.count(n, 'unsaved edit')} ${n === 1 ? 'is' : 'are'} not included: ${items.map(i => i.label).join(', ')}. Save first to see ${n === 1 ? 'it' : 'them'}.` : '';
  };
  /* Two notes follow the edits while the view is open: the full one under the title, where it is on screen, and a short
   * one in the Render button's row, which can sit below the fold of a tall form. Neither exists when nothing is unsaved. */
  function showNote(id, parent, before, text) {
    let el = document.getElementById(id);
    if (!text || !parent) return el?.remove();
    if (!el) { el = document.createElement('p'); el.id = id; el.className = 'gp-unsaved'; if (id === 'gp-unsaved') el.setAttribute('role', 'status'); before ? parent.insertBefore(el, before) : parent.append(el); }
    if (el.textContent !== text) el.textContent = text;
  }
  function showUnsaved() {
    const items = unsaved(), n = items.reduce((sum, i) => sum + i.n, 0);
    showNote('gp-unsaved', document.getElementById('gp-body'), document.querySelector('#gp-body .gp-grid'), items.length ? `Renders the saved files. ${unsavedNote(items)}` : '');
    showNote('gp-unsaved-near', document.querySelector('#gp-form .gp-actions'), null, items.length ? `Renders the saved files, not your ${S.count(n, 'unsaved edit')}.` : '');
  }
  setInterval(() => { if (state.view === 'game' && !document.hidden) showUnsaved(); }, 1000);

  /* ---------- helpers ---------- */
  const pageOf = name => cat?.pages.find(p => p.name === name);
  const entryOf = name => cat?.entries.find(e => e.name === name) || cat?.entries[0];
  const opt = (value, label, current) => `<option value="${esc(value)}"${String(value) === String(current) ? ' selected' : ''}>${esc(label)}</option>`;
  const num = (id, label, value, min, max, step = 1, help = '') =>
    `<div class="gp-row"><label for="gp-${id}">${label}</label><input type="number" id="gp-${id}" data-k="${id}" min="${min}" max="${max}" step="${step}" value="${value}"${help ? ` aria-describedby="gp-${id}-help"` : ''}></div>${help ? `<div class="gp-note" id="gp-${id}-help">${help}</div>` : ''}`;
  const check = (id, label, value, disabled = false) =>
    `<label class="gp-check"><input type="checkbox" id="gp-${id}" data-k="${id}"${value ? ' checked' : ''}${disabled ? ' disabled' : ''}> ${label}</label>`;
  const hhmm = m => `${String(Math.floor(m / 60)).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}`;
  /* With the page shell (shell.js) its one live region announces; the inline line stays as a visible record. */
  const shell = () => window.JelliShell;
  function say(text, cls = '') {
    const el = document.getElementById('gp-status');
    if (el) { el.textContent = text; el.className = 'studio-status ' + cls; if (shell()?.notify) el.removeAttribute('aria-live'); }
    shell()?.notify?.(text, {tone: cls || 'info', id: 'game-preview'});
  }
  function persist() { store.set('game-scenario', sc); }
  function poseName(index) {
    const base = D.creature_poses || [], extra = (D.state_poses || []).map(p => p.name);
    return [...base, ...extra][index] ?? String(index);
  }

  /* ---------- view ---------- */
  function form() {
    if (!cat.entries.length) return '<p class="gp-note">No pets in content/pets.json.</p>';
    const entry = entryOf(sc.pet); if (!sc.pet) sc.pet = entry.name;
    if (!entry.forms.some(f => f.name === sc.form)) sc.form = entry.forms[0]?.name || '';
    const ring = !!pageOf(sc.page)?.ring;
    return `<form class="gp-form" id="gp-form" novalidate>
      <fieldset><legend>Pet</legend>
        <div class="gp-row"><label for="gp-pet">Pet</label><select id="gp-pet" data-k="pet">${cat.entries.map(e => opt(e.name, e.name, sc.pet)).join('')}</select></div>
        <div class="gp-row"><label for="gp-form-pick">Form</label><select id="gp-form-pick" data-k="form">${entry.forms.map(f => opt(f.name, f.name, sc.form)).join('')}</select></div>
        <div class="gp-row"><label for="gp-behavior">Behaviour</label><select id="gp-behavior" data-k="behavior">${opt('', 'none (idle)', sc.behavior)}${cat.states.map(s => opt(s, s, sc.behavior)).join('')}</select></div>
        <div class="gp-row"><label for="gp-moment">Moment</label><select id="gp-moment" data-k="moment"${sc.asleep ? ' disabled aria-describedby="gp-moment-help"' : ''}>${opt('', 'none', sc.moment)}${cat.moments.map(m => opt(m, m, sc.moment)).join('')}</select></div>
        <div class="gp-row"><label for="gp-completion">Completion</label><select id="gp-completion" data-k="completion">${['running','first','repeat'].map(v=>opt(v,{running:'Show running activity',first:'Complete once',repeat:'Complete twice today'}[v],sc.completion)).join('')}</select></div>
        <div class="gp-note">Completion runs real commands and simulation. Repeated daily recipes pay costs and rewards only on the first run.</div>
        ${sc.asleep ? '<div class="gp-note" id="gp-moment-help">A sleeping pet cannot run a moment.</div>' : ''}
        ${check('asleep', 'Asleep (REST command)', sc.asleep)}${check('mess', 'Potty mess on the floor', sc.mess)}
        ${num('potty', 'Potty urge', sc.potty, 0, 1000, 10)}
      </fieldset>
      ${cat.scenes?.length ? `<fieldset><legend>Background motion</legend>
        <div class="gp-row"><label for="gp-scene">Saved scene</label><select id="gp-scene" data-k="scene">${opt('','Flat location background',sc.scene)}${cat.scenes.map(s=>opt(s.key,s.name,sc.scene)).join('')}</select></div>
        ${num('tilt_x','Horizontal tilt (%)',sc.tilt_x,-100,100)}
        ${num('tilt_y','Vertical tilt (%)',sc.tilt_y,-100,100)}
        ${num('travel_speed','Travel speed (px/s)',sc.travel_speed,-32,32)}
        <p class="gp-note">Motion applies to the selected saved scene. Travel starts at zero in frame 1.</p></fieldset>` : ''}
      <fieldset><legend>Needs (0–1000)</legend>${cat.needs.map(n => num(`need-${n}`, n, sc.needs[n], 0, 1000, 10)).join('')}</fieldset>
      <fieldset><legend>Scene and presents</legend>
        <div class="gp-row"><label for="gp-location">Location</label><select id="gp-location" data-k="location">${opt('','Default home',sc.location)}${(cat.locations||[]).map(p=>opt(p.key,p.name,sc.location)).join('')}</select></div>
        <div class="gp-row"><label for="gp-present">Present</label><select id="gp-present" data-k="present">${opt('','None',sc.present)}${(cat.prizes||[]).map(p=>opt(p.key,p.name,sc.present)).join('')}</select></div>
        <div class="gp-row"><label for="gp-present_state">Present state</label><select id="gp-present_state" data-k="present_state">${['none','offered','owned'].map(v=>opt(v,v,sc.present_state)).join('')}</select></div>
        <p class="gp-note">Offered shows the pending catch. Owned shows collection ownership. An activity can choose its own allowed location when started.</p>
      </fieldset>
      <fieldset><legend>Screen</legend>
        <div class="gp-row"><label for="gp-page">Page</label><select id="gp-page" data-k="page">${cat.pages.map(p => opt(p.name, p.name.replace(/_/g, ' ') + (p.ring ? ' · ring' : ''), sc.page)).join('')}</select></div>
        ${check('menu', 'Ring menu open', sc.menu && ring, !ring)}
        ${check('clock', 'Set the clock', sc.clock)}
        <div class="gp-row"><label for="gp-minute">Clock time</label><input type="time" id="gp-minute" data-k="minute" value="${hhmm(sc.minute)}"${sc.clock ? '' : ' disabled'} aria-describedby="gp-minute-help"></div>
        <div class="gp-note" id="gp-minute-help">Night falls from 20:00 to 06:00. Unset, the pet clock runs from content/pets.json.</div>
      </fieldset>
      <fieldset><legend>Time</legend>
        ${num('start_ms', 'Start at (ms)', sc.start_ms, 0, cat.limits.start_ms, 100, 'Animation time offset of the first frame.')}
        ${num('frames', 'Frames', sc.frames, 1, cat.limits.frames)}
        ${num('step_ms', 'Every (ms)', sc.step_ms, 1, cat.limits.step_ms, 50)}
        ${num('advance_s', 'Simulate first (s)', sc.advance_s, 0, cat.limits.advance_s, 1, 'Runs the game this many seconds before the first frame (needs decay, behaviour may change).')}
        ${check('live', 'Advance the game between frames', sc.live)}
      </fieldset>
      <div class="gp-actions"><button type="submit" class="primary" id="gp-render"${busy || !cat.available ? ' disabled' : ''}>${busy ? 'Rendering…' : 'Render in engine'}</button>
        <button type="button" id="gp-reset">Reset scenario</button></div>
      ${check('auto', 'Render after every save', autoRender)}
    </form>`;
  }
  function errors() {
    if (!result || result.ok) return '';
    const fields = result.errors || [];
    const items = fields.length ? fields.map(e => `<li><b>${esc(e.field)}</b>: ${esc(e.message)}</li>`) : (result.messages || [result.error]).map(m => `<li>${esc(m)}</li>`);
    const log = result.log?.length ? `<details><summary>Build log (last ${result.log.length} lines)</summary><pre>${esc(result.log.join('\n'))}</pre></details>` : '';
    return `<div class="gp-errors gp-lead" role="alert"><h3>${esc(STAGES[result.stage] || 'Render')} failed</h3><ul>${items.join('')}</ul>${log}
      <div class="gp-note">${result.stage === 'scenario' ? 'Fix the highlighted fields.' : 'This is what a real build of your working copy reports; fix the data and render again.'}</div></div>`;
  }
  function output() {
    if (!result?.ok) return `<p class="gp-note">${cat.available ? 'Pick a scenario and press <b>Render in engine</b> (or Enter in any field).' : ''}</p>`;
    const f = result.frames, e = result.engine;
    return `<div class="gp-pin"><img class="gp-big" id="gp-big" src="${f[frame]}" alt="${esc(result.alt[frame])}" title="${esc(result.alt[frame])}">
      <div class="gp-actions">${f.length > 1 ? `<button type="button" id="gp-prev" class="jd-c" title="Previous frame (, or ←)">◀ Frame</button><button type="button" id="gp-next" class="jd-c" title="Next frame (. or →)">Frame ▶</button>
        <button type="button" id="gp-play" aria-pressed="${playing}">${playing ? 'Pause' : 'Play'}</button>
        <label class="lbl" for="gp-frame">Frame</label><input type="range" id="gp-frame" min="0" max="${f.length - 1}" value="${frame}" aria-valuetext="Frame ${frame + 1} of ${f.length}, ${result.times_ms[frame]} ms">` : ''}
        <span class="lbl" id="gp-frame-label">${frame + 1}/${f.length} · ${result.times_ms[frame]} ms</span></div></div>
      <div class="gp-strip" role="group" aria-label="Rendered frames">${f.map((src, i) => `<button type="button" data-frame="${i}" aria-current="${i === frame}" aria-label="Show frame ${i + 1} at ${result.times_ms[i]} ms"><img src="${src}" alt=""></button>`).join('')}</div>
      <div class="gp-report" aria-label="What the engine accepted">engine: <span>pet <b>${esc(e.form_name)}</b></span><span>pose <b>${esc(poseName(e.pose))}</b></span>
        <span>behaviour <b>${esc(e.behavior || 'none')}</b></span><span>asleep <b>${e.asleep}</b></span><span>activity <b>${esc(e.activity)}</b></span><span>moment <b>${e.moment ? esc(cat.moments[e.moment - 1] || e.moment) : 'none'}</b></span>
        <span>location <b>${esc(e.location_name)}</b></span><span>present offered / visible <b>${e.offered} / ${e.visible_offered}</b></span><span>owned mask <b>${e.owned}</b></span><span>completed mask <b>${e.completed_moments}</b></span><span>bond / hydration <b>${e.bond} / ${e.hydration}</b></span><span>mess <b>${e.mess}</b></span><span>clock <b>${hhmm(e.clock_minute)}</b>${e.night ? ' (night)' : ''}</span><span>needs <b>${e.needs.join(' / ')}</b></span><span>potty <b>${e.potty}</b></span></div>
      <p class="gp-note">Frames come from <code>jelli_pet_render</code> built from the art and content on disk${result.last_build?.seconds != null ? ` (last build ${result.last_build.seconds} s at ${esc(result.last_build.at)})` : ''}. Unsaved edits in other views are not included.</p>`;
  }
  function paint() {
    const el = document.getElementById('gp-body');
    if (!cat) { el.innerHTML = '<p class="gp-note">Loading…</p>'; return; }
    const focused = document.activeElement?.id;
    el.innerHTML = `<div class="title"><h2>Test in game</h2><span class="lbl">the real C engine, from ${unsaved().length ? 'the saved files' : 'the working copy'}</span> ${help(GAME_HELP, 'About Test in game')}</div>
      ${cat.available ? '' : `<div class="gp-errors" role="alert"><h3>Test in game is unavailable here</h3><p>${esc(cat.reason)}</p><p class="gp-note">Install cmake and a C compiler (cc, gcc or clang) on the studio host to enable it.</p></div>`}
      <div class="gp-grid"><div>${form()}</div><div class="gp-out">${stale ? '<p class="gp-note gp-lead" role="status">Art or content changed since this render.</p>' : ''}${errors()}${output()}</div></div>`;
    wire(); showUnsaved();
    for (const e of result?.errors || []) {
      const id = e.field.startsWith('needs.') ? `gp-need-${e.field.slice(6)}` : e.field === 'form' ? 'gp-form-pick' : `gp-${e.field}`;
      document.getElementById(id)?.setAttribute('aria-invalid', 'true');
    }
    if (focused) document.getElementById(focused)?.focus();
  }
  function show(i) {
    if (!result?.ok) return;
    frame = (i + result.frames.length) % result.frames.length;
    const big = document.getElementById('gp-big'); if (!big) return;
    big.src = result.frames[frame]; big.alt = big.title = result.alt[frame];
    const slider = document.getElementById('gp-frame');
    if (slider) { slider.value = frame; slider.setAttribute('aria-valuetext', `Frame ${frame + 1} of ${result.frames.length}, ${result.times_ms[frame]} ms`); }
    document.getElementById('gp-frame-label').textContent = `${frame + 1}/${result.frames.length} · ${result.times_ms[frame]} ms`;
    document.querySelectorAll('.gp-strip button').forEach(b => b.setAttribute('aria-current', String(Number(b.dataset.frame) === frame)));
  }
  function play() {
    clearInterval(timer);
    if (!playing || !result?.ok || result.frames.length < 2) return;
    timer = setInterval(() => { if (!document.hidden && state.view === 'game') show(frame + 1); }, Math.max(40, result.scenario.step_ms));
  }
  function stop() { playing = false; clearInterval(timer); }
  function wire() {
    const f = document.getElementById('gp-form'); if (!f) return;
    f.onsubmit = e => { e.preventDefault(); renderGame(); };
    f.onchange = e => {
      const k = e.target.dataset.k; if (!k) return;
      if (k === 'auto') { autoRender = e.target.checked; store.set('game-auto', autoRender); return; }
      if (k.startsWith('need-')) sc.needs[k.slice(5)] = Number(e.target.value);
      else if (k === 'minute') { const [h, m] = e.target.value.split(':').map(Number); if (Number.isFinite(h) && Number.isFinite(m)) sc.minute = h * 60 + m; }
      else if (e.target.type === 'checkbox') sc[k] = e.target.checked;
      else if (e.target.type === 'number') sc[k] = Number(e.target.value);
      else sc[k] = e.target.value;
      if (k === 'pet') sc.form = '';
      persist();
      if (['pet', 'page', 'asleep', 'clock'].includes(k)) paint();
    };
    document.getElementById('gp-reset').onclick = () => { Object.assign(sc, structuredClone(defaults)); persist(); result = null; paint(); };
    for (const [id, step] of /** @type {[string, number][]} */ ([['gp-prev', -1], ['gp-next', 1]]))  // the on-screen form of the , and . keys
      document.getElementById(id)?.addEventListener('click', () => { stop(); show(frame + step); document.getElementById('gp-play')?.setAttribute('aria-pressed', 'false'); });
    document.getElementById('gp-play')?.addEventListener('click', () => { playing = !playing; paint(); play(); });
    document.getElementById('gp-frame')?.addEventListener('input', e => { stop(); document.getElementById('gp-play')?.setAttribute('aria-pressed', 'false'); show(Number(e.target.value)); });
    document.querySelector('.gp-strip')?.addEventListener('click', e => { const b = e.target.closest('button'); if (b) { stop(); show(Number(b.dataset.frame)); document.getElementById('gp-play')?.setAttribute('aria-pressed', 'false'); } });
  }

  /* ---------- render after save ---------- */
  const baseApi = S.api;
  S.api = async (method, url, body) => {
    const data = await baseApi(method, url, body);
    if (method === 'POST' && SAVE_URLS.has(url)) afterSave();
    return data;
  };
  function afterSave() {
    stale = !!result;
    if (!autoRender || !cat?.available) return;
    if (state.view === 'game') renderGame('Saved'); else stale = true;
  }
  // Files changed on disk outside the studio (studio.js polls and bumps D.version).
  setInterval(() => {
    if (state.view !== 'game' || busy || !result || !seenVersion || seenVersion === D.version || document.hidden) return;
    if (autoRender) renderGame('Files changed on disk'); else if (!stale) { stale = true; paint(); }
  }, 2000);

  window.JelliShell?.registerShortcuts?.('Test in game', [{keys: ['Enter'], description: 'Render in engine (in a field)'},
    {keys: ['Mod+Enter'], description: 'Render in engine'}, {keys: [',', 'ArrowLeft'], description: 'Previous frame'},
    {keys: ['.', 'ArrowRight'], description: 'Next frame'}]);

  /* ---------- hooks into the page ---------- */
  /** @type {any} */ (S).previewScene = key => {
    sc.scene=key;persist();window.JelliShell.activateMode('game');
  };
  const baseRenderView = renderView, baseSelect = select, baseKeydown = S.keydown;
  renderView = async () => {  // eslint-disable-line no-global-assign
    const on = state.view === 'game', el = document.getElementById('game');
    el.classList.toggle('hidden', !on);
    if (!on) { stop(); return baseRenderView(); }
    window.JelliShell?.showOnly('game');
    document.querySelectorAll('#views button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.view === 'game')));
    document.querySelector('main').scrollTop = 0;
    paint(); await loadCatalogue();
    paint();
    if ((stale || (seenVersion && seenVersion !== D.version)) && autoRender && result) renderGame('Art or content changed');
  };
  select = key => { if (state.view === 'game') state.view = 'detail'; baseSelect(key); };  // eslint-disable-line no-global-assign
  S.keydown = e => {
    if (state.view !== 'game') return baseKeydown(e);
    if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') { e.preventDefault(); renderGame(); return true; }
    if (!result?.ok || e.metaKey || e.ctrlKey || e.altKey) return true;
    if (e.key === 'ArrowRight' || e.key === '.') { stop(); show(frame + 1); return true; }
    if (e.key === 'ArrowLeft' || e.key === ',') { stop(); show(frame - 1); return true; }
    return true;  // other single-key shortcuts belong to the detail and creature views
  };
})();

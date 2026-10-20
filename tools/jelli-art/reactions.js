/* Jelli Art: the Behaviour view (RFC-005). Edits content/behaviors.json (night bounds,
 * states, repertoires, affinities, reactions) and each state's look in
 * content/creatures.json state_presentation, with a stimulus simulator that mirrors
 * core/behavior.c through simulator.js and previews looks as core/pet_behavior_draw.c
 * places them. Names come from the engine (cmake lists, C enums) via D.behavior_vocab.
 * Loaded after behaviour.js, which owns the shared working copy of creatures.json. */
'use strict';
(() => {
  const S = window.Studio, cr = S?.cr, Sim = window.JelliSim;
  if (!cr || !Sim || !S.creatureDoc) return;
  const esc = cr.esc, clone = v => JSON.parse(JSON.stringify(v));
  const PANEL = 466, CAPTION_Y = 254, CAPTION_RGB = '#84e7b5';  // MINT (0x8736) from core/pet_canvas.h
  const PICK_KINDS = ['effects', 'menus', 'health', 'icons', 'props', 'prizes'];
  const reduced = window.matchMedia?.('(prefers-reduced-motion: reduce)');
  let bwork = null, bloaded = null, bbase = null, bseen, bstale = false, previews = [], raf = null, started = performance.now();
  Object.assign(state, {bTab: store.get('behaviour-tab', 'states'), bState: store.get('behaviour-state', null),
    bRep: 0, bAnimate: !reduced?.matches, bMess: false});
  const sim = Object.assign({form: null, stimulus: 'present_caught', need: 'satiety', location: 'home', activityKind: 'moment',
    moment: null, health: 'potty', care: 'eating', prize: 0, plainGift: false, level: 1, petLocation: 'home', night: false,
    mood: 60, bond: 300, asleep: false, busy: false, behavior: '', cooldown: '', id: 1, ticks: '864000'}, store.get('behaviour-sim', {}));

  document.getElementById('views').insertAdjacentHTML('beforeend', '<button data-view="behaviour" aria-pressed="false">Behaviour</button>');
  document.querySelector('main').insertAdjacentHTML('beforeend', '<div id="behaviour" class="hidden"></div><div id="bv-picker" class="bv-picker hidden" role="dialog" aria-label="Choose a sprite"></div>');
  window.JelliShell?.registerViewRoot('behaviour');
  /* Fields are written as <label>Name</label><control>; give each unlabelled control the
   * visible text of the label before it, so screen readers announce it (re-applied on render). */
  const labelControls = root => {
    for (const label of root.querySelectorAll('label:not([for])')) {
      if (label.querySelector('input,select,textarea')) continue; // Already wraps its control.
      let next = label.nextElementSibling;
      while (next && !next.matches('input,select,textarea') && !next.querySelector('input,select,textarea')) next = next.nextElementSibling;
      const control = next && (next.matches('input,select,textarea') ? next : next.querySelector('input,select,textarea'));
      if (control && !control.hasAttribute('aria-label') && !control.hasAttribute('aria-labelledby'))
        control.setAttribute('aria-label', label.textContent.trim());
    }
  };
  const {words: W, help} = /** @type {any} */ (window).JelliDesign;
  /** @type {any} */ (window).JelliDesign.watch(document.getElementById('behaviour'));
  const REACT_HELP = `Each reaction answers one stimulus. Weight is its share among the reactions that fit; chance is the roll after it is picked. "when" limits it by place, day or night, mood range and a minimum bond. Earlier and Later change the order.`;
  const LOOK_HELP = `Effect is a sprite drawn above the head, scaled to about 40 px. Prop is a sprite drawn at 3x over the lower body. The caption is upper case, up to 16 characters.`;
  const BAR_HELP = `Save behaviour validates with the engine and saves both files (also Cmd or Ctrl S). Play previews animates the previews from elapsed time. Potty mess shows the mess beside the pet.`;
  for (const id of ['behaviour', 'bv-picker']) {
    const root = document.getElementById(id);
    new MutationObserver(() => labelControls(root)).observe(root, {childList: true, subtree: true});
  }
  document.head.insertAdjacentHTML('beforeend', `<style>
    #behaviour{container:bv/inline-size}
    .bv-grid{display:grid;grid-template-columns:220px minmax(0,1fr) auto;gap:20px;align-items:start}
    .bv-grid.rp{grid-template-columns:220px minmax(0,1fr)}.bv-grid.sm{grid-template-columns:minmax(0,1fr) auto}
    /* The view's own width decides, not the window's (RFC-007): list beside the inspector with the preview above it,
     * and under 800 px one column with the preview pinned on top. The mid tier is touch-only: a mouse and keyboard
     * desktop keeps the original three-column layout at its container width (the pages look exactly as before). */
    @media(any-pointer:coarse){@container bv (max-width:1300px){
      .bv-grid.st{grid-template-columns:220px minmax(0,1fr);grid-template-areas:"lst prev" "lst ins"}
      .bv-grid.st>:nth-child(1){grid-area:lst}.bv-grid.st>:nth-child(2){grid-area:ins}.bv-grid.st>:nth-child(3){grid-area:prev}
      .bv-grid .bv-preview canvas{width:min(100%,240px)!important;height:auto!important}
    }}
    @container bv (max-width:800px){
      .bv-grid,.bv-grid.rp,.bv-grid.sm,.bv-grid.st{display:flex;flex-direction:column;align-items:stretch;gap:16px}
      .bv-grid>*{min-width:0}
      .bv-preview{order:-1;position:sticky;top:0;z-index:5;background:var(--bg);padding:6px 0}
      .bv-grid .bv-preview canvas{width:min(100%,180px)!important}
    }
    .bv-list{display:grid;gap:4px}.bv-list button{text-align:left;display:flex;justify-content:space-between;gap:8px}
    .bv-list button .sub{font:10px ui-monospace,monospace;color:inherit}
    .bv-field{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:5px 0}
    .bv-field label,.bv-k{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
    .bv-note{font-size:11px;color:var(--muted)}.bv-field input[type=number]{width:78px}.bv-field input[type=text]{width:180px}
    .bv-chips{display:flex;flex-wrap:wrap;gap:4px}.bv-chips button{font-size:11px;padding:2px 7px}
    .bv-card{background:var(--raised);border:1px solid var(--line);border-radius:8px;padding:6px 8px;margin:5px 0;display:flex;gap:8px;flex-wrap:wrap;align-items:center}
    .bv-card.hit{border-color:var(--accent)}.bv-card.miss{opacity:.6}
    .bv-card .n{font:11px ui-monospace,monospace;color:var(--muted);width:22px}
    .bv-card select,.bv-card input{font-size:12px}.bv-card input[type=number]{width:62px}
    .bv-pickbtn{display:inline-flex;gap:6px;align-items:center}.bv-pickbtn img,.bv-picker img{image-rendering:pixelated;width:32px;height:32px;background:#000;border-radius:4px;object-fit:contain}
    .bv-picker{position:fixed;z-index:20;background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:10px;max-width:520px;max-height:60vh;overflow:auto;box-shadow:0 8px 30px #0008}
    .bv-picker .grid{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}.bv-picker .grid button{display:grid;justify-items:center;font:9px ui-monospace,monospace;padding:4px;width:76px;overflow:hidden}
    .bv-preview canvas{border-radius:50%}
    .bv-table{border-collapse:collapse;font-size:12px;margin:8px 0}.bv-table td,.bv-table th{border-bottom:1px solid var(--line);padding:3px 8px;text-align:left}
    .bv-table tr.hit td{color:var(--accent)}.bv-table tr.miss td{color:var(--muted)}
    .bv-outcome{font-size:14px;margin:8px 0}.bv-outcome b{color:var(--accent)}
    .bv-odds{display:grid;gap:3px;max-width:420px}.bv-odds div{display:grid;grid-template-columns:120px 1fr 48px;gap:6px;align-items:center;font:11px ui-monospace,monospace}
    .bv-odds i{display:block;height:8px;background:var(--accent);border-radius:4px}
    .bv-workflow{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px;margin:8px 0 16px}.bv-workflow>p{margin:0 0 9px;color:var(--muted);font-size:12px}.bv-workflow>p b{color:var(--ink)}.bv-workflow ol{list-style:none;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px;margin:0;padding:0}.bv-workflow li{display:grid;gap:3px;font-size:11px;color:var(--muted)}.bv-workflow li b{font-size:12px;color:var(--ink)}
    .bv-summary{padding:12px;background:var(--panel);border:1px solid var(--line);border-left:3px solid var(--accent);border-radius:6px;margin-bottom:12px}.bv-summary h3{overflow-wrap:anywhere;font-size:16px;text-transform:capitalize;margin:0 0 6px}.bv-summary p{font-size:12px;color:var(--muted);margin:5px 0}.bv-summary button{margin-top:4px;font-size:11px}
    .bv-list button{min-width:0}.bv-list button>span{min-width:0;overflow-wrap:anywhere}.bv-section-title{font-size:12px;margin:18px 0 7px}.bv-advanced{border-top:1px solid var(--line);padding:8px 0;margin-top:10px}.bv-advanced>summary{cursor:pointer;font-size:11px;font-weight:600;padding:4px 0}.bv-advanced .bv-chips{margin-block:6px 10px}
    #bv-state .bv-field{gap:6px 10px}#bv-state input[type=text]{max-width:100%}#bv-duplicate-state{display:inline-flex;align-items:center;gap:5px}.bv-card{display:grid;grid-template-columns:minmax(0,1fr);gap:7px;padding:10px}.bv-reaction-main,.bv-reaction-conditions,.bv-reaction-actions{display:flex;flex-wrap:wrap;align-items:center;gap:6px}.bv-rule-word{font-size:11px;font-weight:650}.bv-rule-details{margin:0;padding:3px 0}.bv-reaction-actions{justify-content:flex-end}.bv-reaction-actions button{font-size:11px}.bv-reaction-main select{max-width:100%}.bv-reaction-conditions{padding-top:8px}.bv-rule-details summary{color:var(--muted);font-weight:400}
    @container bv (min-width:801px) and (max-width:1300px){.bv-grid.st{grid-template-columns:200px minmax(0,1fr);grid-template-areas:"lst prev" "lst ins"}.bv-grid.st>:nth-child(1){grid-area:lst}.bv-grid.st>:nth-child(2){grid-area:ins}.bv-grid.st>:nth-child(3){grid-area:prev}.bv-grid .bv-preview canvas{width:min(100%,240px)!important;height:auto!important}}
    @container bv (max-width:800px){.bv-workflow ol{gap:8px}.bv-workflow{padding:10px}.bv-workflow li{font-size:10px}.bv-workflow li b{font-size:11px}.bv-summary{margin-top:4px}}
    @container bv (max-width:450px){.bv-workflow ol{grid-template-columns:1fr}.bv-workflow li{grid-template-columns:125px minmax(0,1fr);align-items:center}.bv-workflow li b{font-size:11px}}

  </style>`);

  /* ---------- data ---------- */
  const V = () => D.behavior_vocab, L = () => V()?.limits || {};
  const looks = () => S.creatureDoc.work?.state_presentation || [];
  const lookOf = name => looks().find(l => l.state === name);
  /* Measured against the document as loaded or last saved, so changes made elsewhere mark the page stale. */
  const bdirty = () => !!bwork && !!bloaded && JSON.stringify(bwork) !== JSON.stringify(bloaded);
  /* This view owns behaviors.json and the looks (state_presentation) of creatures.json; Behaviour & size edits on the
   * Creature tab are not its own, so its marker, Revert and Save leave them alone. */
  const ownDirty = () => bdirty() || S.creatureDoc.looksDirty();
  const prevDirty = S.clipsDirty;
  S.clipsDirty = () => prevDirty() || bdirty();  // guards closing the page with unsaved behaviour edits
  S.statesDirty = () => ownDirty();  // shell.js: the "Behaviour states" unsaved item
  function sync() {
    if (D.behavior_data_sha === bseen) return;
    bseen = D.behavior_data_sha;
    if (!bdirty() || !bwork) { bwork = D.behavior_data ? clone(D.behavior_data) : null; bloaded = bwork && clone(bwork); bbase = D.behavior_data_sha; bstale = false; }
    else bstale = true;
  }
  const stateNames = () => (bwork?.states || []).map(s => s.name);
  const repertoireOf = formName => (bwork?.repertoires || []).find(r => (r.forms || []).includes(formName));
  const artOf = formName => V()?.forms.find(f => f.name === formName)?.art;
  const assetById = id => D.assets.find(a => a.id === id);

  /* Every path into a document: "states.2.effects.energy". Missing objects are created; '' deletes. */
  function setPath(doc, path, value) {
    const keys = path.split('.'), last = keys.pop(), parents = [];
    let node = doc;
    for (const k of keys) { parents.push([node, k]); node = node[k] ??= {}; }
    if (value === undefined) delete node[last]; else node[last] = value;
    for (const [parent, k] of parents.reverse()) {  // drop emptied optional objects (effects, when, request)
      const v = parent[k];
      if (v && typeof v === 'object' && !Array.isArray(v) && !Object.keys(v).length) delete parent[k]; else break;
    }
  }
  function parse(raw, type) {
    if (type === 'int') { const n = Number(raw); return raw === '' ? NaN : n; }
    if (type === 'opt-int') return raw === '' ? undefined : Number(raw);
    if (type === 'opt-int0') return raw === '' || Number(raw) === 0 ? undefined : Number(raw);
    if (type === 'opt-str') return raw === '' ? undefined : raw;
    if (type === 'tri') return raw === '' ? undefined : raw === 'true';
    if (type === 'caption') return raw.toUpperCase();
    return raw;
  }

  /* ---------- validation, mirroring cmake/JelliBehaviors.cmake and creature_data.load_looks ---------- */
  const bad = (v, lo, hi) => !Number.isInteger(v) || v < lo || v > hi;
  function stateIssues(s, lists, lim) {
    const out = [], n = s.name || '(unnamed)', d = s.duration_s || [];
    if (!/^[a-z][a-z0-9_]*$/.test(s.name || '')) out.push(`State ${n}: names are lower_snake_case.`);
    if (bad(d[0], 1, lim.duration_max_s) || bad(d[1], d[0] || 1, lim.duration_max_s)) out.push(`State ${n}: duration is 1–${lim.duration_max_s} s, maximum at least the minimum.`);
    if (bad(s.cooldown_s, 0, lim.cooldown_max_s)) out.push(`State ${n}: cooldown is 0–${lim.cooldown_max_s} s.`);
    for (const [k, v] of Object.entries(s.effects || {}))
      if (![...lists.needs, 'bond'].includes(k) || bad(v, -lim.effect, lim.effect)) out.push(`State ${n}: effect ${k} must be a need or bond, −${lim.effect}…${lim.effect}.`);
    for (const e of s.ends_on || []) if (!lists.stimuli.includes(e)) out.push(`State ${n}: unknown ends_on stimulus ${e}.`);
    if (s.request) {
      if (!lists.commands.includes(s.request.command)) out.push(`State ${n}: pick the request's command.`);
      if (s.request.health !== undefined && !lists.health.includes(s.request.health)) out.push(`State ${n}: unknown health routine ${s.request.health}.`);
      if (bad(s.request.bond ?? 0, 0, lim.request_bond)) out.push(`State ${n}: request bond is 0–${lim.request_bond}.`);
    }
    if (s.on_timeout !== undefined && !lim.timeouts.includes(s.on_timeout)) out.push(`State ${n}: on_timeout is ${lim.timeouts.join(' or ')}.`);
    return out;
  }
  function reactionIssues(r, rep, i, lists, lim, names) {
    const out = [], at = `${rep.name} reaction ${i + 1}`, v = V();
    if (!lists.stimuli.includes(r.on)) out.push(`${at}: pick a stimulus.`);
    if (!names.includes(r.state)) out.push(`${at}: unknown state ${r.state}.`);
    if (r.weight !== undefined && bad(r.weight, 1, lim.weight_max)) out.push(`${at}: weight is 1–${lim.weight_max}.`);
    if (r.chance_pct !== undefined && bad(r.chance_pct, 1, lim.chance_max)) out.push(`${at}: chance is 1–${lim.chance_max} %.`);
    if (r.need !== undefined && !lists.needs.includes(r.need)) out.push(`${at}: unknown need.`);
    if (r.location !== undefined && !lists.locations.includes(r.location)) out.push(`${at}: unknown location.`);
    if (r.moment !== undefined && !v.moments.includes(r.moment)) out.push(`${at}: unknown moment ${r.moment}.`);
    if (r.health !== undefined && !lists.health.includes(r.health)) out.push(`${at}: unknown health routine.`);
    if (r.activity !== undefined && !lists.care.includes(r.activity)) out.push(`${at}: unknown care activity.`);
    if (r.prize !== undefined && bad(r.prize, 0, lim.prize_max)) out.push(`${at}: prize index is 0–${lim.prize_max}.`);
    const w = r.when || {};
    if (w.location !== undefined && !lists.locations.includes(w.location)) out.push(`${at}: unknown condition location.`);
    if (bad(w.mood_min ?? 0, 0, lim.mood_max) || bad(w.mood_max ?? 100, w.mood_min ?? 0, lim.mood_max)) out.push(`${at}: mood range is 0–100, max at least min.`);
    if (bad(w.bond_min ?? 0, 0, lim.bond_max)) out.push(`${at}: minimum bond is 0–${lim.bond_max}.`);
    return out;
  }
  function behaviourIssues() {
    const d = bwork, v = V(); if (!d || !v) return [];
    const lists = v.lists, lim = L(), out = [], names = stateNames();
    if (bad(d.night?.start_minute, 0, lim.minute_max) || bad(d.night?.end_minute, 0, lim.minute_max)) out.push('Night bounds are minutes 0–1439.');
    if (bad(d.need_low, 1, lim.need_low_max)) out.push(`The low-need threshold is 1–${lim.need_low_max}.`);
    if (names.length > lim.states) out.push(`At most ${lim.states} states.`);
    names.forEach((n, i) => { if (names.indexOf(n) !== i) out.push(`Two states are named ${n}.`); });
    for (const s of d.states) out.push(...stateIssues(s, lists, lim));
    if (d.repertoires.length > lim.repertoires) out.push(`At most ${lim.repertoires} repertoires.`);
    const owner = {};
    let reactions = 0, affinities = 0;
    for (const rep of d.repertoires) {
      if (!rep.forms?.length) out.push(`${rep.name}: give the repertoire at least one form.`);
      for (const f of rep.forms || []) {
        if (!v.forms.some(x => x.name === f)) out.push(`${rep.name}: unknown form ${f}.`);
        if (owner[f]) out.push(`${f} is in both ${owner[f]} and ${rep.name}.`);
        owner[f] = rep.name;
      }
      if (!rep.reactions?.length) out.push(`${rep.name}: add at least one reaction.`);
      (rep.reactions || []).forEach((r, i) => out.push(...reactionIssues(r, rep, i, lists, lim, names)));
      for (const a of rep.affinities || [])
        if (!v.moments.includes(a.moment) || bad(a.bonus_pct, 0, lim.bonus_max)) out.push(`${rep.name}: affinities name a moment and a bonus of 0–${lim.bonus_max} %.`);
      reactions += rep.reactions?.length || 0; affinities += rep.affinities?.length || 0;
    }
    if (reactions > lim.reactions_total || affinities > lim.affinities_total) out.push(`At most ${lim.reactions_total} reactions and ${lim.affinities_total} affinities in all.`);
    return out;
  }
  function lookIssues() {
    const out = [], names = stateNames(), seen = looks().map(l => l.state);
    for (const n of names) if (!seen.includes(n)) out.push(`State ${n} has no look.`);
    for (const l of looks()) {
      if (!names.includes(l.state)) out.push(`Look for unknown state ${l.state}.`);
      if (!cr.poses().includes(l.pose)) out.push(`Look ${l.state}: unknown pose ${l.pose}.`);
      if ((l.caption || '').length > 16 || !/^[A-Z !?.,0-9]*$/.test(l.caption || '')) out.push(`Look ${l.state}: captions are upper case, at most 16 characters.`);
      for (const f of ['effect', 'prop']) if (l[f] && !byKey[l[f]]) out.push(`Look ${l.state}: unknown ${f} sprite ${l[f]}.`);
    }
    return out;
  }
  const allIssues = () => [...behaviourIssues(), ...lookIssues()];  // the Creature tab's size messages gate its own Save

  /* ---------- editing ---------- */
  function edit(fn) { fn(bwork, S.creatureDoc.work); render(); S.creatureDoc.refresh(); }
  function ensureLooks(c) { c.state_presentation ||= []; return c.state_presentation; }
  function renameState(from, to) {
    edit((b, c) => {
      const s = b.states.find(x => x.name === from); if (!s) return;
      s.name = to;
      for (const rep of b.repertoires) for (const r of rep.reactions || []) if (r.state === from) r.state = to;
      for (const l of ensureLooks(c)) if (l.state === from) l.state = to;
      state.bState = to;
    });
  }
  function addState() {
    let name = 'new_state', i = 2;
    while (stateNames().includes(name)) name = `new_state_${i++}`;
    edit((b, c) => {
      b.states.push({name, duration_s: [10, 20], cooldown_s: 60, ends_on: ['touched', 'activity_started']});
      ensureLooks(c).push({state: name, pose: 'content', caption: ''});
      state.bState = name;
    });
  }
  // States have implicit table IDs: append the copy so existing saved IDs stay put.
  // Reaction rules remain unchanged; adding another weighted candidate changes gameplay.
  async function duplicateState(sourceName) {
    const source = bwork?.states.find(x => x.name === sourceName);
    if (!source || !lookOf(sourceName)) return S.status('This behaviour needs an appearance before it can be duplicated.', 'warn', true);
    if (bwork.states.length >= L().states) return S.status(`This catalog already has ${L().states} behaviours.`, 'warn', true);
    let name = `${sourceName}_copy`, n = 2;
    while (stateNames().includes(name)) name = `${sourceName}_copy_${n++}`;
    const result = await window.JelliShell.dialog({title: 'Duplicate behaviour', initial: '#bv-copy-name',
      body: `<p>Copy <b>${esc(sourceName)}</b>, including its timing, effects, requests and appearance. The original stays unchanged.</p>
        <label for="bv-copy-name">New behaviour key</label><input id="bv-copy-name" type="text" value="${esc(name)}" required pattern="[a-z][a-z0-9_]*" autocomplete="off" spellcheck="false" aria-describedby="bv-copy-help bv-copy-error" style="width:100%">
        <p id="bv-copy-help" class="studio-note">Use a unique lower_snake_case key. Connect the copy in Reaction rules when you want a creature to use it; existing triggers and their odds stay unchanged.</p>
        <p id="bv-copy-error" class="bh-bad" role="alert"></p>`,
      actions: [{label: 'Cancel', value: 'cancel'}, {label: 'Duplicate behaviour', value: 'copy', primary: true}],
      onOpen(dialog) {
        const input = /** @type {HTMLInputElement} */ (dialog.querySelector('#bv-copy-name'));
        const error = dialog.querySelector('#bv-copy-error');
        const validate = () => {
          name = input.value.trim();
          const message = !/^[a-z][a-z0-9_]*$/.test(name) ? 'Use lower_snake_case: start with a letter, then letters, numbers or underscores.'
            : stateNames().includes(name) ? 'A behaviour already uses that key. Choose another.'
              : bwork.states.length >= L().states ? `The catalog is full (${L().states} behaviours).` : '';
          input.setCustomValidity(message); error.textContent = message; return !message;
        };
        input.addEventListener('input', validate);
        dialog.querySelector('[value=copy]').addEventListener('click', e => { if (!validate()) { e.preventDefault(); input.reportValidity(); } else input.value = name; });
        input.addEventListener('keydown', e => { if (e.key !== 'Enter') return; e.preventDefault(); if (validate()) dialog.close('copy'); else input.reportValidity(); });
        input.select();
      }});
    if (result !== 'copy') return;
    // The dialog can span an asynchronous reload; validate against the current documents.
    const current = bwork?.states.find(x => x.name === sourceName), look = lookOf(sourceName);
    if (!current || !look || stateNames().includes(name) || !/^[a-z][a-z0-9_]*$/.test(name) || bwork.states.length >= L().states)
      return S.status('The behaviour catalog changed while the dialog was open. Duplicate it again from the current version.', 'warn', true);
    edit((b, c) => { b.states.push({...clone(current), name}); ensureLooks(c).push({...clone(look), state: name}); state.bState = name; });
    store.set('behaviour-state', name);
    S.status(`Duplicated ${sourceName} as ${name}. Its appearance is ready to preview; connect it in Reaction rules, then save.`, '', false, {keep: true});
    document.getElementById('bv-state')?.querySelector('input')?.focus({preventScroll: true});
  }
  async function deleteState(name) {
    const users = (bwork.repertoires || []).flatMap(r => (r.reactions || []).filter(x => x.state === name).map(() => r.name));
    const reactions = S.count(users.length, 'reaction');
    if (!await S.ask(`Delete state ${name}${users.length ? ` and the ${reactions} that enter${users.length === 1 ? 's' : ''} it` : ''}?`,
      {title: `Delete state ${name}`, confirmLabel: users.length ? `Delete state and ${reactions}` : 'Delete state', danger: true})) return;
    edit((b, c) => {
      b.states = b.states.filter(s => s.name !== name);
      for (const rep of b.repertoires) rep.reactions = (rep.reactions || []).filter(r => r.state !== name);
      c.state_presentation = ensureLooks(c).filter(l => l.state !== name);
      state.bState = b.states[0]?.name || null;
    });
  }
  const VALUE_FIELDS = ['need', 'location', 'moment', 'health', 'activity', 'prize'];
  function setStimulus(rep, i, on) {
    edit(b => { const r = b.repertoires[rep].reactions[i]; r.on = on; for (const f of VALUE_FIELDS) delete r[f]; });
  }
  function setActivityKind(rep, i, kind) {
    edit(b => {
      const r = b.repertoires[rep].reactions[i], v = V();
      for (const f of ['moment', 'health', 'activity']) delete r[f];
      if (kind === 'moment') r.moment = v.moments[0];
      if (kind === 'health') r.health = v.lists.health[0];
      if (kind === 'activity') r.activity = v.lists.care[0];
    });
  }

  /* ---------- drawing a look: core/pet_actor.c layout, core/pet_behavior_draw.c placement ---------- */
  function bbox(p) {
    let l = p.w, t = p.h, r = 0, b = 0;
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) if (p.data[(y * p.w + x) * 4 + 3]) { l = Math.min(l, x); t = Math.min(t, y); r = Math.max(r, x + 1); b = Math.max(b, y + 1); }
    return r ? [l, t, r, b] : null;
  }
  function centroidQ8(p) {  // tools/assets/sprite_geometry.opaque_centroid_q8
    let sx = 0, sy = 0, n = 0;
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) if (p.data[(y * p.w + x) * 4 + 3]) { sx += 2 * x + 1; sy += 2 * y + 1; n++; }
    return n ? [Math.floor((sx * 128 + Math.floor(n / 2)) / n), Math.floor((sy * 128 + Math.floor(n / 2)) / n)] : [0, 0];
  }
  function drawCentered(ctx, key, x, y, scale) {  // jelli_canvas_centered_sprite
    const p = decoded[key]?.after; if (!p) return;
    const [cx, cy] = centroidQ8(p);
    ctx.drawImage(p.img, x - Math.floor((cx * scale + 128) / 256), y - Math.floor((cy * scale + 128) / 256), p.w * scale, p.h * scale);
  }
  let tinted = null;
  function fontAtlas() {
    const font = D.assets.find(a => a.kind === 'font'), p = font && decoded[font.key]?.after;
    if (!p) return null;
    if (tinted?.src === p) return tinted.canvas;
    const c = document.createElement('canvas'); c.width = p.w; c.height = p.h;
    const x = c.getContext('2d'); x.drawImage(p.img, 0, 0); x.globalCompositeOperation = 'source-in'; x.fillStyle = CAPTION_RGB; x.fillRect(0, 0, p.w, p.h);
    tinted = {src: p, canvas: c};
    return c;
  }
  function drawCaption(ctx, text, y) {  // jelli_canvas_caption: soft dark band, then 2x text centred at 233
    if (!text) return;
    const half = text.length * 8 + 16;
    /* Each pixel keeps shade = 16 - min(min(edge, vertical), 8) sixteenths of its colour, so it is
     * darkened by alpha min(edge, vertical, 8) / 16: a flat middle span plus a ramp at both ends. */
    for (let dy = -8; dy < 32; dy++) {
      const m = Math.min(dy < 12 ? dy + 8 : 31 - dy, 8);
      if (m > 0) { ctx.fillStyle = `rgba(0,0,0,${m / 16})`; ctx.fillRect(233 - (half - m), y + dy, 2 * (half - m) + 1, 1); }
      for (let e = 1; e < m; e++) {
        ctx.fillStyle = `rgba(0,0,0,${e / 16})`;
        ctx.fillRect(233 - (half - e), y + dy, 1, 1); ctx.fillRect(233 + (half - e), y + dy, 1, 1);
      }
    }
    const atlas = fontAtlas(); if (!atlas) return;
    let x = 233 - Math.floor(text.length * 16 / 2);
    for (const ch of text) {
      const i = Math.max(0, Math.min(95, ch.charCodeAt(0) - 32));
      ctx.drawImage(atlas, (i % 16) * 8, Math.floor(i / 16) * 12, 8, 12, x, y, 16, 24);
      x += 16;
    }
  }
  /* The look of a state (or plain idle) for a form at elapsed ms since the state began. */
  function drawLook(view, spec) {
    const {ctx, css} = view, k = DPR * css / PANEL, look = spec.state ? lookOf(spec.state) : null;
    ctx.setTransform(k, 0, 0, k, 0, 0); ctx.imageSmoothingEnabled = false; ctx.clearRect(0, 0, PANEL, PANEL);
    ctx.save(); ctx.beginPath(); ctx.arc(233, 233, 233, 0, 7); ctx.clip(); ctx.fillStyle = '#000'; ctx.fillRect(0, 0, PANEL, PANEL);
    const bg = decoded['backgrounds.home']?.after; if (bg) ctx.drawImage(bg.img, 0, 0, PANEL, PANEL);
    const pose = look?.pose || 'idle', clip = spec.art && cr.resolved(`${spec.art}.${pose}`);
    const index = clip ? cr.frameAt(clip, spec.elapsed, false) : -1, key = index >= 0 ? clip.frames[index] : null;
    const p = key && decoded[key]?.after, scale = cr.scaleFor?.(spec.art) || 6, box0 = p && bbox(p);
    let box = {x: 233, y: 200, width: 0, height: 0};
    if (box0) {
      const layout = Sim.actorLayout(cr.groundAnchor(p), box0, scale);
      ctx.drawImage(p.img, layout.x, layout.y, p.w * scale, p.h * scale);
      box = layout.bounds;
    }
    const potty = D.potty;
    if (spec.mess && potty?.mess_sprites?.length) {
      const id = potty.mess_sprites[Sim.messFrame(spec.messTime, potty.mess_frame_ms, potty.mess_sprites.length)], a = assetById(id);
      if (a) { const m = Sim.messPlacement(box, a.height); drawCentered(ctx, a.key, m.x, m.y, m.scale); }
    }
    if (look && box.height) {
      const effect = look.effect && decoded[look.effect]?.after, at = Sim.lookPlacement(box, effect?.w || 0, Math.max(0, index));
      if (look.prop) drawCentered(ctx, look.prop, at.prop.x, at.prop.y, at.prop.scale);
      if (look.effect) drawCentered(ctx, look.effect, at.effect.x, at.effect.y, at.effect.scale);
    }
    drawCaption(ctx, look?.caption || (spec.mess ? 'OOPS! CLEAN UP' : ''), CAPTION_Y);
    ctx.restore();
    return `${key}|${look?.caption}|${look?.effect}|${look?.prop}|${index & 1}|${spec.mess ? Sim.messFrame(spec.messTime, potty?.mess_frame_ms, potty?.mess_sprites?.length) : '-'}|${scale}`;
  }
  function tick(now) {
    raf = null;
    if (state.view !== 'behaviour' || document.hidden) return;
    const elapsed = state.bAnimate ? now - started : 0;
    for (const v of previews) {
      if (!v.ctx.canvas.isConnected) continue;
      const spec = {...v.spec(), elapsed, messTime: elapsed};
      const sig = JSON.stringify([spec.art, spec.state, spec.mess]) + drawSignature(spec);
      if (v.drawn !== sig) { v.drawn = sig; drawLook(v, spec); }
    }
    if (state.bAnimate) raf = requestAnimationFrame(tick);
  }
  /* Cheap change detection: the frame index and mess frame that would be drawn. */
  function drawSignature(spec) {
    const look = spec.state ? lookOf(spec.state) : null, clip = spec.art && cr.resolved(`${spec.art}.${look?.pose || 'idle'}`);
    const p = D.potty, mf = spec.mess ? Sim.messFrame(spec.messTime, p?.mess_frame_ms, p?.mess_sprites?.length) : -1;
    return `${clip ? cr.frameAt(clip, spec.elapsed, false) : -1}|${mf}|${JSON.stringify(look)}|${cr.scaleFor?.(spec.art)}`;
  }
  function addPreview(host, css, spec) {
    const [c, ctx] = makeCanvas(css, css); host.append(c);
    c.setAttribute('role', 'img'); c.setAttribute('aria-label', 'Preview of the behaviour look on the round panel');
    previews.push({ctx, css, spec, drawn: null});
  }
  function schedule() { if (raf === null) raf = requestAnimationFrame(tick); }
  function redraw() { previews.forEach(p => { p.drawn = null; }); schedule(); }

  /* ---------- sprite picker ---------- */
  function openPicker(anchor, current, onPick) {
    const el = document.getElementById('bv-picker'), kinds = PICK_KINDS.filter(k => D.assets.some(a => a.kind === k));
    let kind = byKey[current]?.kind && kinds.includes(byKey[current].kind) ? byKey[current].kind : kinds[0];
    const draw = () => {
      el.innerHTML = `<div class="bv-chips">${kinds.map(k => `<button data-kind="${k}" aria-pressed="${k === kind}">${k}</button>`).join('')}<button data-none>None</button><button data-close>Close</button></div>
        <div class="grid">${D.assets.filter(a => a.kind === kind).map(a => `<button data-key="${esc(a.key)}" title="${esc(a.key)} · ${a.width}×${a.height}" aria-pressed="${a.key === current}"><img alt="" src="${a.after}">${esc(a.key.split('.')[1])}</button>`).join('')}</div>`;
    };
    draw();
    const r = anchor.getBoundingClientRect();
    el.style.left = `${Math.max(8, Math.min(r.left, window.innerWidth - 540))}px`; el.style.top = `${Math.min(r.bottom + 4, window.innerHeight - 200)}px`;
    el.classList.remove('hidden');
    el.onclick = e => {
      const b = e.target.closest('button'); if (!b) return;
      if (b.dataset.kind) { kind = b.dataset.kind; return draw(); }
      if (b.hasAttribute('data-close')) return el.classList.add('hidden');
      el.classList.add('hidden'); onPick(b.hasAttribute('data-none') ? undefined : b.dataset.key);
    };
    el.querySelector('button')?.focus();
  }
  document.addEventListener('keydown', e => { if (e.key === 'Escape') document.getElementById('bv-picker')?.classList.add('hidden'); });
  const pickButton = (attr, value) => `<button class="bv-pickbtn" ${attr}>${value && byKey[value] ? `<img alt="" src="${byKey[value].after}">${esc(value)}` : 'none'}</button>`;

  /* ---------- rendering ---------- */
  const opt = (list, value, any) => (any !== undefined ? `<option value=""${value === undefined || value === '' ? ' selected' : ''}>${esc(any)}</option>` : '') +
    list.map(v => `<option value="${esc(v)}"${String(v) === String(value) ? ' selected' : ''}>${esc(v)}</option>`).join('');
  const num = (path, value, lo, hi, type = 'int', extra = '') => `<input type="number" data-b="${path}" data-t="${type}" min="${lo}" max="${hi}" value="${value ?? ''}" ${extra}>`;
  const minuteText = m => Number.isInteger(m) ? `${String(Math.floor(m / 60)).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}` : '?';
  function render() {
    const el = document.getElementById('behaviour'); if (!el || state.view !== 'behaviour') return;
    sync();
    const openDisclosures = new Map([...el.querySelectorAll('details[data-bv-disclosure]')].map(x => [x.getAttribute('data-bv-disclosure'), x.hasAttribute('open')]));
    previews = [];
    const v = V();
    if (!v || !bwork) { el.innerHTML = '<div class="title"><h2>Behaviour</h2></div><p class="studio-note">This checkout has no content/behaviors.json or cmake/JelliBehaviors.cmake, so there is no behaviour to edit.</p>'; return; }
    const issues = allIssues(), canSave = D.behavior_editable && D.creature_data_editable, editable = canSave && !bstale && !S.creatureDoc.stale();
    const tabs = [['states', 'Behaviours'], ['repertoires', 'Reaction rules'], ['simulator', 'Test a reaction']];
    el.innerHTML = `<div class="title"><h2>Behaviour</h2><div class="seg" id="bv-tabs">${tabs.map(([id, t]) => `<button data-tab="${id}" aria-pressed="${state.bTab === id}">${t}</button>`).join('')}</div>
        <span class="bv-note">${ownDirty() ? '<span class="cr-dirty">Unsaved changes</span>' : 'Creature reactions & appearance'}</span></div>
      <div class="bv-workflow" id="bv-workflow" aria-label="How behaviours work"><p><b>A reaction is a When → Do rule.</b> Choose what happens, decide which creatures react to it, then preview the result.</p><ol><li><b>1 · Behaviours</b><span>What the pet does and how it looks.</span></li><li><b>2 · Reaction rules</b><span>When it happens, and which creatures use it.</span></li><li><b>3 · Test a reaction</b><span>Try an event and see why a rule matched.</span></li></ol></div>
      ${bstale || S.creatureDoc.stale() ? '<p class="bh-bad">A content file changed on disk while you were editing. Save asks before it overwrites the newer file; Revert loads it instead.</p>' : ''}
      ${D.behavior_editable ? '' : '<p class="bh-warn">Read-only: saving behaviour needs cmake and the checkout\'s cmake/JelliBehaviors.cmake (and --content for trials).</p>'}
      ${v.missing_numbers?.length ? `<p class="bh-warn">The C headers do not define: ${esc(v.missing_numbers.join(', '))}; the simulator may be inexact.</p>` : ''}
      <div id="bv-body"></div>
      <ul class="cr-msgs">${issues.slice(0, 12).map(m => `<li>${esc(m)}</li>`).join('') || `<li class="ok">${ownDirty() ? 'Unsaved behaviour edits.' : 'No unsaved behaviour edits.'}</li>`}${issues.length > 12 ? `<li>…and ${issues.length - 12} more.</li>` : ''}</ul>
      <div class="bar"><div class="seg"><button id="bv-revert" ${ownDirty() || bstale || S.creatureDoc.stale() ? '' : 'disabled'}>Revert</button><button id="bv-save" class="primary" ${saveState !== "saving" && !(S.creatureDoc.looksDirty() && S.creatureDoc.saving) && ownDirty() && !issues.length && canSave ? '' : 'disabled'} title="Validate with the engine and save (⌘S)">${window.JelliIcons.svg('save')}Save behaviour</button></div><span id="bv-save-status" class="save-state" role="status" data-save-state="${saveState}">${saveState === 'saving' ? 'Saving…' : saveState === 'error' ? 'Save failed' : S.creatureDoc.looksDirty() && S.creatureDoc.saving ? 'Another section is saving…' : ownDirty() ? 'Unsaved' : 'Saved'}</span>
        <button id="bv-animate" aria-pressed="${state.bAnimate}" title="Play preview clips from elapsed time">${state.bAnimate ? 'Pause previews' : 'Play previews'}</button>
        <button id="bv-mess" aria-pressed="${state.bMess}" title="Show the potty mess beside the pet (content/potty.json)">Potty mess</button> ${help(BAR_HELP, 'About these buttons')}</div>`;
    const body = el.querySelector('#bv-body');
    if (state.bTab === 'repertoires') renderRepertoires(body); else if (state.bTab === 'simulator') renderSimulator(body); else renderStates(body);
    el.querySelector('#bv-tabs').onclick = e => { const t = e.target.closest('button')?.dataset.tab; if (t) { state.bTab = t; store.set('behaviour-tab', t); render(); } };
    el.querySelector('#bv-revert').onclick = async () => {
      // Stale: creatures.json changed on disk, so reverting its looks reloads all of it, Behaviour & size edits included.
      const also = S.creatureDoc.stale() && S.creatureDoc.sizeDirty() ? ' creatures.json changed on disk, so this also discards your unsaved Behaviour & size edits on the Creature tab.' : '';
      if ((ownDirty() || also) && !await S.ask(`Discard your unsaved behaviour edits (states, repertoires, rules and looks) and go back to the saved ones?${also}`,
        {title: 'Discard behaviour edits', confirmLabel: 'Discard edits', danger: true})) return;
      bwork = D.behavior_data ? clone(D.behavior_data) : null; bloaded = bwork && clone(bwork); bbase = bseen = D.behavior_data_sha; bstale = false;
      S.creatureDoc.revertLooks(); window.JelliDrafts?.drop('behaviour', 'content'); render(); S.creatureDoc.refresh();
    };
    el.querySelector('#bv-save').onclick = () => S.saveContent();
    el.querySelector('#bv-animate').onclick = () => { state.bAnimate = !state.bAnimate; started = performance.now(); render(); };
    el.querySelector('#bv-mess').onclick = () => { state.bMess = !state.bMess; render(); };
    if (!editable) {  // the simulator and preview pickers still work on read-only content
      body.querySelectorAll('input, select, textarea, button').forEach(x => {
        if (!x.matches('[data-sim], [data-sim-step], [data-readonly-ok], [data-state], [data-rep], .jt-info')) x.disabled = true;
      });
    }
    body.querySelectorAll('details[data-bv-disclosure]').forEach(node => {
      const x = /** @type {HTMLDetailsElement} */ (node), key = x.dataset.bvDisclosure;
      x.open = openDisclosures.has(key) ? openDisclosures.get(key) : store.get(`bv-disclosure-${key}`, false);
      x.addEventListener('toggle', () => store.set(`bv-disclosure-${key}`, x.open));
    });
    wireBindings(body);
    redraw();
  }
  /* data-b paths write behaviors.json, data-c paths write creatures.json; data-t picks the parser. */
  function wireBindings(body) {
    body.addEventListener('change', e => {
      const t = e.target, type = t.dataset.t || 'str';
      if (t.dataset.rename !== undefined) return renameState(t.dataset.rename, t.value.trim());
      if (t.dataset.b) edit(b => setPath(b, t.dataset.b, parse(t.value, type)));
      else if (t.dataset.c) edit((b, c) => { ensureLooks(c); setPath(c, t.dataset.c, parse(t.value, type)); });
    });
  }

  function renderStates(body) {
    const b = bwork, v = V(), lists = v.lists;
    if (!stateNames().includes(state.bState)) state.bState = stateNames()[0] || null;
    const si = b.states.findIndex(s => s.name === state.bState), s = b.states[si];
    const list = b.states.map(x => `<button data-state="${esc(x.name)}" aria-pressed="${x.name === state.bState}"><span>${esc(x.name)}</span><span class="sub">${esc(lookOf(x.name)?.caption || '')}</span></button>`).join('');
    body.innerHTML = `<div class="bv-grid st"><div><div class="bv-k">Behaviours · ${b.states.length}/${L().states}</div><div class="bv-list" id="bv-states">${list}</div>
        <button id="bv-add-state" style="margin-top:6px" ${b.states.length >= L().states ? 'disabled' : ''}>New behaviour</button>
        <details class="bv-advanced" id="bv-global-rules" data-bv-disclosure="global"><summary>Global event settings</summary><p class="studio-note">These settings apply to every creature: when night begins and ends, and when a need becomes low.</p>
        <div class="bv-field"><label>Night from</label>${num('night.start_minute', b.night?.start_minute, 0, 1439)}<span class="bv-note">${minuteText(b.night?.start_minute)}</span></div>
        <div class="bv-field"><label>to</label>${num('night.end_minute', b.night?.end_minute, 0, 1439)}<span class="bv-note">${minuteText(b.night?.end_minute)} · minutes of the pet's day</span></div>
        <div class="bv-field"><label>Need low below</label>${num('need_low', b.need_low, 1, 999)}<span class="bv-note">of 1000 · raises need_low</span></div></details></div>
      <div id="bv-state">${s ? stateEditor(s, si, lists) : '<p class="studio-note">No states yet.</p>'}</div>
      <div class="bv-preview" id="bv-look-preview"></div></div>`;
    body.querySelector('#bv-states').onclick = e => { const n = e.target.closest('button')?.dataset.state; if (n) { state.bState = n; store.set('behaviour-state', n); render(); } };
    body.querySelector('#bv-add-state').onclick = addState;
    if (!s) return;
    body.querySelector('#bv-delete-state').onclick = () => deleteState(s.name);
    body.querySelector('#bv-duplicate-state').onclick = () => duplicateState(s.name);
    body.querySelector('#bv-open-rules').onclick = () => { const selected = s.name; state.bTab = 'repertoires'; const index = bwork.repertoires.findIndex(r => (r.reactions || []).some(x => x.state === s.name)); if (index >= 0) state.bRep = index; store.set('behaviour-tab', state.bTab); render(); if (index < 0) S.status(`Choose a creature group, add a reaction rule, and set its Do behaviour to ${selected}.`, '', false, {keep: true}); };
    body.querySelector('#bv-ends').onclick = e => {
      const stim = e.target.closest('button')?.dataset.end; if (!stim) return;
      edit(bw => { const x = bw.states[si], ends = x.ends_on ||= []; const i = ends.indexOf(stim); if (i < 0) ends.push(stim); else ends.splice(i, 1); });
    };
    body.querySelector('#bv-request').onchange = e => edit(bw => {
      const x = bw.states[si];
      if (!e.target.value) delete x.request; else x.request = {command: e.target.value, ...(e.target.value === 'health' ? {health: x.request?.health || lists.health[0]} : {}), bond: x.request?.bond ?? 10};
    });
    for (const f of ['effect', 'prop']) body.querySelector(`[data-pick="${f}"]`).onclick = e => openPicker(e.currentTarget, lookOf(s.name)?.[f], key => edit((bw, c) => {
      let l = ensureLooks(c).find(x => x.state === s.name);
      if (!l) { l = {state: s.name, pose: 'idle', caption: ''}; c.state_presentation.push(l); }
      if (key) l[f] = key; else delete l[f];
    }));
    body.querySelector('#bv-add-look')?.addEventListener('click', () => edit((bw, c) => ensureLooks(c).push({state: s.name, pose: 'idle', caption: ''})));
    renderLookPreview(body.querySelector('#bv-look-preview'), s.name);
  }
  function stateEditor(s, si, lists) {
    const lim = L(), p = `states.${si}`, li = looks().findIndex(l => l.state === s.name), look = looks()[li];
    const effects = [...lists.needs, 'bond'].map(n => `<div class="bv-field"><label>${n}</label>${num(`${p}.effects.${n}`, s.effects?.[n] ?? 0, -100, 100, 'opt-int0')}</div>`).join('');
    const users = bwork.repertoires.flatMap(r => (r.reactions || []).map((x, i) => [r.name, i, x]).filter(([, , x]) => x.state === s.name));
    const poseOpts = cr.poses().map(pose => { const f = cr.fallbackOf(pose); return `<option value="${esc(pose)}"${pose === look?.pose ? ' selected' : ''}>${esc(pose)}${f ? ` (state pose; falls back to ${esc(f)})` : ''}</option>`; }).join('');
    const matches = users.map(([rep, index, reaction]) => `${rep}: ${reaction.on}`).join(' · ');
    const summary = `${s.duration_s?.[0] ?? '?'}–${s.duration_s?.[1] ?? '?'} seconds · ${s.cooldown_s ?? '?'} seconds before it can repeat`;
    return `<section class="bv-summary" id="bv-state-summary"><h3>${esc(s.name.replace(/_/g, ' '))}</h3><p>${esc(summary)}</p><p>${users.length ? `Triggered by ${esc(matches)}.` : 'Not triggered yet. Add a reaction rule to let a creature use this behaviour.'}</p><button id="bv-open-rules" type="button">${users.length ? 'Edit its reaction rules' : 'Connect a reaction rule'} →</button></section>
      <div class="bv-field"><label>Behaviour key</label><input type="text" data-rename="${esc(s.name)}" value="${esc(s.name)}" pattern="[a-z][a-z0-9_]*" aria-label="State name"><button id="bv-duplicate-state" ${bwork.states.length >= lim.states || !look ? 'disabled' : ''} title="Copy this behaviour and appearance; choose a new key">${window.JelliIcons.svg('copy')}Duplicate behaviour</button></div>
      <h3 class="bv-section-title">Timing</h3><div class="bv-field"><label>Lasts</label>${num(`${p}.duration_s.0`, s.duration_s?.[0], 1, lim.duration_max_s, 'int', 'aria-label="Shortest duration, seconds"')}–${num(`${p}.duration_s.1`, s.duration_s?.[1], 1, lim.duration_max_s, 'int', 'aria-label="Longest duration, seconds"')}<span class="bv-note">s</span>
        <label>Cooldown</label>${num(`${p}.cooldown_s`, s.cooldown_s, 0, lim.cooldown_max_s)}<span class="bv-note">s before it can start again</span></div>
      <h3 class="bv-section-title">Appearance ${help(LOOK_HELP, 'About the look')}</h3><p class="studio-note">Reuse a pose clip, add a caption, and optionally place an effect or prop. The panel preview follows these settings.</p>
      ${look ? `<div class="bv-field"><label>Pose</label><select data-c="state_presentation.${li}.pose">${poseOpts}</select></div>
        <div class="bv-field"><label>Caption</label><input type="text" data-c="state_presentation.${li}.caption" data-t="caption" maxlength="16" value="${esc(look.caption || '')}" style="text-transform:uppercase" aria-label="Caption, upper case, at most 16"></div>
        <div class="bv-field"><label>Effect</label>${pickButton('data-pick="effect" title="Sprite drawn above the head, scaled to about 40 px"', look.effect)}
          <label>Prop</label>${pickButton('data-pick="prop" title="Sprite drawn at 3x over the lower body"', look.prop)}</div>`
        : `<p class="bh-warn">This state has no look yet.</p><button id="bv-add-look">Add look</button><span data-pick="effect"></span><span data-pick="prop"></span>`}
      <details class="bv-advanced" data-bv-disclosure="effects"${store.get('bv-disclosure-effects', false) ? ' open' : ''}><summary>Effects, requests & ending conditions</summary>      <div class="bv-k" style="margin-top:8px">Effects on entry · ±${lim.effect}, then clamped to 0–1000</div><div style="display:flex;flex-wrap:wrap;gap:4px 14px">${effects}</div>
      <div class="bv-k" style="margin-top:8px">Ends early on</div><div class="bv-chips" id="bv-ends">${lists.stimuli.map(st => `<button data-end="${st}" aria-pressed="${(s.ends_on || []).includes(st)}">${st}</button>`).join('')}</div>
      <div class="bv-field" style="margin-top:8px"><label>Request</label><select id="bv-request" aria-label="Command that answers the state">${opt(lists.commands, s.request?.command, 'none')}</select>
        ${s.request?.command === 'health' ? `<select data-b="${p}.request.health" data-t="opt-str" aria-label="Health routine">${opt(lists.health, s.request?.health, 'any routine')}</select>` : ''}
        ${s.request ? `<label>Bond reward</label>${num(`${p}.request.bond`, s.request.bond ?? 0, 0, lim.request_bond, 'opt-int')}` : ''}
        <label>On timeout</label><select data-b="${p}.on_timeout" data-t="opt-str">${opt(lim.timeouts.slice(1), s.on_timeout, 'nothing')}</select></div>
      <p class="studio-note">${s.request ? 'Answering the request ends the state with the bond reward; ignoring it times out.' : 'No request: the state just plays out.'} ${s.on_timeout === 'accident' ? 'Timing out causes a potty accident (the mess).' : ''}</p>
</details>
      <details class="bv-advanced" data-bv-disclosure="identity"${store.get('bv-disclosure-identity', false) ? ' open' : ''}><summary>References & removal</summary><p class="studio-note">${users.length ? users.map(([r, i, x]) => `${esc(r)} #${i + 1} on ${esc(x.on)}`).join(' · ') : 'No reaction enters this behaviour yet.'}</p><button id="bv-delete-state">Delete state</button></details>`;
  }
  function renderLookPreview(host, stateName) {
    const forms = V().forms.filter(f => f.art);
    const rep = (bwork.repertoires || []).find(r => (r.reactions || []).some(x => x.state === stateName));
    const def = forms.find(f => rep?.forms?.includes(f.name)) || forms.find(f => f.art === state.cForm) || forms[0];
    state.bLookForm = forms.some(f => f.name === state.bLookForm) ? state.bLookForm : def?.name;
    host.innerHTML = `<div class="bv-field"><label>Preview on</label><select id="bv-look-form" data-readonly-ok>${opt(forms.map(f => f.name), state.bLookForm)}</select></div>`;
    host.querySelector('#bv-look-form').onchange = e => { state.bLookForm = e.target.value; redraw(); };
    addPreview(host, Math.min(320, window.innerWidth - 80), () => ({art: artOf(state.bLookForm), state: stateName, mess: state.bMess}));
    host.insertAdjacentHTML('beforeend', `<p class="studio-note">At ${esc(artOf(state.bLookForm) || '')}'s profile scale, as the game places pose, prop, effect and caption.</p>`);
  }

  function renderRepertoires(body) {
    const b = bwork, v = V(), lists = v.lists;
    state.bRep = Math.min(state.bRep, Math.max(0, b.repertoires.length - 1));
    const rep = b.repertoires[state.bRep], ri = state.bRep;
    body.innerHTML = `<div class="bv-grid rp"><div><div class="bv-k">Creature groups · ${b.repertoires.length}/${L().repertoires}</div>
        <div class="bv-list" id="bv-reps">${b.repertoires.map((r, i) => `<button data-rep="${i}" aria-pressed="${i === ri}"><span>${esc(r.name)}</span><span class="sub">${esc((r.forms || []).join(', '))}</span></button>`).join('')}</div>
        <button id="bv-add-rep" style="margin-top:6px" ${b.repertoires.length >= L().repertoires ? 'disabled' : ''}>New creature group</button>
        <p class="studio-note">A group shares reaction rules across its selected creature forms. Forms without a group do not enter these behaviours.</p></div>
      <div>${rep ? repertoireEditor(rep, ri, lists, v) : '<p class="studio-note">No repertoires yet.</p>'}</div></div>`;
    body.querySelector('#bv-reps').onclick = e => { const i = e.target.closest('button')?.dataset.rep; if (i !== undefined) { state.bRep = Number(i); render(); } };
    body.querySelector('#bv-add-rep').onclick = () => edit(bw => {
      const free = v.forms.find(f => !repertoireOf(f.name)), name = `repertoire_${bw.repertoires.length + 1}`;
      bw.repertoires.push({name, forms: free ? [free.name] : [], affinities: [], reactions: [{on: 'present_caught', state: bw.states[0]?.name || ''}]});
      state.bRep = bw.repertoires.length - 1;
    });
    if (!rep) return;
    body.querySelector('#bv-del-rep').onclick = async () => {
      const forms = (rep.forms || []).length, text = `Delete repertoire ${rep.name}? ${forms ? `Its ${S.count(forms, 'form')}` : 'Its forms'} will no longer react.`;
      if (!await S.ask(text, {title: `Delete repertoire ${rep.name}`, confirmLabel: 'Delete repertoire', danger: true})) return;
      // Found by name after the dialog: the list may have changed while it was open.
      edit(bw => { const i = bw.repertoires.findIndex(r => r.name === rep.name); if (i >= 0) bw.repertoires.splice(i, 1); });
    };
    body.querySelector('#bv-forms').onclick = e => {
      const f = e.target.closest('button')?.dataset.form; if (!f) return;
      edit(bw => { const r = bw.repertoires[ri], i = (r.forms ||= []).indexOf(f); if (i < 0) r.forms.push(f); else r.forms.splice(i, 1); });
    };
    body.querySelector('#bv-add-aff').onclick = () => edit(bw => (bw.repertoires[ri].affinities ||= []).push({moment: v.moments[0], bonus_pct: 25}));
    body.querySelector('#bv-affs').onclick = e => { const i = e.target.closest('[data-del-aff]')?.dataset.delAff; if (i !== undefined) edit(bw => bw.repertoires[ri].affinities.splice(Number(i), 1)); };
    body.querySelector('#bv-add-reaction').onclick = () => edit(bw => (bw.repertoires[ri].reactions ||= []).push({on: 'idle', state: bw.states[0]?.name || '', chance_pct: 5}));
    const box = body.querySelector('#bv-reactions');
    box.addEventListener('change', e => {
      const t = e.target, i = Number(t.closest('[data-r]')?.dataset.r);
      if (t.dataset.on !== undefined) { e.stopPropagation(); setStimulus(ri, i, t.value); }
      if (t.dataset.actkind !== undefined) { e.stopPropagation(); setActivityKind(ri, i, t.value); }
    }, true);
    box.onclick = e => {
      const act = e.target.closest('button')?.dataset.act, i = Number(e.target.closest('[data-r]')?.dataset.r); if (!act) return;
      edit(bw => {
        const list = bw.repertoires[ri].reactions, j = act === 'up' ? i - 1 : i + 1;
        if (act === 'remove') list.splice(i, 1); else if (act === 'copy') list.splice(i + 1, 0, clone(list[i])); else [list[i], list[j]] = [list[j], list[i]];
      });
    };
  }
  function repertoireEditor(rep, ri, lists, v) {
    const p = `repertoires.${ri}`, names = stateNames();
    const formChips = v.forms.map(f => { const other = repertoireOf(f.name); const taken = other && other !== rep;
      return `<button data-form="${esc(f.name)}" aria-pressed="${(rep.forms || []).includes(f.name)}" ${taken ? `disabled title="In ${esc(other.name)}"` : ''}>${esc(f.name)}</button>`; }).join('');
    const affs = (rep.affinities || []).map((a, i) => `<div class="bv-card"><select data-b="${p}.affinities.${i}.moment" aria-label="Moment">${opt(v.moments, a.moment)}</select>
      +${num(`${p}.affinities.${i}.bonus_pct`, a.bonus_pct, 0, 200)}<span class="bv-note">% gains</span><button data-del-aff="${i}" title="Remove">${W('✕', 'Remove')}</button></div>`).join('');
    const rows = (rep.reactions || []).map((r, i) => reactionRow(r, i, `${p}.reactions.${i}`, lists, v, names, (rep.reactions || []).length)).join('');
    return `<div class="bv-field"><label>Name</label><input type="text" data-b="${p}.name" value="${esc(rep.name)}"><button id="bv-del-rep">Delete repertoire</button></div>
      <h3 class="bv-section-title">Who reacts?</h3><p class="studio-note">Choose the creature forms that share these rules. A form can belong to one group.</p><div class="bv-chips" id="bv-forms">${formChips}</div>
      <details class="bv-advanced" data-bv-disclosure="affinities-${ri}"><summary>Activity affinities · reward tuning</summary><p class="studio-note">Optional bonuses to an activity’s meter gains for this group.</p><div id="bv-affs">${affs}</div><button id="bv-add-aff">Add affinity</button></details>
      <h3 class="bv-section-title">When → Do · ${(rep.reactions || []).length} rules ${help(REACT_HELP, 'About reactions')}</h3><p class="studio-note">When an event happens, matching rules compete by weight. The chosen rule then rolls its chance to start the behaviour.</p>
      <div id="bv-reactions">${rows}</div><button id="bv-add-reaction">Add reaction</button>`;
  }
  function valueControls(r, p, lists, v) {
    if (r.on === 'need_low') return `<select data-b="${p}.need" data-t="opt-str" aria-label="Need">${opt(lists.needs, r.need, 'any need')}</select>`;
    if (r.on === 'location') return `<select data-b="${p}.location" data-t="opt-str" aria-label="Location">${opt(lists.locations, r.location, 'any place')}</select>`;
    if (/^activity_/.test(r.on)) {
      const kind = r.moment !== undefined ? 'moment' : r.health !== undefined ? 'health' : r.activity !== undefined ? 'activity' : '';
      const second = kind === 'moment' ? `<select data-b="${p}.moment" aria-label="Moment">${opt(v.moments, r.moment)}</select>`
        : kind === 'health' ? `<select data-b="${p}.health" aria-label="Health routine">${opt(lists.health, r.health)}</select>`
        : kind === 'activity' ? `<select data-b="${p}.activity" aria-label="Care activity">${opt(lists.care, r.activity)}</select>` : '';
      return `<select data-actkind aria-label="Activity kind"><option value=""${kind ? '' : ' selected'}>any activity</option>${['moment', 'health', 'activity'].map(k => `<option value="${k}"${k === kind ? ' selected' : ''}>${k === 'activity' ? 'care' : k}</option>`).join('')}</select>${second}`;
    }
    if (/^present_/.test(r.on)) return `<select data-b="${p}.prize" data-t="opt-int" aria-label="Prize">${opt([...Array(L().prize_max + 1).keys()], r.prize, 'any prize')}</select>`;
    return '<span class="bv-note">any value</span>';
  }
  function reactionRow(r, i, p, lists, v, names, count, hit) {
    const w = r.when || {};
    return `<div class="bv-card${hit ? ` ${hit}` : ''}" data-r="${i}"><div class="bv-reaction-main"><span class="n">${i + 1}</span><span class="bv-rule-word">When</span>
      <select data-on aria-label="Stimulus">${opt(lists.stimuli, r.on)}</select>${valueControls(r, p, lists, v)}
      <span class="bv-rule-word">→ Do</span> <select data-b="${p}.state" aria-label="State">${opt(names, r.state)}</select></div><details class="bv-advanced bv-rule-details" data-bv-disclosure="rule-${state.bRep}-${i}"><summary>Chance & conditions · ${r.chance_pct ?? 100}% · weight ${r.weight ?? 1}</summary><div class="bv-reaction-conditions">
      <label class="bv-note">weight</label>${num(`${p}.weight`, r.weight, 1, 100, 'opt-int', 'placeholder="1"')}
      <label class="bv-note">chance</label>${num(`${p}.chance_pct`, r.chance_pct, 1, 100, 'opt-int', 'placeholder="100"')}<span class="bv-note">%</span>
      <span class="bv-note">when</span><select data-b="${p}.when.location" data-t="opt-str" aria-label="Condition location">${opt(lists.locations, w.location, 'anywhere')}</select>
      <select data-b="${p}.when.night" data-t="tri" aria-label="Condition night"><option value=""${w.night === undefined ? ' selected' : ''}>day or night</option><option value="true"${w.night === true ? ' selected' : ''}>night</option><option value="false"${w.night === false ? ' selected' : ''}>day</option></select>
      <label class="bv-note">mood</label>${num(`${p}.when.mood_min`, w.mood_min, 0, 100, 'opt-int', 'placeholder="0"')}–${num(`${p}.when.mood_max`, w.mood_max, 0, 100, 'opt-int', 'placeholder="100"')}
      <label class="bv-note">bond ≥</label>${num(`${p}.when.bond_min`, w.bond_min, 0, 1000, 'opt-int', 'placeholder="0"')}
      </div></details><div class="bv-reaction-actions"><button data-act="up" ${i ? '' : 'disabled'} title="Earlier">${W('↑', 'Earlier')}</button><button data-act="down" ${i < count - 1 ? '' : 'disabled'} title="Later">${W('↓', 'Later')}</button><button data-act="copy" title="Duplicate">${W('⧉', 'Duplicate')}</button><button data-act="remove" title="Remove">${W('✕', 'Remove')}</button></div></div>`;
  }

  /* ---------- stimulus simulator ---------- */
  function simPet() {
    const v = V(), names = stateNames();
    let ticks; try { ticks = BigInt(sim.ticks); } catch { ticks = 0n; }
    return {id: Number(sim.id) >>> 0, ticks, location: Math.max(0, v.lists.locations.indexOf(sim.petLocation)), night: !!sim.night,
      mood: Number(sim.mood) || 0, bond: Number(sim.bond) || 0, asleep: !!sim.asleep, busy: !!sim.busy,
      behavior: names.indexOf(sim.behavior), cooldown: names.indexOf(sim.cooldown)};
  }
  function simulate(pet) {
    const v = V(), rep = repertoireOf(sim.form), value = Sim.stimulusValue(sim.stimulus, sim, v);
    return {rep, value, ...Sim.deliver(bwork, v, rep?.reactions || [], pet, sim.stimulus, value)};
  }
  function outcomeText(o) {
    if (o.kept) return `<b>${esc(o.kept)}</b> keeps running: only its ends_on stimuli or its timer end it, so this stimulus is ignored.`;
    const r = o.result, lead = o.ended ? `${esc(sim.stimulus)} ends <b>${esc(o.ended)}</b> (it is in its ends_on), which starts that state's cooldown. Then ` : '';
    if (r.gate) return `${lead}${esc(r.gate)}`;
    if (!r.total) return `${lead}no reaction fits, so nothing happens.`;
    const row = r.rows[r.chosen], e = row.encoded;
    const pick = `weighted pick ${r.pick} of 0–${r.total - 1} chooses reaction ${r.chosen + 1} (${esc(row.reaction.state)}, weight ${e.weight})`;
    return r.entered ? `${lead}${pick}; chance roll ${r.chanceRoll} &lt; ${e.chance_pct}: enters <b>${esc(r.entered.name)}</b> for ${r.entered.seconds} s.`
      : `${lead}${pick}; chance roll ${r.chanceRoll} ≥ ${e.chance_pct}: nothing happens this time.`;
  }
  function odds(pet, seconds) {
    const tally = {};
    for (let k = 0; k < seconds; k++) {
      const o = simulate({...pet, ticks: pet.ticks + BigInt(k * 10)}), name = o.kept ? `${o.kept} (kept)` : o.result?.entered?.name || 'nothing';
      tally[name] = (tally[name] || 0) + 1;
    }
    return Object.entries(tally).sort((a, b) => b[1] - a[1]);
  }
  function renderSimulator(body) {
    const v = V(), lists = v.lists, names = stateNames();
    if (!v.forms.some(f => f.name === sim.form)) sim.form = (v.forms.find(f => repertoireOf(f.name)) || v.forms[0])?.name;
    sim.moment ??= v.moments[0];
    const pet = simPet(), o = simulate(pet), r = o.result, rep = o.rep;
    const stimValue = (() => {
      const s = sim.stimulus;
      if (s === 'need_low') return `<select data-sim="need">${opt(lists.needs, sim.need)}</select>`;
      if (s === 'location') return `<select data-sim="location">${opt(lists.locations, sim.location)}</select>`;
      if (/^activity_/.test(s)) return `<select data-sim="activityKind">${opt(['moment', 'health', 'care'], sim.activityKind)}</select>` +
        (sim.activityKind === 'moment' ? `<select data-sim="moment">${opt(v.moments, sim.moment)}</select>` : sim.activityKind === 'health' ? `<select data-sim="health">${opt(lists.health, sim.health)}</select>` : `<select data-sim="care">${opt(lists.care, sim.care)}</select>`);
      if (/^present_/.test(s)) return `<select data-sim="prize" ${sim.plainGift && s === 'present_given' ? 'disabled' : ''}>${opt([...Array(L().prize_max + 1).keys()], sim.prize)}</select>${s === 'present_given' ? `<label><input type="checkbox" data-sim="plainGift" ${sim.plainGift ? 'checked' : ''}> plain gift</label>` : ''}`;
      if (s === 'touched' || s === 'woke') return `<label class="bv-note">${s === 'touched' ? 'reaction level' : 'wake mood'}</label><input type="number" data-sim="level" min="0" max="255" value="${sim.level}">`;
      return '<span class="bv-note">no value</span>';
    })();
    const rows = r?.rows?.map((row, i) => {
      const e = row.encoded, cls = i === r.chosen ? 'hit' : row.reason ? 'miss' : '';
      return `<tr class="${cls}"><td>${i + 1}</td><td>${esc(row.reaction.on)}</td><td>${e.value === Sim.ANY ? 'any' : e.value}</td><td>${esc(row.reaction.state)}</td><td>${e.weight}</td><td>${row.reason ? '—' : `${row.from}–${row.to - 1}`}</td><td>${e.chance_pct}%</td><td>${row.reason ? esc(row.reason) : i === r.chosen ? 'picked' : 'fits'}</td></tr>`;
    }).join('') || '';
    const entered = r?.entered ? bwork.states[r.entered.index] : null, shown = o.kept || r?.entered?.name || null;
    const tally = odds(pet, 600), total = 600;
    body.innerHTML = `<p class="studio-note bv-test-intro">Choose a creature and event to test your current edits. This simulator explains rule selection; <b>Test in game</b> previews saved content with the real game engine.</p><div class="bv-grid sm"><div>
      <div class="bv-field"><label>Form</label><select data-sim="form">${opt(v.forms.map(f => f.name), sim.form)}</select>
        <span class="bv-note">${rep ? `repertoire ${esc(rep.name)} · ${(rep.reactions || []).length} reactions` : 'no repertoire: never reacts'}</span></div>
      <div class="bv-field"><label>Stimulus</label><select data-sim="stimulus">${opt(lists.stimuli, sim.stimulus)}</select>${stimValue}<span class="bv-note">value ${o.value}${o.value === Sim.ANY ? ' (any)' : ''}</span></div>
      <div class="bv-field"><label>Pet at</label><select data-sim="petLocation">${opt(lists.locations, sim.petLocation)}</select>
        <label><input type="checkbox" data-sim="night" ${sim.night ? 'checked' : ''}> night</label>
        <label>Mood</label><input type="number" data-sim="mood" min="1" max="100" value="${sim.mood}">
        <label>Bond</label><input type="number" data-sim="bond" min="0" max="1000" value="${sim.bond}">
        <label><input type="checkbox" data-sim="asleep" ${sim.asleep ? 'checked' : ''}> asleep</label><label><input type="checkbox" data-sim="busy" ${sim.busy ? 'checked' : ''}> busy</label></div>
      <div class="bv-field"><label>Running state</label><select data-sim="behavior">${opt(names, sim.behavior, 'none')}</select>
        <label>Cooling down</label><select data-sim="cooldown">${opt(names, sim.cooldown, 'none')}</select></div>
      <div class="bv-field"><label>Pet ID</label><input type="number" data-sim="id" min="0" value="${sim.id}">
        <label>Tick</label><input type="text" data-sim="ticks" value="${esc(sim.ticks)}" style="width:150px" aria-label="Pet tick count (the roll seed)">
        <button data-sim-step="10">+1 s</button><button data-sim-step="600">+1 min</button><button data-sim-step="random">Random</button></div>
      <p class="bv-outcome">${outcomeText(o)}</p>
      ${entered ? `<p class="studio-note">On entry: ${Object.entries(entered.effects || {}).map(([k, x]) => `${esc(k)} ${x > 0 ? '+' : ''}${x}`).join(', ') || 'no need changes'}${entered.request ? ` · asks for ${esc(entered.request.command)}${entered.request.health ? ` (${esc(entered.request.health)})` : ''}, +${entered.request.bond ?? 0} bond if answered` : ''}.</p>` : ''}
      ${rows ? `<table class="bv-table"><thead><tr><th>#</th><th>on</th><th>value</th><th>state</th><th>weight</th><th>pick range</th><th>chance</th><th>fit</th></tr></thead><tbody>${rows}</tbody></table>` : ''}
      <div class="bv-k" style="margin-top:10px">Over the next ${total} behaviour seconds (same stimulus, tick +10 each)</div>
      <div class="bv-odds">${tally.map(([n, c]) => `<div><span>${esc(n)}</span><i style="width:${Math.max(1, c / total * 100)}%"></i><span>${(c / total * 100).toFixed(1)}%</span></div>`).join('')}</div>
      <p class="studio-note">Mirrors core/behavior.c: roll() from pet ID, tick and stimulus; reaction_fits(); the weighted pick; the chance roll; the duration draw. Night is set here directly; the game derives it from the pet clock and the night bounds.</p></div>
      <div class="bv-preview" id="bv-sim-preview"><div class="bv-k">${shown ? `Look: ${esc(shown)}` : 'No state: idle look'}</div></div></div>`;
    addPreview(body.querySelector('#bv-sim-preview'), Math.min(320, window.innerWidth - 80), () => ({art: artOf(sim.form), state: shown, mess: state.bMess}));
    body.querySelectorAll('[data-sim]').forEach(x => {
      x.onchange = () => { sim[x.dataset.sim] = x.type === 'checkbox' ? x.checked : x.value; store.set('behaviour-sim', sim); started = performance.now(); render(); };
    });
    body.querySelectorAll('[data-sim-step]').forEach(x => {
      x.onclick = () => {
        const step = x.dataset.simStep;
        let t; try { t = BigInt(sim.ticks); } catch { t = 0n; }
        sim.ticks = String(step === 'random' ? BigInt(Math.floor(Math.random() * 2 ** 40)) : t + BigInt(step));
        store.set('behaviour-sim', sim); started = performance.now(); render();
      };
    });
  }

  /* ---------- save: both files, validated together by the engine ---------- */
  let saveState = 'idle';
  S.saveContent = async () => {
    if (saveState === 'saving') return;
    sync();
    const docs = {}, bases = {};
    if (bdirty()) { docs.behaviors = clone(bwork); bases.behaviors = bbase; }
    if (S.creatureDoc.looksDirty()) { docs.creatures = S.creatureDoc.merged(['state_presentation']); bases.creatures = S.creatureDoc.base(); }  // the looks only
    if (!Object.keys(docs).length || (docs.creatures && S.creatureDoc.saving)) return;
    const issues = allIssues();
    if (issues.length) return S.status(`Fix before saving: ${issues[0]}`, 'bad', true);
    saveState = 'saving'; if (docs.creatures) S.creatureDoc.saving = true; render();
    try {
      const res = await S.saveGuarded(current => S.api('POST', '/api/content', {docs, bases: {...bases, ...Object.fromEntries(Object.keys(docs).map(n => [n, current?.[n] ?? bases[n]]))}, artist: state.artist}), 'Content');
      if (!res) return;  // Cancel keeps the edits and the newer files
      if (docs.behaviors) { D.behavior_data = docs.behaviors; bloaded = clone(docs.behaviors); D.behavior_data_sha = bseen = bbase = res.shas.behaviors; bstale = false; }
      if (docs.creatures) S.creatureDoc.savedSection(docs.creatures, res.shas.creatures, ['state_presentation']);
      if (!ownDirty()) window.JelliDrafts?.drop('behaviour', 'content');
      D.version = res.version;
      render(); S.creatureDoc.refresh();
      const files = res.changed.map(n => `${n}.json`).join(' and ') || 'nothing (no changes)';
      if (res.git_error) S.status(`Saved ${files}, but the commit failed: ${res.git_error}`, 'bad', true);
      else S.status(res.commit ? `Saved ${files} (commit ${res.commit}).` : `Saved ${files}.`, '', false, {keep: true});
    } catch (err) { saveState = 'error'; S.status(`Save failed: ${err.message}`, 'bad', true); }
    finally { if (docs.creatures) S.creatureDoc.saving = false; if (saveState === 'saving') saveState = 'idle'; render(); }
  };

  /* ---------- hooks into the page ---------- */
  const baseRenderView = renderView, baseSelect = select, baseKeydown = S.keydown;
  renderView = () => {  // eslint-disable-line no-global-assign
    const on = state.view === 'behaviour', el = document.getElementById('behaviour');
    el.classList.toggle('hidden', !on);
    if (!on) { previews = []; return baseRenderView(); }
    window.JelliShell?.showOnly('behaviour');
    document.querySelectorAll('#views button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.view === 'behaviour')));
    document.querySelector('main').scrollTop = 0;
    render();
  };
  select = key => { if (state.view === 'behaviour') state.view = 'detail'; baseSelect(key); };  // eslint-disable-line no-global-assign
  S.keydown = e => {
    if (state.view !== 'behaviour') return baseKeydown(e);
    const mod = e.metaKey || e.ctrlKey;
    if (mod && e.key.toLowerCase() === 's') { e.preventDefault(); S.saveContent(); return true; }
    return !mod;  // other single-key shortcuts belong to the detail and creature views
  };
  document.addEventListener('visibilitychange', () => { if (!document.hidden && state.view === 'behaviour') redraw(); });

  /* ---------- drafts: unsaved states, repertoires, rules and looks survive a reload or a Safari tab eviction ---------- */
  const draftBase = () => `${bbase}:${S.creatureDoc.base()}`;
  function restoreDraft() {
    const Drafts = window.JelliDrafts, d = Drafts?.restore('behaviour', 'content', `${D.behavior_data_sha}:${D.creature_data_sha}`); if (!d) return;
    sync();
    const {behaviors, looks, bases} = d.data || {};
    if (!bwork || !S.creatureDoc.work || !behaviors || typeof behaviors !== 'object' || !(looks === undefined || Array.isArray(looks))) return Drafts.drop('behaviour', 'content');
    bwork = behaviors; S.creatureDoc.setLooks(looks);
    if (!ownDirty()) return Drafts.drop('behaviour', 'content');
    if (d.stale) { bstale = true; bbase = bases?.b ?? bbase; S.creatureDoc.setBase(bases?.c ?? S.creatureDoc.base()); }  // Save then answers 409 and asks before overwriting; Revert loads the newer files
    window.JelliShell?.notify(`Restored unsaved behaviour edits from your last visit${d.stale ? ' (a content file changed on disk since)' : ''}.`,
      {tone: 'warn', id: 'drafts-behaviour', hint: d.stale ? 'Save asks before it overwrites the newer files; Revert discards your edits.' : 'Save to keep them, or Revert to discard them.'});
  }
  const baseBoot = S.boot;
  S.boot = async () => {
    await baseBoot();
    restoreDraft();
    window.JelliDrafts?.track('behaviour', 'content', {dirty: ownDirty, base: draftBase,
      data: () => ({behaviors: bwork, looks: S.creatureDoc.work?.state_presentation, bases: {b: bbase, c: S.creatureDoc.base()}})});
  };
})();

/* Jelli Art shell: the frame every panel shares. Loaded first, before studio.js, by
 * jelli_art.py and by compare_slice.py for the static review page, so it must work
 * without window.Studio. It owns the mode tabs (Review / Paint / Creature / Behaviour and
 * any extra view), the polite live region and toasts, an accessible dialog, the unsaved
 * changes indicator, themes, the shortcut registry with its ? overlay, and the
 * first-run guide. Panels use window.JelliShell; see tools/jelli-art/README.md. */
'use strict';
(() => {
  const PREFIX = 'jelli-shell:';
  const local = {
    get(k, d) { try { const v = localStorage.getItem(PREFIX + k); return v === null ? d : JSON.parse(v); } catch { return d; } },
    set(k, v) { try { localStorage.setItem(PREFIX + k, JSON.stringify(v)); } catch { /* storage unavailable */ } }};
  const esc = s => String(s).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
  const MAC = /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent);
  const REVIEW_VIEWS = ['detail', 'sheet'];
  const THEMES = [['auto', 'System'], ['dark', 'Dark'], ['light', 'Light'], ['contrast', 'High contrast']];
  const shortcuts = new Map();  // section -> [{keys: [combo, ...], description}]
  const extraModes = [];        // registerMode() entries
  const dirtyChecks = new Map();  // id -> {label, mode, check}
  const toasts = new Map();     // id -> element
  let tabsKey = '', lastReviewMode = 'side', wasDirty = false, offline = false, toastSeq = 0, currentMode = '';

  /* ---------- shortcut registry ---------- */
  /* registerShortcuts('Paint', [{keys: ['P'], description: 'Pencil'}, {keys: ['Mod+Z'], description: 'Undo'}]).
   * keys lists alternatives; '+' joins a chord; Mod is ⌘ on macOS and Ctrl elsewhere.
   * Registering a section again replaces it. The registry documents keys; handlers stay with their panel. */
  function registerShortcuts(section, list) {
    shortcuts.set(String(section), (list || []).map(s => ({keys: [].concat(s.keys), description: String(s.description || '')})));
  }
  const KEY_NAMES = {Mod: MAC ? '⌘' : 'Ctrl', Shift: MAC ? '⇧' : 'Shift', Alt: MAC ? '⌥' : 'Alt', ArrowUp: '↑', ArrowDown: '↓',
    ArrowLeft: '←', ArrowRight: '→', Space: 'Space', Escape: 'Esc'};
  const combo = c => c.split('+').map(k => `<kbd>${esc(KEY_NAMES[k] || k)}</kbd>`).join(MAC ? '' : '+');

  /* ---------- live region and toasts ---------- */
  let liveTimer = null;
  function announce(text) {
    const el = document.getElementById('shell-live'); if (!el || !text) return;
    el.textContent = ''; clearTimeout(liveTimer);
    liveTimer = setTimeout(() => { el.textContent = text; }, 60);  // a fresh node change so repeats are read again
  }
  const HINTS = [
    [/failed to fetch|networkerror|load failed|did not answer/i, 'Check the terminal running make jelli-art; restart it if it stopped, then try again. Unsaved edits stay in this tab.'],
    [/commit failed/i, 'The file is saved on disk. Fix the git problem, then commit it yourself.'],
    [/changed (since you loaded it|on disk)|revert first/i, 'Revert loads the current file; then make your change again.'],
    [/cmake/i, 'Install CMake and restart Jelli Art: behaviour saves are checked with cmake -P.'],
    [/request too large/i, 'Requests are limited to 1 MiB; save fewer edits at once.']];
  const TONES = {'': 'info', info: 'info', ok: 'ok', good: 'ok', warn: 'warn', bad: 'bad', error: 'bad'};
  /* notify(text, {tone: 'info'|'ok'|'warn'|'bad', sticky, hint, id, timeout}). Errors stay until dismissed.
   * The same id (or the same text) replaces the toast rather than stacking. Returns the id. */
  function notify(text, opts = {}) {
    text = String(text || ''); if (!text) return null;
    const tone = (!opts.tone && /^(Saved|Tidied|Copied|Updated)\b/.test(text) ? 'ok' : TONES[opts.tone ?? '']) || 'info', sticky = opts.sticky ?? tone === 'bad';
    const hint = opts.hint ?? (tone === 'bad' || tone === 'warn' ? HINTS.find(([re]) => re.test(text))?.[1] : '') ?? '';
    const box = document.getElementById('shell-toasts');
    if (!box) { announce(text); return null; }
    const same = [...toasts].find(([, el]) => el.dataset.text === text)?.[0];
    const id = opts.id || same || `toast-${++toastSeq}`;
    let el = toasts.get(id);
    if (!el) {
      el = document.createElement('div'); toasts.set(id, el); box.append(el);
      el.addEventListener('mouseenter', () => clearTimeout(el.timer));
      el.addEventListener('focusin', () => clearTimeout(el.timer));
    }
    el.className = `shell-toast ${tone}`; el.dataset.text = text; el.dataset.mode = currentMode; el.dataset.sticky = String(sticky);
    const label = {info: 'Note', ok: 'Done', warn: 'Warning', bad: 'Problem'}[tone];
    el.innerHTML = `<p><span class="shell-tone">${label}:</span> ${esc(text)}</p>${hint ? `<p class="shell-hint">${esc(hint)}</p>` : ''}` +
      '<button type="button" class="shell-dismiss" aria-label="Dismiss notification">×</button>';
    el.querySelector('.shell-dismiss').onclick = () => dismiss(id);
    clearTimeout(el.timer);
    if (!sticky) el.timer = setTimeout(() => dismiss(id), opts.timeout || 6000);
    while (toasts.size > 4) dismiss(toasts.keys().next().value);
    announce(`${label}: ${text}${hint ? ` ${hint}` : ''}`);
    return id;
  }
  /* Switching mode clears passing info and done toasts from the mode left behind; warnings, problems and sticky ones stay. */
  function leaveMode(next) {
    if (currentMode) for (const [id, el] of [...toasts]) if (el.dataset.mode !== next && el.dataset.sticky !== 'true' && /\b(info|ok)\b/.test(el.className)) dismiss(id);
    currentMode = next;
  }
  function dismiss(id) {
    const el = toasts.get(id); if (!el) return;
    toasts.delete(id); clearTimeout(el.timer);
    if (el.contains(document.activeElement)) document.getElementById('main')?.focus();
    el.remove();
  }

  /* ---------- dialog ---------- */
  /* dialog({title, body: html string or Node, actions: [{label, value, primary, danger}], onOpen(el)})
   * resolves with the chosen value, or null for Escape or ×. Focus returns to where it was. */
  function dialog({title, body = '', actions = [{label: 'Close', value: 'close', primary: true}], onOpen, className = '', initial} = {}) {
    const prev = document.activeElement, el = document.createElement('dialog'), id = `shell-dialog-${++toastSeq}`;
    el.className = `shell-dialog ${className}`; el.setAttribute('aria-labelledby', `${id}-title`);
    el.innerHTML = `<form method="dialog"><div class="shell-dialog-head"><h2 id="${id}-title">${esc(title)}</h2>` +
      '<button value="" class="shell-x" aria-label="Close">×</button></div><div class="shell-dialog-body"></div>' +
      `<div class="shell-dialog-actions">${actions.map(a => `<button value="${esc(a.value)}" class="${a.primary ? 'primary' : ''}${a.danger ? ' danger' : ''}">${esc(a.label)}</button>`).join('')}</div></form>`;
    const slot = el.querySelector('.shell-dialog-body');
    if (typeof body === 'string') slot.innerHTML = body; else slot.append(body);
    document.body.append(el);
    el.showModal();
    (initial ? el.querySelector(initial) : el.querySelector('.shell-dialog-actions .primary') || el.querySelector('.shell-x'))?.focus();
    onOpen?.(el);
    return new Promise(resolve => el.addEventListener('close', () => {
      el.remove();
      (prev?.isConnected ? prev : document.getElementById('main'))?.focus?.();
      resolve(el.returnValue || null);
    }));
  }
  const confirmDialog = (message, {title = 'Are you sure?', confirmLabel = 'OK', cancelLabel = 'Cancel', danger = false} = {}) =>
    dialog({title, body: `<p>${esc(message).replace(/\n/g, '<br>')}</p>`, initial: danger ? '[value=cancel]' : undefined,
      actions: [{label: cancelLabel, value: 'cancel'}, {label: confirmLabel, value: 'ok', primary: !danger, danger}]}).then(v => v === 'ok');

  /* ---------- modes ---------- */
  const st = () => (typeof state === 'object' ? state : null);
  function modeList() {
    const s = st(); if (!s) return [];
    const list = [{id: 'review', label: 'Review', panel: () => (REVIEW_VIEWS.includes(s.view) ? s.view : 'detail'),
      active: () => REVIEW_VIEWS.includes(s.view) && s.mode !== 'paint',
      activate: () => { if (s.mode === 'paint') s.mode = lastReviewMode; if (!REVIEW_VIEWS.includes(s.view)) s.view = 'detail'; }}];
    if (window.Studio && MODES.includes('paint')) list.push({id: 'paint', label: 'Paint', panel: () => 'detail',
      active: () => s.view === 'detail' && s.mode === 'paint', activate: () => { s.view = 'detail'; s.mode = 'paint'; }});
    for (const b of document.querySelectorAll('#views [data-view]')) {
      const v = b.dataset.view; if (REVIEW_VIEWS.includes(v)) continue;
      list.push({id: v, label: b.textContent.trim(), panel: () => v, active: () => s.view === v, activate: () => { s.view = v; }});
    }
    for (const m of extraModes) if (!list.some(x => x.id === m.id)) list.push(m);
    return list;
  }
  /* registerMode({id, label, view}) adds a tab for a panel shown when state.view === view; or pass
   * {id, label, panel(), active(), activate()} for anything else. A #views button is picked up too. */
  function registerMode(m) {
    const s = () => st() || {};
    extraModes.push({panel: () => m.view, active: () => s().view === m.view, activate: () => { s().view = m.view; }, ...m});
    sync();
  }
  function activateMode(id, focus = false) {
    const m = modeList().find(x => x.id === id); if (!m) return;
    m.activate(); renderView(); savePrefs();
    if (focus) document.getElementById(`tab-${id}`)?.focus();
  }
  function buildTabs(list) {
    const nav = document.getElementById('shell-modes'), bar = nav.querySelector('[role=tablist]');
    bar.innerHTML = list.map(m => `<button type="button" role="tab" id="tab-${esc(m.id)}" data-mode-id="${esc(m.id)}">${esc(m.label)}<span class="shell-tab-dirty" hidden> ●<span class="sr-only"> (unsaved)</span></span></button>`).join('');
    nav.classList.toggle('hidden', list.length < 2);
  }
  /* Bring tabs, panels, title and the review sub-switch in line with state. Cheap; runs after every render. */
  function sync() {
    const s = st(), list = modeList(); if (!s || !list.length) return;
    const key = list.map(m => `${m.id}:${m.label}`).join('|');
    if (key !== tabsKey) { tabsKey = key; buildTabs(list); }
    if (s.mode !== 'paint' && s.mode) lastReviewMode = s.mode;
    const current = list.find(m => m.active()) || list[0];
    if (current.id !== currentMode) leaveMode(current.id);
    for (const m of list) {
      const tab = document.getElementById(`tab-${m.id}`); if (!tab) continue;
      const on = m === current, panel = m.panel();
      tab.setAttribute('aria-selected', String(on)); tab.tabIndex = on ? 0 : -1; tab.setAttribute('aria-controls', panel);
      const el = document.getElementById(panel);
      if (el && list.length > 1 && (on || !el.hasAttribute('aria-labelledby'))) { el.setAttribute('role', 'tabpanel'); el.setAttribute('aria-labelledby', tab.id); }
    }
    for (const b of document.querySelectorAll('#views [data-view]')) b.classList.toggle('hidden', !REVIEW_VIEWS.includes(b.dataset.view) && list.length > 1);
    document.getElementById('views')?.classList.toggle('hidden', current.id !== 'review');
    document.querySelector('#modes [data-mode=paint]')?.classList.add('hidden');  // the Paint tab replaces it
    document.getElementById('modes')?.parentElement.classList.toggle('hidden', current.id === 'paint');
    const a = typeof byKey === 'object' ? byKey[s.key] : null;
    const where = ['review', 'paint'].includes(current.id) && a ? ` · ${a.key}` : '';
    document.title = `${current.label}${where} — ${D.live ? 'Jelli Art' : 'Jelligotchi pixel review'}`;
    updateDirty();
  }
  function wireTabs() {
    const bar = document.querySelector('#shell-modes [role=tablist]');
    bar.addEventListener('click', e => { const t = e.target.closest('[role=tab]'); if (t) activateMode(t.dataset.modeId); });
    bar.addEventListener('keydown', e => {
      const tabs = [...bar.querySelectorAll('[role=tab]')], i = tabs.indexOf(document.activeElement);
      const next = {ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: tabs.length - 1}[e.key];
      e.stopPropagation();  // arrows and letters here never reach the page shortcuts
      if (next === undefined || i < 0) return;
      e.preventDefault();
      activateMode(tabs[(next + tabs.length) % tabs.length].dataset.modeId, true);
    });
  }

  /* ---------- unsaved changes ---------- */
  /* registerDirty(id, {label, mode, check}): check() returns a count or boolean of unsaved edits. */
  function registerDirty(id, entry) { dirtyChecks.set(id, entry); updateDirty(); }
  function dirtyItems() {
    const out = [];
    for (const [id, d] of dirtyChecks) {
      let n = 0; try { n = Number(d.check()) || 0; } catch { n = 0; }
      if (n) out.push({id, n, ...d});
    }
    return out;
  }
  function updateDirty() {
    const items = dirtyItems(), btn = document.getElementById('shell-unsaved'); if (!btn) return;
    btn.classList.toggle('hidden', !items.length);
    btn.textContent = items.length ? `● Unsaved: ${items.map(i => i.label + (i.n > 1 ? ` (${i.n})` : '')).join(', ')}` : '';
    const modes = new Set(items.map(i => i.mode));
    document.querySelectorAll('#shell-modes [role=tab]').forEach(t => { t.querySelector('.shell-tab-dirty').hidden = !modes.has(t.dataset.modeId); });
    if (items.length && !wasDirty) announce('You have unsaved changes.');
    wasDirty = !!items.length;
  }
  async function showUnsaved() {
    const items = dirtyItems();
    const rows = items.map(i => `<li>${esc(i.label)}${i.n > 1 ? ` (${i.n})` : ''}${i.detail ? `: ${esc(i.detail())}` : ''}` +
      (i.mode ? ` <button type="button" value="${esc(i.mode)}" class="shell-go">Open ${esc(modeList().find(m => m.id === i.mode)?.label || i.mode)}</button>` : '') + '</li>').join('');
    const choice = await dialog({title: 'Unsaved changes', body: `<p>These edits exist only in this tab. Save each one in its mode; the page warns before you close it.</p><ul>${rows}</ul>`,
      onOpen: el => el.querySelectorAll('.shell-go').forEach(b => { b.onclick = () => el.close(b.value); })});
    if (choice && choice !== 'close') activateMode(choice, true);
  }

  /* ---------- theme ---------- */
  function setTheme(theme) {
    if (theme === 'auto') delete document.documentElement.dataset.theme; else document.documentElement.dataset.theme = theme;
    local.set('theme', theme);
    const sel = document.getElementById('shell-theme'); if (sel) sel.value = theme;
  }

  /* ---------- help overlay and guide ---------- */
  function openHelp() {
    if (document.querySelector('dialog[open]')) return;
    const id = document.querySelector('#shell-modes [aria-selected=true]')?.dataset.modeId, current = modeList().find(m => m.id === id)?.label;
    const sections = [...shortcuts].sort(([a], [b]) => (b === current) - (a === current) || (b === 'Everywhere') - (a === 'Everywhere'));
    const body = sections.map(([name, list]) => `<section><h3>${esc(name)}${name === current ? ' <span class="lbl">· this mode</span>' : ''}</h3><dl class="shell-keys">` +
      list.map(s => `<dt>${s.keys.map(combo).join(' <span class="lbl">or</span> ')}</dt><dd>${esc(s.description)}</dd>`).join('') + '</dl></section>').join('');
    // Focus the × at the top so the list opens at its start, not scrolled to the Close button.
    dialog({title: 'Keyboard shortcuts', body, className: 'shell-help', initial: '.shell-x', onOpen: el => { el.scrollTop = 0; el.querySelector('.shell-dialog-body').scrollTop = 0; }});
  }
  function guideHtml() {
    const live = D.live, git = D.git || {};
    const modes = modeList().map(m => m.label);
    const what = {Review: 'compare each asset with an earlier commit: side by side, swipe, flip, onion skin or a pixel diff, and mark it good or needs work.',
      Paint: 'edit the pixels of the selected asset; a creature frame also gets a flip-book beside the canvas to play its clip, step frames with ← and →, and onion skin its neighbours.',
      Creature: 'set the frames, timing and loop of each pose clip, and each form\'s size and behaviour.',
      Behaviour: 'edit behaviour states and reactions, and try them in the stimulus simulator.',
      'Test in game': 'render a scenario (pet, form, needs, page, clock) with the real game engine and step through its frames; save first, since unsaved edits are not included.',
      Activities: 'edit the activity recipes: which forms can do them, opening times, meter costs and rewards, locations, icon, prop and motion.'};
    const saving = !live ? '<p>This is a static review page: notes stay in this browser. Use <b>Copy review notes</b> to share them.</p>'
      : git.enabled ? `<p><b>Saving.</b> Each mode has its own Save (Mod+S). The studio checks the file, writes it, and commits it to <code>${esc(git.branch)}</code>${git.push ? ', then pushes it for review' : ''}. Commit and push results appear here and in the header.</p>`
        : '<p><b>Saving.</b> Each mode has its own Save button (⌘S / Ctrl+S). The studio checks the change, then writes it into this checkout. Nothing is committed: check <code>git diff</code> and commit when you are happy.</p>';
    return `<h2 id="shell-guide-title">Getting started</h2><p>Pick an asset in the list on the left (or press J and K), then choose a mode:</p>
      <ul>${modes.map(m => `<li><b>${esc(m)}</b>${what[m] ? `: ${what[m]}` : ''}</li>`).join('')}</ul>${saving}
      <p>Unsaved edits show as <span class="shell-warn">● Unsaved</span> in the header. Press <kbd>?</kbd> for every keyboard shortcut.</p>
      <button type="button" id="shell-guide-close">Hide this guide</button>`;
  }
  function showGuide(show) {
    const el = document.getElementById('shell-guide'); if (!el) return;
    el.hidden = !show; local.set('guide', show);
    if (!show) return;
    el.innerHTML = guideHtml();
    el.querySelector('#shell-guide-close').onclick = () => { showGuide(false); document.getElementById('shell-guide-button')?.focus(); };
  }

  /* ---------- page furniture ---------- */
  function insertChrome() {
    const header = document.querySelector('header');
    if (D.live) header.querySelector('h1').innerHTML = '<span class="mark" aria-hidden="true"></span><b>Jelli Art</b> <span>studio</span>';
    header.querySelector('h1').insertAdjacentHTML('afterend', '<nav id="shell-modes" class="shell-modes hidden" aria-label="Modes"><div role="tablist" aria-label="Modes"></div></nav>' +
      '<div class="shell-tools"><button type="button" id="shell-unsaved" class="shell-unsaved hidden" aria-haspopup="dialog"></button>' +
      '<button type="button" id="shell-help-button" aria-haspopup="dialog" aria-keyshortcuts="Shift+?">Shortcuts <kbd>?</kbd></button>' +
      '<button type="button" id="shell-guide-button" aria-controls="shell-guide">Guide</button>' +
      `<label class="lbl" for="shell-theme">Theme</label><select id="shell-theme">${THEMES.map(([v, t]) => `<option value="${v}">${t}</option>`).join('')}</select></div><div class="shell-break"></div>`);
    document.body.insertAdjacentHTML('beforeend', '<div id="shell-live" class="sr-only" role="status" aria-live="polite" aria-atomic="true"></div>' +
      '<section id="shell-toasts" class="shell-toasts" aria-label="Notifications"></section>');
    document.getElementById('main').insertAdjacentHTML('afterbegin', '<section id="shell-guide" class="shell-guide" aria-labelledby="shell-guide-title" tabindex="-1" hidden></section>');
    const studioStatus = document.getElementById('studio-status');  // studio.js routes status() here instead
    if (studioStatus) { studioStatus.hidden = true; studioStatus.removeAttribute('role'); }
    document.getElementById('shell-unsaved').onclick = showUnsaved;
    document.getElementById('shell-help-button').onclick = openHelp;
    document.getElementById('shell-guide-button').onclick = () => { showGuide(document.getElementById('shell-guide').hidden); document.getElementById('shell-guide').focus?.(); };
    const theme = document.getElementById('shell-theme'); theme.value = local.get('theme', 'auto'); theme.onchange = () => setTheme(theme.value);
    document.querySelector('.skip-link')?.addEventListener('click', e => { e.preventDefault(); document.getElementById('main').focus(); });
  }
  function watchGit() {
    const el = document.getElementById('git-status'); if (!el) return;
    let failing = false;
    new MutationObserver(() => {
      const bad = el.classList.contains('bad');
      if (bad && !failing) notify(`Git push is failing: ${el.title || 'see the server log'}`, {tone: 'warn', id: 'git-push', sticky: true,
        hint: 'Saves are still committed locally and the push retries; check the network and deploy key.'});
      if (!bad && failing) { dismiss('git-push'); notify('Git push recovered; commits are on GitHub.', {tone: 'ok'}); }
      failing = bad;
    }).observe(el, {attributes: true, childList: true, characterData: true, subtree: true});
  }
  function watchServer() {
    if (!D.live || !window.fetch) return;
    const base = window.fetch.bind(window);
    window.fetch = async (input, init) => {
      const api = String(input?.url || input).startsWith('/api/');
      try {
        const res = await base(input, init);
        if (api && offline) { offline = false; dismiss('offline'); notify('Reconnected to the Jelli Art server.', {tone: 'ok'}); }
        return res;
      } catch (err) {
        if (api && !offline) { offline = true; notify('The Jelli Art server did not answer, so saves and live reload are paused.', {tone: 'bad', id: 'offline'}); }
        throw err;
      }
    };
  }
  /* Elements with role=button that are not <button> get Enter and Space, as a button would. */
  function keyboardButtons(e) {
    const el = e.target; if (!(e.key === 'Enter' || e.key === ' ') || el.tagName === 'BUTTON' || el.getAttribute?.('role') !== 'button' || el.onkeydown) return;
    e.preventDefault(); e.stopPropagation(); el.click();
  }
  function globalKeys(e) {
    if (e.key !== '?' || e.metaKey || e.ctrlKey || e.altKey || e.target.closest?.('input, textarea, select, [contenteditable], dialog')) return;
    e.preventDefault(); e.stopPropagation(); openHelp();
  }

  /* ---------- default shortcut docs (panels replace their section by registering it again) ---------- */
  registerShortcuts('Everywhere', [{keys: ['?'], description: 'Show these shortcuts'}, {keys: ['Tab', 'Shift+Tab'], description: 'Move between controls'},
    {keys: ['ArrowLeft', 'ArrowRight'], description: 'Switch mode when a mode tab has focus'}, {keys: ['Escape'], description: 'Close a dialog'}]);
  registerShortcuts('Review', [{keys: ['J', 'ArrowDown'], description: 'Next asset'}, {keys: ['K', 'ArrowUp'], description: 'Previous asset'},
    {keys: ['1 … 5'], description: 'Compare: side by side, swipe, flip, onion, diff'}, {keys: [']', '='], description: 'Zoom in'}, {keys: ['[', '-'], description: 'Zoom out'},
    {keys: ['0'], description: 'Fit to window'}, {keys: ['D'], description: 'Device scale'}, {keys: ['G'], description: 'Grid'}, {keys: ['I'], description: 'Issue overlay'},
    {keys: ['B'], description: 'Next backdrop'}, {keys: ['Space'], description: 'Hold to peek at the before image'}, {keys: ['A'], description: 'Mark looks good'},
    {keys: ['X'], description: 'Mark needs work'}, {keys: ['S'], description: 'Switch between detail and sheet'}]);
  registerShortcuts('Paint', [{keys: ['P'], description: 'Pencil'}, {keys: ['E'], description: 'Eraser'}, {keys: ['F'], description: 'Fill'},
    {keys: ['C'], description: 'Pick colour'}, {keys: ['M'], description: 'Mirror left/right'}, {keys: ['Mod+Z'], description: 'Undo'},
    {keys: ['Mod+Shift+Z', 'Mod+Y'], description: 'Redo'}, {keys: ['Mod+S'], description: 'Save the asset'}, {keys: ['Space'], description: 'Hold to peek at the before image'}]);
  registerShortcuts('Creature', [{keys: ['Space'], description: 'Play or pause'}, {keys: [',', 'ArrowLeft'], description: 'Previous frame'},
    {keys: ['.', 'ArrowRight'], description: 'Next frame'}, {keys: ['J', 'ArrowDown'], description: 'Next pose'}, {keys: ['K', 'ArrowUp'], description: 'Previous pose'},
    {keys: ['1 … 9'], description: 'Choose a pose'}, {keys: ['Mod+S'], description: 'Save clips and behaviour & size'}]);
  registerShortcuts('Behaviour', [{keys: ['Mod+S'], description: 'Save behaviour (both files)'}]);

  window.JelliShell = {registerShortcuts, notify, dismiss, announce, dialog, confirm: confirmDialog, registerMode, registerDirty,
    activateMode, openHelp, setTheme, sync, showGuide, shortcuts: () => new Map(shortcuts)};

  function init() {
    if (!document.querySelector('header') || !document.getElementById('main')) return;
    insertChrome();
    for (const name of ['renderView', 'renderDetail']) {
      const base = window[name]; if (typeof base !== 'function') continue;
      window[name] = function (...args) { const r = base.apply(this, args); sync(); return r; };
    }
    const S = window.Studio;
    if (S?.paintDirty) registerDirty('paint', {label: 'Paint', mode: 'paint',
      check: () => D.assets?.filter(a => S.paintDirty(a.key)).length || 0,
      detail: () => D.assets.filter(a => S.paintDirty(a.key)).map(a => a.key).join(', ')});
    if (S?.clipsDirty) registerDirty('content', {label: 'Creature or behaviour data', mode: null, check: () => S.clipsDirty()});
    wireTabs(); watchGit(); watchServer();
    document.addEventListener('keydown', keyboardButtons, true);
    document.addEventListener('keydown', globalKeys, true);
    window.addEventListener('beforeunload', e => { if (dirtyItems().length) { e.preventDefault(); e.returnValue = ''; } });
    setInterval(() => { if (!document.hidden) updateDirty(); }, 1000);
    document.addEventListener('pointerup', () => setTimeout(updateDirty, 0));
    document.addEventListener('keyup', () => setTimeout(updateDirty, 0));
    showGuide(local.get('guide', true));
    sync();
  }
  window.addEventListener('DOMContentLoaded', init);
})();

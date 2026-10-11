/* Jelli Art touch and Pencil foundation (RFC-007 T0). Loaded right after shell.js by jelli_art.py
 * (studio only; the static review page does not load it). It owns pointer classification, the
 * canvas gesture recogniser, the "Pencil only" switch, coarse-pointer sizing, keyboard-hint hiding,
 * the long-press explanation popover, tap-to-reveal ⓘ help, the asset-list drawer and the
 * "Touch and Pencil" help section. Panels use window.JelliTouch:
 *
 *   T                         thresholds below (RFC-007 §1); tests read the same values
 *   kind(e)                   'pen' | 'touch' | 'mouse' for a PointerEvent
 *   coarse()                  true while any pointer is coarse (a finger can reach the page)
 *   pencilOnly()              the viewer's "Pencil only" switch (stored per viewer, off by default)
 *   setPencilOnly(on)         set it; fires onPencilOnly listeners and announces the change
 *   onPencilOnly(fn)          fn(on) after every change; returns an unsubscribe function
 *   offerPencilOnly()         once per viewer: a toast offering to turn Pencil only on
 *   gestures(handlers, opts)  a recogniser; see "gesture recogniser" below
 *   registerGestures(list)    adds [{gesture, description}] to the help dialog's Touch and Pencil
 *                             section, merged by gesture name (later entries replace earlier ones)
 *   info(text, label)         HTML for a tap-to-reveal ⓘ button that shows text in the popover
 *   popover(text, anchor)     shows text in the popover next to an element; hidePopover() closes it
 *   setList(open)             opens or closes the asset-list drawer (coarse pointers only)
 *
 * Page furniture it adds: `html[data-keys]` after the first key press outside a text field (CSS hides
 * <kbd> hints in buttons on coarse pointers until then); a long press (T.LONG_PRESS_MS) on any
 * control with a title shows that title in the popover (put `data-touch-surface` on canvases and
 * other elements that handle their own long press); class `jt-hit` grows an element's hit area by
 * --hit-pad without changing its look; tokens --tap, --tap-gap and --hit-pad on :root. */
'use strict';
(() => {
  /** Thresholds (RFC-007 §1). Proposals to tune on a physical iPad; the UI tests use the same values. */
  const T = Object.freeze({
    SECOND_FINGER_MS: 250,   // a second finger this soon after the first cancels the first finger's stroke
    PALM_MS: 300,            // a touch this soon after a pen lifts (or while it is down) never paints
    PAN_PX: 8,               // two-finger midpoint movement before a pan starts (one finger with Pencil only)
    PINCH_RATIO: 0.12,       // change in finger distance before a pinch starts
    TAP_DOWN_MS: 100,        // every finger of a multi-finger tap lands within this of the first
    TAP_UP_MS: 350,          // and the last lifts within this of the first landing
    TAP_MOVE_PX: 12,         // and no finger moves further than this
    LONG_PRESS_MS: 500,      // a finger held this long picks (canvas) or explains (control with a title)
    LONG_PRESS_MOVE_PX: 8,   // and moves no further than this
  });
  const PREFIX = 'jelli-touch:';
  const local = {
    get(k, d) { try { const v = localStorage.getItem(PREFIX + k); return v === null ? d : JSON.parse(v); } catch { return d; } },
    set(k, v) { try { localStorage.setItem(PREFIX + k, JSON.stringify(v)); } catch { /* storage unavailable */ } }};
  const shell = () => window.JelliShell;
  const esc = s => String(s).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
  const now = () => performance.now();
  const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

  /* ---------- pointer classification and the Pencil only switch ---------- */
  /** @param {PointerEvent} e */
  const kind = e => (e.pointerType === 'pen' ? 'pen' : e.pointerType === 'touch' ? 'touch' : 'mouse');
  const coarseQuery = window.matchMedia ? matchMedia('(any-pointer: coarse)') : null;
  const coarse = () => !!coarseQuery?.matches;
  const listeners = new Set();
  const pencilOnly = () => !!local.get('pencil-only', false);
  /** @param {boolean} on */
  function setPencilOnly(on) {
    on = !!on; if (on === pencilOnly()) return;
    local.set('pencil-only', on);
    for (const fn of [...listeners]) fn(on);
    shell()?.announce(on ? 'Pencil only on: fingers pan, zoom and pick; the Pencil paints.' : 'Pencil only off: fingers paint too.');
  }
  /** @param {(on: boolean) => void} fn */
  function onPencilOnly(fn) { listeners.add(fn); return () => { listeners.delete(fn); }; }
  function offerPencilOnly() {
    if (pencilOnly() || local.get('pencil-offered', false) || !shell()) return;
    local.set('pencil-offered', true);
    const text = 'Using an Apple Pencil? Turn on Pencil only so fingers pan, zoom and pick instead of painting.';
    const id = shell().notify(text, {tone: 'info', id: 'pencil-only-offer', timeout: 15000});
    const toast = [...document.querySelectorAll('#shell-toasts .shell-toast')].find(el => /** @type {HTMLElement} */ (el).dataset.text === text);
    if (!toast || !id) return;
    toast.insertAdjacentHTML('beforeend', '<p><button type="button" class="jt-offer">Turn on Pencil only</button></p>');
    /** @type {HTMLElement} */ (toast.querySelector('.jt-offer')).onclick = () => { setPencilOnly(true); shell().dismiss(id); };
  }

  /* ---------- gesture recogniser ----------
   * gestures({stroke: {start(e), move(e), end(e), cancel(e)}, pan(dx, dy), pinch(g), gestureEnd(g), tap(fingers, g),
   *           point(e), longPress(e)}, {pencilOnly: () => bool, offerPencilOnly: bool})
   * returns {handle(e), attach(el), reset(), busy()}. Feed it pointerdown/move/up/cancel (attach(el) does
   * that, captures each pointer and returns a detach function); handle(e) returns what the event became:
   * 'stroke', 'gesture' or 'ignored'.
   * - stroke.*: the mouse, a pen, and one finger while Pencil only is off. A finger stroke is cancelled
   *   (stroke.cancel) when a second finger lands within SECOND_FINGER_MS or a long press fires; after that
   *   window further fingers are ignored until it ends. Touches while a pen is down or within PALM_MS of
   *   it lifting never stroke. A pen landing during a finger gesture ends the gesture first.
   * - pan(dx, dy): incremental movement of the two-finger midpoint (or one finger with Pencil only) once it
   *   has moved PAN_PX; pinch(g) once the finger distance changed by PINCH_RATIO, with g = {scale (since the
   *   gesture began), cx, cy (midpoint now), x0, y0 (midpoint at the start)}; gestureEnd(g) when the last
   *   finger lifts after a pan or pinch, or when a pen ends the gesture. Client coordinates throughout.
   * - tap(2 | 3, g): a multi-finger tap (TAP_DOWN_MS, TAP_UP_MS, TAP_MOVE_PX); point(e): a one-finger tap
   *   with Pencil only on (show that pixel). longPress(e): a finger held LONG_PRESS_MS within
   *   LONG_PRESS_MOVE_PX, with the pointerdown event; the Pencil never long-presses. */
  /**
   * @param {{stroke?: {start?: Function, move?: Function, end?: Function, cancel?: Function}, pan?: Function, pinch?: Function,
   *   gestureEnd?: Function, tap?: Function, point?: Function, longPress?: Function}} handlers
   * @param {{pencilOnly?: () => boolean, offerPencilOnly?: boolean}} [opts]
   */
  function gestures(handlers, opts = {}) {
    const h = handlers || {}, sk = h.stroke || {}, onlyPen = opts.pencilOnly || pencilOnly;
    /** @type {Map<number, {x: number, y: number, x0: number, y0: number, t0: number, moved: number}>} */
    const touches = new Map();
    /** @type {{id: number, kind: string, t0: number} | null} */
    let stroke = null;
    /** @type {any} */
    let gesture = null;  // {ids, first, startMid, startDist, mid, maxFingers, tapOk, panning, pinching, one}
    let penDown = false, penUpAt = -Infinity, pressTimer = 0, pressId = -1;
    const ignored = new Set();  // finger ids that do nothing until they lift
    const clearPress = () => { clearTimeout(pressTimer); pressTimer = 0; pressId = -1; };
    const live = () => [...touches.keys()].filter(id => !ignored.has(id));
    const midOf = ids => { const ps = ids.map(id => touches.get(id)); return {x: ps.reduce((s, p) => s + p.x, 0) / ps.length, y: ps.reduce((s, p) => s + p.y, 0) / ps.length}; };
    const distOf = ids => (ids.length >= 2 ? dist(touches.get(ids[0]), touches.get(ids[1])) : 0);
    const snap = () => ({scale: gesture.scale, cx: gesture.mid.x, cy: gesture.mid.y, x0: gesture.x0, y0: gesture.y0});
    /* Re-anchor after a finger lands or lifts, so the midpoint and distance never jump; scale and the start midpoint carry on. */
    function rebase() {
      const ids = live(); gesture.ids = ids; if (!ids.length) return;
      gesture.mid = midOf(ids); gesture.baseDist = distOf(ids); gesture.baseScale = gesture.scale;
    }
    function startGesture(one) {
      const ids = live(), m = midOf(ids), t0s = ids.map(id => touches.get(id).t0), first = Math.min(...t0s);
      gesture = {ids, first, x0: m.x, y0: m.y, mid: m, startDist: distOf(ids), maxFingers: ids.length,
        tapOk: !one && Math.max(...t0s) - first <= T.TAP_DOWN_MS && ids.every(id => touches.get(id).moved <= T.TAP_MOVE_PX),
        panning: false, pinching: false, one, scale: 1, baseScale: 1, baseDist: distOf(ids)};
    }
    function endGesture() {
      const g = gesture; gesture = null; if (!g) return;
      if (g.panning || g.pinching) h.gestureEnd?.({scale: g.scale, cx: g.mid.x, cy: g.mid.y, x0: g.x0, y0: g.y0});
    }
    function cancelStroke(e) { if (!stroke) return; stroke = null; sk.cancel?.(e); }
    function armPress(e) {
      clearPress(); if (!h.longPress) return;
      pressId = e.pointerId;
      pressTimer = window.setTimeout(() => {
        const p = touches.get(pressId); if (!p || p.moved > T.LONG_PRESS_MOVE_PX || live().length !== 1) return;
        if (stroke?.id === pressId) cancelStroke(e);
        if (gesture?.one) gesture = null;
        ignored.add(pressId); pressTimer = 0;
        h.longPress(e);
      }, T.LONG_PRESS_MS);
    }
    /** @param {PointerEvent} e */
    function down(e) {
      const k = kind(e);
      if (k !== 'touch') {
        if (stroke && !(k === 'pen' && stroke.kind === 'touch')) return 'ignored';
        if (k === 'pen') {  // the Pencil takes over: a finger stroke is cancelled, a gesture ends, fingers down stay inert
          cancelStroke(e); if (gesture) endGesture();
          for (const id of touches.keys()) ignored.add(id);
          penDown = true; if (opts.offerPencilOnly) offerPencilOnly();
        }
        clearPress();
        stroke = {id: e.pointerId, kind: k, t0: now()};
        sk.start?.(e); return 'stroke';
      }
      touches.set(e.pointerId, {x: e.clientX, y: e.clientY, x0: e.clientX, y0: e.clientY, t0: now(), moved: 0});
      // While a pen or mouse stroke runs, fingers are palms. Just after a pen lifts, one never paints, but
      // it stays live so a second finger can still make a two-finger gesture with it.
      if (penDown || (stroke && stroke.kind !== 'touch')) { ignored.add(e.pointerId); return 'ignored'; }
      const ids = live();
      if (ids.length === 1) {
        if (now() - penUpAt < T.PALM_MS) return 'ignored';
        if (onlyPen()) { startGesture(true); armPress(e); return 'gesture'; }
        stroke = {id: e.pointerId, kind: 'touch', t0: now()};
        armPress(e); sk.start?.(e); return 'stroke';
      }
      clearPress();
      if (stroke) {  // a finger stroke: a second finger this soon means a gesture, later it is ignored
        if (now() - stroke.t0 > T.SECOND_FINGER_MS) { ignored.add(e.pointerId); return 'ignored'; }
        cancelStroke(e);
      }
      if (!gesture || gesture.one) startGesture(false);
      else {
        gesture.maxFingers = Math.max(gesture.maxFingers, live().length);
        if (now() - gesture.first > T.TAP_DOWN_MS) gesture.tapOk = false;
        rebase();
      }
      return 'gesture';
    }
    /** @param {PointerEvent} e */
    function move(e) {
      if (stroke?.id === e.pointerId) {
        const p = touches.get(e.pointerId);
        if (p) { p.x = e.clientX; p.y = e.clientY; p.moved = Math.max(p.moved, Math.hypot(p.x - p.x0, p.y - p.y0)); }
        if (p && p.moved > T.LONG_PRESS_MOVE_PX && pressId === e.pointerId) clearPress();
        sk.move?.(e); return 'stroke';
      }
      const p = touches.get(e.pointerId); if (!p) return 'ignored';
      p.x = e.clientX; p.y = e.clientY; p.moved = Math.max(p.moved, Math.hypot(p.x - p.x0, p.y - p.y0));
      if (pressId === e.pointerId && p.moved > T.LONG_PRESS_MOVE_PX) clearPress();
      if (ignored.has(e.pointerId) || !gesture || !gesture.ids.includes(e.pointerId)) return 'ignored';
      if (p.moved > T.TAP_MOVE_PX) gesture.tapOk = false;
      const m = midOf(gesture.ids), d = distOf(gesture.ids);
      let prev = gesture.mid;
      // Once a pan starts, its first step covers the distance moved under the threshold too.
      if (!gesture.panning && Math.hypot(m.x - gesture.x0, m.y - gesture.y0) > T.PAN_PX) { gesture.panning = true; gesture.tapOk = false; prev = {x: gesture.x0, y: gesture.y0}; }
      if (!gesture.one && !gesture.pinching && gesture.startDist && Math.abs(d / gesture.startDist - 1) > T.PINCH_RATIO) { gesture.pinching = true; gesture.tapOk = false; }
      gesture.mid = m;
      if (gesture.pinching && gesture.baseDist) { gesture.scale = gesture.baseScale * d / gesture.baseDist; h.pinch?.(snap()); }
      if (gesture.panning) h.pan?.(m.x - prev.x, m.y - prev.y);
      return 'gesture';
    }
    /** @param {PointerEvent} e */
    function up(e) {
      const cancelled = e.type === 'pointercancel';
      if (stroke?.id === e.pointerId) {
        if (stroke.kind === 'pen') { penDown = false; penUpAt = now(); }
        if (pressId === e.pointerId) clearPress();
        stroke = null; touches.delete(e.pointerId);
        if (cancelled) sk.cancel?.(e); else sk.end?.(e);
        return 'stroke';
      }
      const p = touches.get(e.pointerId); if (!p) return 'ignored';
      touches.delete(e.pointerId); if (pressId === e.pointerId) clearPress();
      if (ignored.delete(e.pointerId) || !gesture || !gesture.ids.includes(e.pointerId)) return 'ignored';
      if (!live().length) {
        const g = gesture, quick = now() - g.first <= T.TAP_UP_MS;
        if (g.one) {
          gesture = null;
          if (g.panning) h.gestureEnd?.({scale: 1, cx: g.mid.x, cy: g.mid.y, x0: g.x0, y0: g.y0});
          else if (!cancelled) h.point?.(e);
        } else if (!cancelled && g.tapOk && quick && !g.panning && !g.pinching && g.maxFingers >= 2 && g.maxFingers <= 3) {
          gesture = null; h.tap?.(g.maxFingers, {scale: 1, cx: g.mid.x, cy: g.mid.y, x0: g.x0, y0: g.y0});
        } else endGesture();
      } else rebase();
      return 'gesture';
    }
    /** @param {PointerEvent} e */
    function handle(e) {
      if (e.type === 'pointerdown') return down(e);
      if (e.type === 'pointermove') return move(e);
      if (e.type === 'pointerup' || e.type === 'pointercancel') return up(e);
      return 'ignored';
    }
    function reset() { clearPress(); touches.clear(); ignored.clear(); stroke = null; gesture = null; penDown = false; }
    /** @param {HTMLElement} el */
    function attach(el) {
      /** @param {PointerEvent} e */
      const on = e => {
        if (e.type === 'pointerdown') { try { el.setPointerCapture(e.pointerId); } catch { /* synthetic or released pointer */ } }
        if (handle(e) !== 'ignored' && e.pointerType !== 'mouse') e.preventDefault();
      };
      const types = ['pointerdown', 'pointermove', 'pointerup', 'pointercancel'];
      for (const t of types) el.addEventListener(t, on);
      el.dataset.touchSurface = ''; el.style.touchAction = 'none';
      return () => { for (const t of types) el.removeEventListener(t, on); reset(); };
    }
    return {handle, attach, reset, busy: () => !!(stroke || gesture)};
  }

  /* ---------- help: popover, ⓘ buttons and the Touch and Pencil section ---------- */
  /** @type {HTMLElement | null} */
  let pop = null, popAnchor = null;
  function hidePopover() {
    if (!pop) return;
    pop.remove(); pop = null;
    popAnchor?.getAttribute('aria-expanded') === 'true' && popAnchor.setAttribute('aria-expanded', 'false');
    popAnchor = null;
  }
  /** @param {string} text @param {Element} anchor */
  function popover(text, anchor) {
    hidePopover(); if (!text) return;
    pop = document.createElement('div'); pop.className = 'jt-pop'; pop.id = 'jt-pop'; pop.setAttribute('role', 'tooltip'); pop.textContent = text;
    document.body.append(pop);
    const r = anchor.getBoundingClientRect(), w = pop.offsetWidth, ph = pop.offsetHeight;
    const left = Math.max(8, Math.min(innerWidth - w - 8, r.left + r.width / 2 - w / 2));
    const top = r.bottom + 8 + ph < innerHeight ? r.bottom + 8 : Math.max(8, r.top - ph - 8);
    pop.style.left = `${left}px`; pop.style.top = `${top}px`;
    popAnchor = /** @type {HTMLElement} */ (anchor);
    shell()?.announce(text);
  }
  /** @param {string} text @param {string} [label] */
  const info = (text, label = 'More about this') =>
    `<button type="button" class="jt-info" aria-expanded="false" aria-label="${esc(label)}" data-info="${esc(text)}">ⓘ</button>`;
  const gestureList = [];
  /** @param {{gesture: string, description: string}[]} list */
  function registerGestures(list) {
    for (const g of list || []) {
      const i = gestureList.findIndex(x => x.gesture === g.gesture);
      if (i >= 0) gestureList[i] = g; else gestureList.push(g);
    }
    shell()?.registerShortcuts('Touch and Pencil', gestureList.map(g => ({gesture: g.gesture, description: g.description})));
  }
  /* A long press on a control with a title shows the title; the click that ends it is swallowed. */
  function wireTitles() {
    let timer = 0, start = null, target = null, shown = false;
    const clear = () => { clearTimeout(timer); timer = 0; target = null; };
    document.addEventListener('pointerdown', e => {
      const t = /** @type {Element} */ (e.target);
      if (pop && !t.closest?.('.jt-pop, .jt-info')) hidePopover();
      shown = false; clear();
      if (e.pointerType === 'mouse') return;
      const el = t.closest?.('[title]');
      if (!el || el.closest('[data-touch-surface]') || !el.getAttribute('title')) return;
      target = el; start = {x: e.clientX, y: e.clientY};
      timer = window.setTimeout(() => { if (target) { popover(target.getAttribute('title'), target); shown = true; } timer = 0; }, T.LONG_PRESS_MS);
    }, true);
    document.addEventListener('pointermove', e => { if (target && Math.hypot(e.clientX - start.x, e.clientY - start.y) > T.LONG_PRESS_MOVE_PX) clear(); }, true);
    for (const type of ['pointerup', 'pointercancel']) document.addEventListener(type, clear, true);
    document.addEventListener('click', e => {
      if (shown) { shown = false; e.preventDefault(); e.stopPropagation(); return; }
      const b = /** @type {HTMLElement} */ (/** @type {Element} */ (e.target).closest?.('.jt-info'));
      if (!b) return;
      e.preventDefault(); e.stopPropagation();
      if (popAnchor === b) return hidePopover();
      popover(b.dataset.info || '', b); b.setAttribute('aria-expanded', 'true');
    }, true);
    document.addEventListener('contextmenu', e => { if (shown || timer) e.preventDefault(); }, true);
    document.addEventListener('keydown', e => { if (e.key === 'Escape' && pop) { const a = popAnchor; hidePopover(); a?.focus?.(); } }, true);
    window.addEventListener('scroll', hidePopover, true);
  }

  /* ---------- keyboard hints ---------- */
  function wireKeys() {
    document.addEventListener('keydown', e => {
      const t = /** @type {Element} */ (e.target);
      if (document.documentElement.dataset.keys !== undefined || t.closest?.('input, textarea, select, [contenteditable]')) return;
      document.documentElement.dataset.keys = '';
    }, true);
  }

  /* ---------- asset-list drawer ---------- */
  const listOpen = () => document.documentElement.dataset.list === 'open';
  /** @param {boolean} open */
  function setList(open) {
    const btn = document.getElementById('jt-list-button'), aside = document.querySelector('aside');
    if (!btn || !aside) return;
    document.documentElement.dataset.list = open ? 'open' : 'closed';
    btn.setAttribute('aria-expanded', String(open));
    if (!coarse()) return;
    if (open) /** @type {HTMLElement} */ (aside.querySelector('input, [aria-selected=true], .item') || aside)?.focus?.({preventScroll: true});
    else if (aside.contains(document.activeElement)) btn.focus();
  }
  function wireList() {
    const tools = document.querySelector('.shell-tools'), aside = document.querySelector('aside');
    if (!tools || !aside) return;
    aside.id ||= 'assets';
    tools.insertAdjacentHTML('afterbegin', `<button type="button" id="jt-list-button" class="jt-list-toggle" aria-controls="${esc(aside.id)}" aria-expanded="false">List</button>`);
    document.body.insertAdjacentHTML('beforeend', '<div class="jt-scrim" aria-hidden="true"></div>');
    document.documentElement.dataset.list = 'closed';
    document.getElementById('jt-list-button').onclick = () => setList(!listOpen());
    /** @type {HTMLElement} */ (document.querySelector('.jt-scrim')).onclick = () => setList(false);
    aside.addEventListener('click', e => { if (coarse() && /** @type {Element} */ (e.target).closest?.('.item')) setList(false); });
    document.addEventListener('keydown', e => { if (e.key === 'Escape' && listOpen() && coarse() && !document.querySelector('dialog[open]')) setList(false); });
  }

  /* ---------- styles ---------- */
  const CSS = `
:root{--tap:44px;--tap-gap:8px;--hit-pad:10px}
.jt-hit{position:relative}.jt-hit::before{content:"";position:absolute;inset:calc(-1 * var(--hit-pad))}
.jt-pop{position:fixed;z-index:70;max-width:min(340px,calc(100vw - 16px));background:var(--ink);color:var(--bg);padding:8px 12px;border-radius:8px;font-size:13px;line-height:1.4;box-shadow:0 6px 24px var(--shadow);white-space:pre-line}
.jt-info{border-radius:50%;padding:0 6px;line-height:18px;font-size:12px;color:var(--muted)}
.jt-list-toggle,.jt-scrim{display:none}
.shell-keys .shell-gesture{font-weight:600}
@media (any-pointer: coarse){
  button,select,input:not([type=checkbox]):not([type=radio]):not([type=range]),.chip,[role=tab],[role=button],[role=slider],.item{min-height:var(--tap)}
  button,[role=tab],[role=button],.chip{min-width:var(--tap)}
  input[type=checkbox],input[type=radio]{width:24px;height:24px}
  input[type=range]{height:var(--tap)}
  input[type=range]::-webkit-slider-thumb{width:28px;height:28px}
  .chips,.shell-tools,.shell-dialog-actions{gap:var(--tap-gap)}
  .shell-dismiss{top:2px;right:2px}
  .jt-info{min-width:var(--tap);min-height:var(--tap)}
  button,select,a,.chip,.item,[role=tab],[role=button],[role=slider],.card{touch-action:manipulation}
  button,.chip,.item,[role=tab],[role=button],.card,canvas,[data-touch-surface]{-webkit-touch-callout:none;-webkit-user-select:none;user-select:none}
  :root:not([data-keys]) button kbd,:root:not([data-keys]) [role=tab] kbd,:root:not([data-keys]) [role=button] kbd{display:none}
  .jt-list-toggle{display:inline-block}
  :root[data-list=closed] .layout{grid-template-columns:minmax(0,1fr)}:root[data-list=closed] .layout>aside{display:none}
  :root[data-list=open] .layout{grid-template-columns:minmax(0,1fr)}
  :root[data-list=open] .layout>aside{position:fixed;inset:0 auto 0 0;width:min(340px,86vw);max-height:none;z-index:45;box-shadow:0 0 40px var(--shadow);border-right:2px solid var(--line)}
  :root[data-list=open] .jt-scrim{display:block;position:fixed;inset:0;z-index:44;background:#0008}
}
@media (hover: none){
  button:not([aria-pressed=true]):hover,.card:hover{border-color:var(--line)}
  .item:not([aria-selected=true]):hover{background:transparent}
}`;

  function init() {
    if (!document.querySelector('header') || !document.getElementById('main')) return;
    const style = document.createElement('style'); style.id = 'jelli-touch-style'; style.textContent = CSS; document.head.append(style);
    wireKeys(); wireTitles(); wireList();
    registerGestures([
      {gesture: 'Long press a control', description: 'Show what it does'},
      {gesture: 'Tap ⓘ', description: 'Show the explanation next to it'},
      {gesture: 'List button', description: 'Show or hide the asset list'}]);
  }

  window.JelliTouch = {T, kind, coarse, pencilOnly, setPencilOnly, onPencilOnly, offerPencilOnly, gestures, registerGestures,
    info, popover, hidePopover, setList};
  window.addEventListener('DOMContentLoaded', init);
})();

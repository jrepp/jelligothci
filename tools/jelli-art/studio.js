/* Jelli Art: painting, palette editing and live reload for compare.html.
 * Injected only by tools/jelli-art/jelli_art.py; the static review page never loads it.
 * Shares the page's globals (state, D, decoded, renderers) and adds a Paint mode.
 * The pixel logic (rasterisers, selection transforms, history) is paint_tools.js (window.JelliPaint). */
'use strict';
(() => {
  /** @type {typeof window.Studio} */
  const S = window.Studio = {};
  const T = window.JelliPaint, L = window.JelliLayers, JT = window.JelliTouch;
  const LOCKED = new Set(['font']);
  const SHAPES = new Set(['line', 'rect', 'ellipse']);
  /* [id, name, key, what it does] */
  const TOOLS = [
    ['pencil', 'Pencil', 'P', 'Draw pixels; right-click draws the secondary colour. Shift+arrows draw with the keyboard cursor'],
    ['eraser', 'Eraser', 'E', 'Make pixels transparent'],
    ['fill', 'Fill', 'F', 'Fill the touching area of one colour; right-click fills with the secondary colour, which erases the area while it is transparent'],
    ['picker', 'Pick colour', 'C', 'Take a palette colour from the sprite (Alt-click with any tool except Select picks without switching); right-click takes the secondary colour'],
    ['shade', 'Shade', 'T', 'Step pixels one colour lighter along their ramp; right-click or Shift steps deeper'],
    ['line', 'Line', 'L', 'Drag from end to end; Shift snaps to clean ratios: flat, 3:1, 2:1, 1:1, 1:2, 1:3 and upright'],
    ['rect', 'Rectangle', 'R', 'Drag corner to corner; Shift makes a square'],
    ['ellipse', 'Ellipse', 'U', 'Drag the bounding box; Shift makes a circle'],
    ['select', 'Select', 'V', 'Drag a rectangle; drag inside it to move, Alt-drag to copy'],
    ['wand', 'Magic wand', 'Q', 'Select the touching pixels of one colour; Shift-click selects that colour everywhere'],
    ['hand', 'Hand', 'H', 'Drag to pan the canvas without changing pixels; middle-drag pans with any tool'],
  ];
  const ARROWS = {ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1]};
  const edits = {};  // key -> {saved, hist}; the working buffer is decoded[key].after
  let pointing = false, stroke = null, view = null, statusTimer = null, clipboard = null, selKey = null, pan = null, anchor = null, wheel = 0, queued = 0, rec = null, detachCanvas = null, gs = null, menu = null, chipHold = null, chipNoClick = false, later = null;
  const prefs = Object.assign({filled: false, perfect: false, wrap: false, guides: false, tiled: false, quickbar: false, lefty: false}, store.get('paint', {}));
  const axes = store.get('mirror-axes', {});  // per asset: {x2, y2} mirror axes in half pixels
  /* color is the main colour (left button), color2 the secondary (right button); null is transparent. */
  Object.assign(state, {tool: 'pencil', color: '#291b35', custom: false, color2: null, custom2: false, ramp: null, mirror: false, before: store.get('before', 'HEAD'), artist: store.get('artist', ''),
    sel: null, selMask: null, float: null, cursor: [0, 0], kbd: false, mirrorV: false}, prefs);
  const savePaintPrefs = () => store.set('paint', {filled: state.filled, perfect: state.perfect, wrap: state.wrap, guides: state.guides, tiled: state.tiled, quickbar: state.quickbar, lefty: state.lefty});
  MODES.push('paint');
  document.getElementById('modes').insertAdjacentHTML('beforeend', '<button data-mode="paint" title="Paint (6)">Paint</button>');
  /* Header row 2 (Review only) holds the before and after refs and the compare-with select; in Paint that select lives in the before pane
   * (homeCompare puts it back whenever the stage is rebuilt). Row 1 gets the save-state cluster; the shell (shell.js) adds the Unsaved pill to it. */
  document.querySelector('header .refs').insertAdjacentHTML('afterend', '<span class="before-wrap" id="before-wrap"><label class="lbl" for="before-ref">compare with</label><select id="before-ref"></select></span>');
  document.querySelector('header h1').insertAdjacentHTML('afterend', '<div class="shell-state" id="shell-state"><span class="live-badge" id="live-badge">● live</span><span id="git-status" class="studio-status"></span><span id="studio-status" class="studio-status" role="status"></span></div>');
  document.head.insertAdjacentHTML('beforeend', `<style>
    .live-badge{font:600 11px ui-monospace,monospace;color:var(--accent)}
    select{font:inherit;color:var(--ink);background:var(--raised);border:1px solid var(--line);border-radius:6px;padding:4px 6px;max-width:220px}
    .studio-status{font-size:12px;color:var(--muted)}.studio-status.warn{color:var(--warn)}.studio-status.bad{color:var(--bad)}
    button.primary{background:var(--primary);color:var(--on-primary);border-color:var(--primary);font-weight:700}
    button:disabled{opacity:.45;cursor:default}
    select:focus-visible,[role=button]:focus-visible,canvas:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
    .paint-chip{display:grid;grid-template-columns:auto;justify-items:center;gap:3px;background:var(--raised);border:2px solid transparent;border-radius:8px;padding:6px;cursor:pointer;min-width:62px;font:10px ui-monospace,monospace;color:var(--muted);position:relative}
    .paint-chip .sw{width:34px;height:34px;border-radius:6px;border:1px solid #fff3}
    .paint-chip[aria-pressed=true]{border-color:var(--ink);color:var(--ink)}
    .paint-slot{position:relative;display:grid}
    .paint-slot .edit{position:absolute;top:2px;right:2px;padding:0 4px;font-size:11px;line-height:16px;border-radius:4px;background:var(--panel)}
    @media (any-pointer: coarse){.paint-slot .edit{display:none}}  /* a 44 px pencil would cover the whole chip; long-press the chip for Edit slot */
    .paint-chip.off .sw{outline:2px solid var(--bad);outline-offset:1px}
    .paint-chip.second{border-style:dashed;border-color:var(--muted)}
    .paint-ramp{display:flex;flex-wrap:wrap;gap:4px;align-items:center;flex-basis:100%}
    .paint-ramp .ramp-name{font:10px ui-monospace,monospace;color:var(--muted);min-width:64px;text-transform:lowercase}
    .paint-pair{display:flex;gap:8px;align-items:center;flex-basis:100%;flex-wrap:wrap}.paint-pair .studio-note{margin:0}
    .pair-sw{position:relative;width:40px;height:34px;flex:none}.pair-sw .sw{position:absolute;width:24px;height:24px;border-radius:5px;border:1px solid #fff6;box-shadow:0 0 0 1px #0008}
    .pair-sw .sw:first-child{left:0;top:0;z-index:1}.pair-sw .sw:last-child{left:14px;top:10px}
    .paint-chip.faint .sw{outline:2px dashed var(--ink);outline-offset:2px}
    .eraser-sw{background:repeating-conic-gradient(#555 0 25%,#2b2b31 0 50%) 0 0/10px 10px}
    .studio-note{font-size:12px;color:var(--muted);max-width:760px;margin:6px 0}
    .artist{width:100%;padding:4px 8px}
    .paint-tools{display:flex;gap:10px 14px;flex-wrap:wrap;align-items:end}
    .paint-readout{font:12px ui-monospace,monospace;color:var(--ink);text-transform:none;letter-spacing:0}
    .paint-hist{list-style:none;margin:0;padding:0;max-height:320px;overflow:auto;position:relative;min-width:150px;border:1px solid var(--line);border-radius:8px;background:var(--panel)}
    .paint-hist button{display:block;width:100%;text-align:left;border:0;border-radius:0;background:none;padding:3px 9px;font-size:12px}
    .paint-hist button:hover{background:var(--raised)}.paint-hist button[aria-current=step]{background:var(--ink);color:var(--bg)}
    .paint-hist button.redo{color:var(--muted);font-style:italic}
    .paint-hint{margin:0;max-width:none}
    /* The workspace (UX M2-B): a grid whose cells are sized by CSS, so the canvas box never depends on the canvas. Flow tier (narrow main area):
     * canvas, quick bar row, flip-book dock, then the side panels. Workspace tier (main area 900 px and up, window 600 px and taller, compare.html):
     * quick bar rail | canvas over its flip-book dock | side column, filling #stage, which does not scroll. */
    .paint-work{display:grid;grid-template-columns:minmax(0,1fr);grid-template-areas:"c" "q" "d" "s";gap:14px;width:100%;--side:clamp(260px,28cqi,380px)}
    .paint-work>.paint-canvas{grid-area:c}.paint-work>.paint-qbar{grid-area:q}.paint-work>.paint-dock{grid-area:d}.paint-work>.paint-side{grid-area:s}
    .paint-dock[hidden]{display:none}
    .paint-canvas{display:grid;grid-template-rows:auto auto;gap:6px;justify-items:stretch;min-width:0;min-height:0}
    .paint-canvas .scroller{display:flex;width:100%;max-width:none;min-width:0;min-height:0;height:clamp(240px,50vh,100cqi);height:clamp(240px,50dvh,100cqi)}
    .paint-canvas .scroller canvas{flex:none;margin:auto}
    .paint-side{min-width:0;display:grid;gap:14px;align-content:start}
    .paint-side #palette-block{margin:0;padding:0;border:0}.paint-side #palette-block h3{margin:0 0 6px;font-size:12px}
    .paint-side .palette{gap:4px;margin:0}.paint-side .paint-chip{padding:3px 2px;font-size:9px}
    .paint-side .paint-ramp .ramp-name{flex-basis:100%;min-width:0}
    .paint-side .paint-pair{flex-wrap:nowrap;gap:6px}.paint-side .paint-pair .studio-note{flex:1 1 0;min-width:0;font-size:10px;line-height:1.25}.paint-side .paint-pair button{flex:none;padding:2px 6px}
    .paint-side .paint-ramp{gap:3px}.paint-side .paint-chip{min-width:40px;gap:1px}.paint-side .paint-chip .sw{width:22px;height:22px}
    .paint-side .studio-note{font-size:11px}.paint-refs{display:flex;gap:12px;flex-wrap:wrap;align-items:flex-start}.paint-refs .paint-hist{max-height:180px}
    .paint-colour-help{margin-top:8px}.paint-colour-help>summary{cursor:pointer;font-size:11px;font-weight:600;width:fit-content}.paint-colour-help>.studio-note{margin-top:4px}
    .paint-more{margin-top:8px}.paint-more>summary{cursor:pointer;font-weight:600;font-size:12px;width:fit-content;padding:2px 0}.paint-more>summary .lbl{font-weight:400;margin-left:6px}
    .paint-more>.paint-tools{margin-top:6px}
    .paint-qbar{display:flex;flex-wrap:wrap;gap:var(--tap-gap,8px);align-items:stretch;touch-action:none;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none}
    .paint-qbar[hidden]{display:none}.paint-qbar .qb-group{display:flex;gap:var(--tap-gap,8px);flex-wrap:wrap}.paint-qbar .qb-group[hidden]{display:none}
    .paint-qbar button{min-width:var(--tap,44px);min-height:var(--tap,44px);padding:2px 4px;font-size:11px;line-height:1.15;touch-action:none}
    .paint-qbar button[aria-pressed=true]{border-color:var(--ink);background:var(--ink);color:var(--bg)}
    .paint-qbar .qb-sep{width:1px;background:var(--line);align-self:stretch}
    .paint-work.lefty .paint-qbar{justify-content:flex-end}
    @media(min-height:600px){@container stage (min-width:900px){
      .paint-work{height:100%;gap:0 16px;grid-template-columns:auto minmax(0,1fr) var(--side);grid-template-rows:minmax(0,1fr) auto;grid-template-areas:"q c s" "q d s"}
      .paint-work.lefty{grid-template-columns:minmax(0,1fr) var(--side) auto;grid-template-areas:"c s q" "d s q"}
      .paint-work.noq{grid-template-columns:minmax(0,1fr) var(--side);grid-template-areas:"c s" "d s"}
      .paint-canvas{grid-template-rows:auto minmax(0,1fr)}
      .paint-canvas .scroller{height:auto}
      .paint-dock{margin-top:10px}
      .paint-side{overflow-y:auto;min-height:0;padding-right:2px}
      .paint-qbar{flex-direction:column;flex-wrap:nowrap;width:68px;align-self:start;max-height:100%;overflow-y:auto;overflow-x:hidden}
      .paint-qbar .qb-group{flex-direction:column;flex-wrap:nowrap}.paint-qbar .qb-sep{width:auto;height:1px;align-self:stretch}.paint-qbar button{width:100%}
    }}
    .paint-menu{position:fixed;z-index:60;display:grid;background:var(--panel);border:2px solid var(--line);border-radius:10px;padding:4px;box-shadow:0 6px 24px var(--shadow)}
    .paint-menu button{text-align:left;min-height:var(--tap,44px);border:0;background:none}

    /* Paint chrome uses a compact rail and contextual options; controls keep their original IDs and actions. */
    #mode-extra{min-width:0;flex:1 1 100%;display:grid;grid-template-columns:minmax(0,1fr) auto;column-gap:12px;align-items:center}.paint-commandbar{grid-column:2;grid-row:1}.paint-context{grid-column:1;grid-row:1}.paint-selection-context{grid-column:1/-1;grid-row:2}.paint-commandbar{display:flex;align-items:center;gap:10px;min-height:34px;justify-content:flex-end;padding:4px 0}
    .paint-commandbar>div>.lbl{display:none}.paint-commandbar #save{min-width:76px}.paint-commandbar .seg{gap:3px;align-items:center}.paint-commandbar button,.paint-property button{display:inline-flex;gap:5px;align-items:center;justify-content:center}.paint-commandbar .save-state{display:inline-block;min-width:58px;text-align:right;margin-right:7px;font-size:10px;color:var(--muted)}.save-state[data-state=dirty]{color:var(--warn)}.save-state[data-state=error]{color:var(--bad)}.jelli-icon{display:inline-block;width:16px;height:16px;flex:none;vertical-align:-3px}
    .paint-context button[hidden]{display:none}.paint-context{display:flex;align-items:center;gap:10px;min-height:40px;flex-wrap:wrap;padding:5px 0}.paint-active-tool{font-size:12px;font-weight:650;min-width:92px}.paint-tool-help{font-size:11px;color:var(--muted);flex:1 1 160px}
    .paint-selection-context{display:flex;align-items:center;gap:8px;flex-wrap:wrap;padding:5px 0;border-top:1px solid var(--line)}.paint-selection-context[hidden]{display:none}.paint-selection-context .lbl{margin:0}
    .paint-toolrail{grid-area:t;align-self:start;min-width:0;background:var(--panel);border:1px solid var(--line);border-radius:7px;padding:4px}
    .paint-toolrail #tools{display:flex;gap:3px;flex-wrap:wrap}.paint-toolrail #tools button{display:flex;align-items:center;justify-content:center;gap:4px;position:relative;min-width:36px;min-height:36px;padding:6px;border:1px solid transparent;border-radius:4px;background:transparent;color:var(--muted)}
    .paint-toolrail #tools button[aria-pressed=true]{background:var(--chip-on);border-color:var(--accent);color:var(--ink)}.paint-toolrail #tools button:hover{background:var(--raised);color:var(--ink)}
    .paint-tool-icon{display:block;width:21px;height:21px;flex:none}.paint-tool-icon svg{display:block;width:100%;height:100%}.paint-toolrail .paint-tool-name{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0)}
    .paint-toolrail kbd{font:8px ui-monospace,monospace;position:absolute;right:2px;bottom:0;color:var(--muted)}
    .paint-work{grid-template-areas:"t" "c" "q" "d" "s";gap:10px;--side:232px}.paint-work>.paint-toolrail{grid-area:t}
    .paint-canvas .scroller{border-color:#ffffff1a;border-radius:5px;background:#25262a;box-shadow:inset 0 0 0 1px #0003}.paint-canvas>.lbl{min-height:22px;align-items:center}.paint-readout{font-size:10px;overflow-wrap:anywhere}
    .paint-side{grid-template-columns:minmax(0,1fr);gap:12px;padding:10px;background:var(--panel);border:1px solid var(--line);border-radius:7px}.paint-side #palette-block{order:-2}.paint-side #paint-more{order:-1}.paint-side #legend:empty{display:none}
    .paint-side .paint-more{margin:0;border-block:1px solid var(--line);padding-block:8px}.paint-more>summary{font-size:12px;list-style-position:inside;padding:3px 0}.paint-more>summary .lbl{display:none}
    .paint-property{padding:7px 0;border-top:1px solid var(--line)}.paint-property:first-of-type{margin-top:7px}.paint-property>summary{font-size:11px;font-weight:600;cursor:pointer;padding:3px 0}.paint-property>div{margin-top:8px}.paint-property .lbl{font-size:10px;margin-bottom:4px}.paint-property .seg{display:flex;flex-wrap:wrap;gap:3px}.paint-property button{font-size:11px;padding:4px 7px}.paint-property select{max-width:100%}
    .paint-side .paint-pair{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr) 30px;gap:5px;align-items:center;margin-bottom:10px;min-width:0;width:100%;max-width:100%}.paint-side #swap-colours{min-width:0;width:100%;max-width:100%;justify-self:stretch}.paint-side .paint-colour-current{display:grid;justify-items:start;gap:5px;padding:6px!important;min-width:0;width:100%;text-align:left;background:var(--raised);border:1px solid var(--line);border-radius:5px}.paint-side .paint-colour-current .sw{display:block;width:32px;height:32px;border-radius:3px;border:1px solid #fff3}.paint-colour-current span:not(.sw){font-size:10px;color:var(--muted);line-height:1.3}.paint-colour-current strong{display:block;font:9px ui-monospace,monospace;color:var(--ink);overflow-wrap:anywhere}
    .paint-side .paint-refs{display:grid;grid-template-columns:minmax(0,1fr);gap:12px}.paint-side .paint-refs>.pane{min-width:0;max-width:100%}.paint-side .before-wrap{display:grid;min-width:0;max-width:100%}.paint-side .before-wrap select{min-width:0;width:100%;max-width:100%}.paint-side .paint-hist{width:100%;min-width:0;max-height:150px}.paint-side .paint-hint{font-size:10px;line-height:1.5}.paint-side .paint-slot .edit{top:-3px;right:-3px;border:1px solid var(--line)}
    @media(min-height:600px){@container stage (min-width:650px){
      .paint-work,.paint-work.noq,.paint-work.lefty{height:100%;grid-template-columns:48px minmax(0,1fr) var(--side);grid-template-rows:minmax(0,1fr) auto auto;grid-template-areas:"t c s" "t q s" "t d s";gap:8px 10px}
      .paint-toolrail #tools{flex-direction:column;flex-wrap:nowrap}.paint-toolrail{width:48px;max-height:100%;overflow-y:auto;overflow-x:hidden}.paint-toolrail #tools button{width:38px}
      .paint-canvas{grid-template-rows:auto minmax(0,1fr)}.paint-canvas .scroller{height:auto}.paint-side{overflow-y:auto;min-height:0}.paint-dock{margin-top:0}
      .paint-qbar,.paint-work.lefty .paint-qbar{grid-area:q;flex-direction:row;flex-wrap:wrap;width:auto;max-height:104px;overflow:auto;align-self:auto;justify-content:flex-start}.paint-work.lefty .paint-qbar{justify-content:flex-end}.paint-qbar .qb-group{flex-direction:row;flex-wrap:wrap}.paint-qbar .qb-sep{width:1px;height:auto}.paint-qbar button{width:auto;min-height:36px}
    }}
    @container stage (max-width:649px){#mode-extra{grid-template-columns:minmax(0,1fr)}.paint-commandbar{grid-column:1;grid-row:1}.paint-context{grid-row:2}.paint-selection-context{grid-row:3}.paint-tool-help{flex-basis:100%}.paint-toolrail #tools{flex-wrap:nowrap;overflow-x:auto}.paint-toolrail #tools button{flex:none}}
    @media(any-pointer:coarse){.paint-toolrail #tools button{min-width:44px;min-height:44px}.paint-toolrail kbd{display:none}.paint-commandbar button,.paint-context button,.paint-selection-context button{min-height:44px}.paint-toolrail{padding:3px}.paint-side .paint-colour-current{min-height:76px}.paint-side .paint-pair{grid-template-columns:minmax(0,1fr) minmax(0,1fr) 44px}.paint-side .paint-chip{min-width:44px;min-height:44px}.paint-qbar button{min-height:44px}}
    @media(min-height:600px) and (any-pointer:coarse){@container stage (min-width:650px){.paint-work,.paint-work.noq,.paint-work.lefty{grid-template-columns:54px minmax(0,1fr) var(--side)}.paint-toolrail{width:54px}.paint-toolrail #tools button{width:44px}}}

    /* Touch landscape keeps modifiers beside the canvas; portrait gives the artwork the full width. */
    @media(min-height:600px) and (any-pointer:coarse){
      @container stage (min-width:900px){
        .paint-work,.paint-work.noq{grid-template-columns:54px 68px minmax(0,1fr) var(--side);grid-template-rows:minmax(0,1fr) auto;grid-template-areas:"t q c s" "t q d s"}
        .paint-work.lefty{grid-template-columns:54px minmax(0,1fr) var(--side) 68px;grid-template-areas:"t c s q" "t d s q"}
        .paint-work.noq,.paint-work.noq.lefty{grid-template-columns:54px minmax(0,1fr) var(--side);grid-template-areas:"t c s" "t d s"}
        .paint-qbar,.paint-work.lefty .paint-qbar{width:68px;max-height:100%;height:100%;flex-direction:column;flex-wrap:nowrap;overflow-y:auto;overflow-x:hidden;align-self:stretch;justify-content:flex-start}
        .paint-qbar .qb-group{flex-direction:column;flex-wrap:nowrap}.paint-qbar .qb-sep{width:auto;height:1px;flex:none}.paint-qbar button{width:100%;flex:none}
      }
      @container stage (min-width:650px) and (max-width:899px){
        html[data-mode=paint] main>#detail.painting{flex:none;display:block}
        #detail.painting>#stage{flex:none}
        .paint-work,.paint-work.noq,.paint-work.lefty{height:auto;grid-template-columns:minmax(0,1fr);grid-template-rows:auto;grid-template-areas:"t" "c" "q" "d" "s";gap:10px}
        .paint-toolrail{width:auto;max-height:none}.paint-toolrail #tools{flex-direction:row;flex-wrap:nowrap;overflow-x:auto}.paint-toolrail #tools button{width:44px;flex:none}
        .paint-canvas{grid-template-rows:auto auto}.paint-canvas .scroller{height:clamp(240px,50dvh,100cqi)}
        .paint-side{overflow:visible}.paint-qbar,.paint-work.lefty .paint-qbar{max-height:none;overflow:visible}
      }
    }

    .vh{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}
    @media (prefers-reduced-motion: reduce){*{scroll-behavior:auto!important;transition:none!important}}
  </style>`);

  /* ---------- server calls ---------- */
  /* Every save records a name (D2, 2026-10-12): it lives in Settings, and an empty one is asked for once, before the first save of this visit. */
  let askedName = false;
  async function ensureArtist() {
    if (String(state.artist || '').trim() || askedName || !window.JelliShell?.dialog) return;
    askedName = true;
    const body = document.createElement('div'); let typed = '';
    body.innerHTML = '<p>Each save is recorded with a name in the history. You can change it later under Settings in the header.</p><label class="lbl" for="ask-artist">Your name</label><input id="ask-artist" type="text" autocomplete="name" maxlength="80" style="width:100%">';
    const choice = await window.JelliShell.dialog({title: 'Who is saving?', body, initial: '#ask-artist', actions: [{label: 'Skip', value: 'skip'}, {label: 'Save with this name', value: 'ok', primary: true}],
      onOpen: el => {
        const field = /** @type {HTMLInputElement} */ (el.querySelector('#ask-artist'));
        field.addEventListener('input', () => { typed = field.value; });
        field.addEventListener('keydown', /** @param {KeyboardEvent} e */ e => { if (e.key === 'Enter') { e.preventDefault(); el.close('ok'); } });
      }});
    const name = choice === 'ok' ? typed.trim() : '';
    if (!name) return;
    state.artist = name; store.set('artist', name);
    const field = /** @type {HTMLInputElement | null} */ (document.getElementById('artist')); if (field) field.value = name;
  }
  async function api(method, url, body) {
    if (method !== 'GET' && body && 'artist' in body && !String(body.artist || '').trim()) { await ensureArtist(); body.artist = state.artist; }
    const res = await fetch(url, {method, headers: body ? {'Content-Type': 'application/json'} : {}, body: body ? JSON.stringify(body) : undefined});
    const data = await res.json().catch(() => ({error: res.statusText}));
    if (!res.ok) throw Object.assign(new Error(data.error || res.statusText), {status: res.status, body: data});
    return data;
  }
  function status(text, cls = '', sticky = false, opts = {}) {
    if (window.JelliShell) return window.JelliShell.notify(text, {tone: cls, sticky, ...opts});  // shell.js: live region and toasts
    const el = document.getElementById('studio-status'); el.textContent = text; el.className = 'studio-status ' + cls;
    clearTimeout(statusTimer); if (!sticky) statusTimer = setTimeout(() => { el.textContent = ''; }, 4000);
  }
  const announce = text => { if (window.JelliShell) return window.JelliShell.announce(text); const el = document.getElementById('paint-live'); if (el) el.textContent = text; };

  /* ---------- loading and live reload ---------- */
  const pendingSaves = new Set(), paintSaveErrors = new Map();
  let reloadSequence = 0, writeEpoch = 0, pendingReloads = 0;
  Object.assign(S, {paintSaving: key => pendingSaves.has(key)});
  async function reload() {
    pendingReloads++;
    try { await reloadPayload(); }
    finally { pendingReloads--; }
  }
  async function reloadPayload() {
    const sequence = ++reloadSequence, epoch = writeEpoch;
    let keep = [];
    const published = await loadPayload(await api('GET', `/api/data?before=${encodeURIComponent(state.before)}`), () => {
      // Fetch and image decoding both yield to painting. Capture the current buffers
      // only at publication, and ignore responses overtaken by a newer load or save.
      if (sequence !== reloadSequence || epoch !== writeEpoch || pendingSaves.size) return false;
      if (stroke) release();
      keep = Object.keys(edits).filter(dirty);
    });
    if (!published) return;
    state.float = null; stroke = null;
    for (const key of Object.keys(edits)) {
      if (!decoded[key]?.after || !keep.includes(key)) delete edits[key];
      else { refresh(key); draft(key); }
    }
  }

  /* Unsaved paint from a previous visit (drafts.js); a stale one saves only after a confirm (409).
   * The history starts at the file on disk, then a "Restored draft" step, so Undo shows what is on disk. */
  function restoreDrafts() {
    const back = [];
    for (const {key} of window.JelliDrafts?.list('paint') || []) {
      const p = decoded[key]?.after, d = JelliDrafts.restore('paint', key, byKey[key]?.sha);
      if (!p || !d) continue;
      const e = ensure(key);
      try {
        if (d.data?.version === 1) {
          const doc = L.unpack(d.data);
          if (!doc.layers.length || doc.layers.length > L.limit || doc.layers.some(l => l.data.length !== p.data.length)) throw new Error('Invalid draft');
          e.doc = doc;
        } else {
          const data = JelliDrafts.decodeBytes(d.data);
          if (!data || data.length !== p.data.length) throw new Error('Invalid draft');
          e.doc = L.create({...p, data});
        }
      } catch { JelliDrafts.drop('paint', key); continue; }
      e.base = d.base; T.record(e.hist, 'Restored draft', L.snapshot(e.doc)); refresh(key);
      back.push(key + (d.stale ? ' (changed on disk since)' : ''));
    }
    if (!back.length) return;
    const text = `Restored unsaved edits from your last visit: ${back.join(', ')}`;
    if (window.JelliShell) window.JelliShell.notify(text, {tone: 'warn', id: 'drafts', hint: 'Save to keep them, or Revert to discard them.'});
    else status(text, 'warn', true);
  }
  S.boot = async () => {
    await reload();
    restoreDrafts();
    const refs = await api('GET', '/api/refs').catch(() => ({base: null, releases: [], commits: []}));
    const select = document.getElementById('before-ref');
    /* Groups of [value, label]: what is in the game, what players have, and recent art changes. */
    const groups = [['', [['HEAD', 'Last save (HEAD)'], ...(refs.base ? [[refs.base, `In the game now (${refs.base})`]] : [])]],
      ['Game releases', refs.releases.map(t => [t, `Release ${t}`])],
      ['Art changes', refs.commits.map(c => [c.ref, `${c.ref} ${c.subject}`.slice(0, 60)])]];
    if (!groups.some(([, opts]) => opts.some(([v]) => v === state.before))) groups[0][1].unshift([state.before, state.before]);
    const esc = t => t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
    const html = opts => opts.map(([v, t]) => `<option value="${esc(v)}">${esc(t)}</option>`).join('');
    select.innerHTML = groups.filter(([, opts]) => opts.length)
      .map(([label, opts]) => label ? `<optgroup label="${esc(label)}">${html(opts)}</optgroup>` : html(opts)).join('');
    select.value = state.before;
    select.onchange = async () => { state.before = select.value; store.set('before', state.before); await reload(); rerender(); };
    const field = document.createElement('div');
    field.innerHTML = '<label class="lbl" for="artist">Your name</label><input id="artist" class="artist" type="text" autocomplete="name" maxlength="80" aria-describedby="artist-hint"><p id="artist-hint">Recorded in the history with every save.</p>';
    window.JelliShell?.addSetting(field);
    const artist = /** @type {HTMLInputElement} */ (field.querySelector('#artist')); artist.value = state.artist;
    artist.oninput = () => { state.artist = artist.value; store.set('artist', state.artist); };
    document.getElementById('live-badge').textContent = `● Jelli Art ${D.studio_version || ''}`;
    renderGit(D.git);
    setInterval(poll, 2000);
    window.addEventListener('beforeunload', e => { if (Object.keys(edits).some(dirty) || S.clipsDirty?.()) { e.preventDefault(); e.returnValue = ''; } });
  };
  /* Confirm through the shell's styled dialog (plain confirm() without it); "1 clip", "3 clips". */
  /* A save that may find the file changed on disk: send(null) uses the page's own base; on a 409 it asks, and Overwrite
   * sends again with the bases the server reports (err.body.current, a map by file name). Cancel returns null. */
  async function saveGuarded(send, what) {
    try { return await send(null); } catch (err) {
      if (err.status !== 409) throw err;
      if (!await ask(`${err.message}\n\nSave your version anyway and replace the one on disk?`, {title: `${what} changed on disk`, confirmLabel: 'Overwrite', danger: true})) return null;
      return send(err.body?.current ?? {});
    }
  }
  const ask = (text, opts = {}) => window.JelliShell?.confirm ? window.JelliShell.confirm(text, opts) : Promise.resolve(confirm(text));
  const count = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;
  Object.assign(S, {api, saveGuarded, status, reload, rerender: () => rerender(), renderGit, paintDirty: key => dirty(key), redraw: () => draw(), ask, count});
  function renderGit(git) {
    const el = document.getElementById('git-status'); if (!git?.enabled) { el.textContent = ''; return; }
    const push = git.last_push || {}, pending = git.unpushed ? `${git.unpushed} to push` : 'synced';
    /* The slot is a fixed width with an ellipsis, so a problem comes first: it must never be the part that is cut off. */
    el.textContent = push.ok === false ? `⚠ push failing, will retry · ⎇ ${git.branch}` : `⎇ ${git.branch} · ${git.push ? pending : 'commits only'}`;
    el.className = 'studio-status' + (push.ok === false ? ' bad' : '');
    el.title = push.error || `Every save is committed to ${git.branch}${git.push ? ' and pushed to GitHub for review' : ''}.`;
    if (git.pending_commits) {  // commits that failed are queued server-side and retried (git_sync.py)
      el.textContent = `⚠ ${git.pending_commits} commit(s) queued · ${el.textContent}`; el.className = 'studio-status bad'; el.title = git.last_commit_error;
      if (queued !== git.pending_commits) window.JelliShell?.notify(`${git.pending_commits} commit(s) are queued and will be retried: ${git.last_commit_error}`,
        {tone: 'warn', id: 'git-queue', sticky: true, hint: 'Your saves are on disk; the studio retries the commit (POST /api/git/retry retries now).'});
    } else if (queued) window.JelliShell?.dismiss?.('git-queue');
    queued = git.pending_commits || 0;
  }
  async function poll() {
    if (pendingReloads || document.hidden || stroke || rec?.busy() || document.querySelector('dialog[open]')) return;  // no reload under an open dialog
    if (D.git?.enabled) api('GET', '/api/git').then(g => { D.git = g; renderGit(g); }).catch(() => {});
    const {version} = await api('GET', '/api/version').catch(() => ({}));
    // A foreground reload may start while the version request is in flight.
    // Background polling must not invalidate the refresh awaited by creation/save.
    if (pendingReloads || !version || version === D.version) return;
    await reload(); rerender();
    status(Object.keys(edits).some(dirty) ? 'Files changed on disk; your unsaved edits were kept' : 'Updated from disk', 'warn');
  }
  function rerender() { renderTotals(); renderKinds(); renderList(); renderView(); }

  /* ---------- pixel buffer helpers ---------- */
  const signature = doc => JSON.stringify({...L.pack(doc), active: 0});
  const ensure = key => {
    if (edits[key]) return edits[key];
    const p = decoded[key].after, doc = byKey[key]?.paint_document ? L.unpack(byKey[key].paint_document) : L.create(p);
    const saved = L.snapshot(doc);
    return edits[key] = {doc, saved, savedSignature: signature(doc), hist: T.history(saved, 'Opened', 40), base: byKey[key]?.sha};
  };
  const work = key => {
    const p = decoded[key]?.after; if (!p) return p;
    const doc = ensure(key).doc;
    return {...p, data: doc.layers[doc.active].data};
  };
  const composite = key => L.composite(ensure(key).doc, decoded[key].after);
  const dirty = key => !!edits[key] && signature(edits[key].doc) !== edits[key].savedSignature;
  const writable = () => {
    const d = ensure(state.key).doc, l = d.layers[d.active];
    if (l.locked || !l.visible) { announce('Select a visible, unlocked layer to paint'); return false; }
    return true;
  };
  Object.assign(S, {
    layerDocument: () => ensure(state.key).doc,
    changeLayers: (label, change) => {
      if (stroke) release();
      state.float = state.sel = state.selMask = null;
      change(ensure(state.key).doc); done(label); S.renderLayers?.();
    },
    selectLayer: index => {
      if (stroke) release();
      state.float = state.sel = state.selMask = null;
      ensure(state.key).doc.active = index; draw(); renderToolbarState(); S.renderLayers?.();
    }
  });
  /* Assets may carry their own palette (a name in manifest.palettes or an inline list). */
  const ownPalette = a => !!a.palette_name && a.palette_name !== 'shared';
  const paletteOf = a => (a.palette || D.palette).map(h => h.toLowerCase());
  const paletteWhere = a => ownPalette(a) ? `the ${a.palette_name} palette` : 'the shared palette';
  /* The outline colour: shared ink, or the darkest colour of the asset's own palette (lint.js; the server agrees). */
  const inkOf = a => window.JelliLint.outlineInk(ownPalette(a) ? paletteOf(a) : null);
  const slotName = (a, i) => ownPalette(a) ? label(paletteOf(a)[i], a) : slotLabel(i);
  /* Shading ramps (light to deep) for the asset's palette, from assets.json palette_ramps. */
  const rampsOf = a => (D.palette_ramps?.[ownPalette(a) ? a.palette_name : 'shared'] || []).map(r => ({name: r.name, colours: r.colours.map(h => h.toLowerCase())}));
  S.ownPalette = ownPalette;
  const measure = (p, a) => window.JelliLint.measure(p, a.kind, inkOf(a));  // lint.js: compare_slice.py's rules
  /* Push the working buffer to its canvas, metrics, thumbnail and change count. */
  function refresh(key) {
    const a = byKey[key], p = composite(key), before = decoded[key].before;
    decoded[key].after.data.set(p.data);
    p.img.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(p.data), p.w, p.h), 0, 0);
    a.after_metrics = measure(p, a);
    a.after = p.img.toDataURL('image/png');
    if (before) { let n = 0; for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) if (pixel(before, x, y) !== pixel(p, x, y)) n++; a.changed = n; }
  }
  /* Keep unsaved paint in drafts.js after each change, or drop the draft once the sprite is clean again. */
  function draft(key) {
    if (!window.JelliDrafts) return;
    if (!dirty(key)) return JelliDrafts.drop('paint', key);
    JelliDrafts.later('paint', key, edits[key].base, () => L.pack(edits[key].doc));
  }
  function commit() {
    const a = asset(); refresh(a.key); draft(a.key);
    renderHeader(a); renderPalette(a); renderContext(a); renderTotals(); renderList(); draw(); renderToolbarState(); renderHistory(); renderReadout();
  }
  /* The selection as history stores it. selSeen is the selection when the last action finished, or as last changed
   * by selecting, so an action records the selection it started from as well as the one it left. */
  const selNow = () => state.sel ? {sel: {...state.sel}, mask: state.selMask} : null;
  let selSeen = null;
  const noteSel = () => { selSeen = selNow(); };
  /* Finish an action: record it in the asset's history (no-ops are skipped) and redraw. */
  function done(label) { T.record(ensure(state.key).hist, label, L.snapshot(ensure(state.key).doc), {before: selSeen, after: selNow()}); noteSel(); commit(); }
  /* Jump to history entry i. Undo puts back the selection the next step started from, redo the one entry i left. */
  function restore(i) {
    const e = edits[state.key], from = e?.hist.at, data = e && T.jump(e.hist, i); if (!data) return;
    const s = i < from ? e.hist.entries[i + 1].before : e.hist.entries[i].after;
    e.doc = L.restore(data); state.float = null; stroke = null;
    S.renderLayers?.();
    state.sel = s ? {...s.sel} : null; state.selMask = s?.mask || null; noteSel(); commit();
    announce(`History: ${e.hist.entries[i].label}`);
  }
  const undo = () => { const e = edits[state.key]; if (e) restore(e.hist.at - 1); };
  const redo = () => { const e = edits[state.key]; if (e) restore(e.hist.at + 1); };

  /* ---------- painting ---------- */
  /* The colour tools may paint: a colour of the asset's palette, transparent, or any colour once "custom" is chosen.
   * secondary picks the right-button colour. Returns undefined (and warns) when the colour may not be painted. */
  function paintHex(secondary = false) {
    const a = asset(), hex = secondary ? state.color2 : state.color;
    if (hex === null || paletteOf(a).includes(hex) || (secondary ? state.custom2 : state.custom)) return hex;
    status(`${hex} is not in ${paletteWhere(a)}; pick a palette colour, or choose it under custom`, 'warn', true);
    return undefined;
  }
  /* X: swap the main and secondary colours. */
  function swapColours() {
    [state.color, state.color2, state.custom, state.custom2] = [state.color2, state.color, state.custom2, state.custom];
    state.ramp = null;  // the ramp row belonged to the old main colour
    const a = asset(); renderPalette(a); renderToolbarState(); draw();
    announce(`Main colour ${state.color ? label(state.color, a) : 'transparent'}, secondary ${state.color2 ? label(state.color2, a) : 'transparent'}`);
  }
  const selRect = () => state.sel && T.clip(work(state.key), state.sel);
  /* The selection as {r, m}: its rect and, for a magic-wand selection, the mask cropped to it. */
  const selArea = () => state.sel && selRect() && {r: {...state.sel}, m: state.selMask};
  const inSel = (area, x, y) => !area || T.selected(area.r, area.m, x, y);
  /* Mirror axes in half pixels: between the centre columns and rows unless moved (per asset, in this browser). */
  const axisOf = a => ({x2: axes[a.key]?.x2 ?? a.width, y2: axes[a.key]?.y2 ?? a.height});
  const twins = (x, y) => { const ax = axisOf(asset()); return T.mirrorPoints(x, y, state.mirror ? ax.x2 : null, state.mirrorV ? ax.y2 : null); };
  /* Paint one pixel (and its mirror twins); a selection keeps painting inside it. `apply` replaces the plain set,
   * so the Shade tool shares the mirror axes and the selection mask. */
  function plot(p, x, y, hex, area, apply = (px, py) => T.set(p, px, py, hex)) {
    for (const [px, py] of [[x, y], ...twins(x, y)]) if (inSel(area, px, py)) apply(px, py);
  }
  /* Shift while dragging: lines snap to clean ratios (flat, 3:1, 2:1, 1:1 and upright), boxes to squares. */
  function constrain(tool, [x0, y0], [x1, y1]) {
    if (tool === 'line') return T.snapClean(x0, y0, x1, y1);
    const dx = x1 - x0, dy = y1 - y0, d = Math.max(Math.abs(dx), Math.abs(dy));
    return [x0 + (Math.sign(dx) || 1) * d, y0 + (Math.sign(dy) || 1) * d];
  }
  function strokePoints(s) {
    if (s.kind === 'free') return state.perfect && (s.hex || s.tool === 'shade') ? T.pixelPerfect(s.pts) : s.pts;
    const [[x0, y0], [x1, y1]] = [s.from, s.to];
    if (s.tool === 'line') return (s.snapped ? T.cleanLine : T.line)(x0, y0, x1, y1);
    return (s.tool === 'rect' ? T.rect : T.ellipse)(x0, y0, x1, y1, state.filled);
  }
  function paintStroke() {
    const p = work(state.key); p.data.set(stroke.base);
    if (stroke.tool === 'shade') return shadeStroke(p);
    for (const [x, y] of strokePoints(stroke)) plot(p, x, y, stroke.hex, stroke.mask);
  }
  /* Shade: each pixel the stroke covers (and its mirror twins) steps once along its ramp, read from the stroke's start. */
  function shadeStroke(p) {
    const s = stroke, base = {w: p.w, h: p.h, data: s.base}, seen = new Set();
    const step = (x, y) => {
      if (seen.has(y * p.w + x)) return;
      seen.add(y * p.w + x);
      const hex = T.shade(T.get(base, x, y), s.ramps, s.dir, s.prefer); if (hex) T.set(p, x, y, hex);
    };
    for (const [x, y] of strokePoints(s)) plot(p, x, y, null, s.mask, step);
  }
  const snapshot = () => ({data: work(state.key).data.slice(), sel: state.sel && {...state.sel}, selMask: state.selMask, float: state.float && {...state.float}});
  /* Start the tool at a pixel. pointer: false for the keyboard cursor. secondary: the right button (secondary colour,
   * or a deeper shade); deeper: Shift with the shade tool; shift: Shift with the magic wand (select everywhere). */
  function press(x, y, tool, {secondary = false, deeper = false, alt = false, pointer = true, held = false, shift = false} = {}) {
    if (S.placeAttachment?.(asset(), x, y)) return;
    if (tool === 'hand') return announce('Hand tool: drag to pan the canvas');
    if (tool === 'picker') return pick(x, y, secondary);
    // Right-click with Select paints the secondary colour (it used to erase); Shift+Enter at the keyboard cursor stays a select.
    if (!writable()) return;
    if (tool === 'select' && secondary && pointer) tool = 'pencil';
    if (tool === 'select') return pressSelect(x, y, alt, pointer);
    if (tool === 'shade') return pressShade(x, y, secondary || deeper, pointer, held);
    if (tool === 'wand') return pressWand(x, y, shift);
    const hex = tool === 'eraser' ? null : paintHex(secondary); if (hex === undefined) return;
    const p = work(state.key), mask = selArea(); state.float = null;  // painting drops a floating selection where it is
    if (tool === 'fill') {
      if (!inSel(mask, x, y)) return status('Fill inside the selection, or press Escape to clear it', 'warn');
      for (const [px, py] of [[x, y], ...twins(x, y)]) T.flood(p, px, py, hex, mask ? mask.r : T.whole(p), mask?.m);
      return done(hex ? 'Fill' : 'Erase fill');
    }
    const label = SHAPES.has(tool) ? TOOLS.find(t => t[0] === tool)[1] + (tool !== 'line' && state.filled ? ' (filled)' : '') : hex ? 'Pencil' : 'Eraser';
    stroke = {kind: SHAPES.has(tool) ? 'shape' : 'free', tool, hex, pointer, held, mask, label, base: p.data.slice(), from: [x, y], to: [x, y], pts: [[x, y]], snap: snapshot()};
    paintStroke(); draw();
  }
  /* A colour in two ramps (cream in coral and gold) follows the ramp row the artist last chose a colour from, then the paint colour. */
  function pressShade(x, y, deeper, pointer, held) {
    const a = asset(), all = rampsOf(a), ramps = all.map(r => r.colours);
    if (!ramps.length) return status(`${paletteWhere(a)} has no ramps yet; add them under palette_ramps in assets.json`, 'warn', true);
    const p = work(state.key), chosen = all.find(r => r.name === state.ramp); state.float = null;
    stroke = {kind: 'free', tool: 'shade', dir: deeper ? 1 : -1, ramps, prefer: [chosen && ramps[all.indexOf(chosen)], state.color], pointer, held, mask: selArea(),
      label: deeper ? 'Shade deeper' : 'Shade lighter', base: p.data.slice(), from: [x, y], to: [x, y], pts: [[x, y]], snap: snapshot()};
    paintStroke(); draw();
    if (!pointer) {  // the keyboard cursor gets told when nothing can change
      const hex = pixel(p, x, y), before = T.get({w: p.w, h: p.h, data: stroke.base}, x, y);
      if (hex === before) announce(before ? `${label(before, a)} is ${T.rampOf(before, ramps) ? `at the ${deeper ? 'deep' : 'light'} end of its ramp` : 'in no ramp'}` : 'Transparent; nothing to shade');
    }
  }
  function pressSelect(x, y, alt, pointer) {
    const snap = snapshot(), area = selArea();
    if (area && inSel(area, x, y)) {
      if (state.float && alt) state.float.base = work(state.key).data.slice();  // Alt-drag a floating selection: stamp a copy here
      lift(alt);
      stroke = {kind: 'move', pointer, from: [x, y], origin: [state.float.x, state.float.y], snap, label: alt ? 'Copy selection' : 'Move selection'};
    } else {
      state.float = null; state.sel = {x, y, w: 1, h: 1}; state.selMask = null;
      stroke = {kind: 'marquee', pointer, from: [x, y], to: [x, y], snap, moved: false};
    }
    draw(); renderReadout();
  }
  function drag(x, y, snapTo = false) {
    const s = stroke; if (!s) return;
    if (s.kind === 'free') { const [lx, ly] = s.pts[s.pts.length - 1]; s.pts.push(...T.line(lx, ly, x, y).slice(1)); paintStroke(); }
    else if (s.kind === 'shape') { s.snapped = snapTo; s.to = snapTo ? constrain(s.tool, s.from, [x, y]) : [x, y]; paintStroke(); }
    else if (s.kind === 'marquee') { s.moved ||= x !== s.from[0] || y !== s.from[1]; s.to = [x, y]; state.sel = T.clip(work(state.key), T.rectFrom(...s.from, x, y)); }
    else if (s.kind === 'move') {
      place(state.float, s.origin[0] + x - s.from[0], s.origin[1] + y - s.from[1]); compose();
    }
    draw(); renderReadout();
  }
  function release() {
    const s = stroke; if (!s) return; stroke = null;
    if (s.kind === 'marquee') {
      if (s.pointer && !s.moved) state.sel = null;  // a click without a drag clears the selection
      noteSel(); draw(); renderToolbarState(); renderReadout(); flushRefit();
      return announce(state.sel ? `Selected ${state.sel.w} by ${state.sel.h} at ${state.sel.x}, ${state.sel.y}` : 'Selection cleared');
    }
    done(s.label); flushRefit();
  }
  function cancel(quiet = false) {
    const s = stroke; if (!s) return false; stroke = null;
    work(state.key).data.set(s.snap.data); state.sel = s.snap.sel; state.selMask = s.snap.selMask; state.float = s.snap.float;
    draw(); renderReadout(); if (!quiet) announce('Cancelled'); flushRefit(); return true;
  }
  /* Alt-click borrows the picker for one click and keeps the current tool (paint_tools.js pickOutcome).
   * secondary (the right button) sets the secondary colour, transparent included, and always keeps the tool. */
  function pick(x, y, secondary = false) {
    const a = asset(), hex = pixel(work(state.key), x, y);
    if (secondary) {
      if (hex && !paletteOf(a).includes(hex)) return status(`${hex} is not in ${paletteWhere(a)}; the secondary colour takes palette colours`, 'warn', true);
      state.color2 = hex; state.custom2 = false;
      announce(`Secondary colour ${hex ? label(hex, a) : 'transparent'}`);
    } else {
      const out = T.pickOutcome(hex, paletteOf(a), state.tool);
      if (out.off) return status(`${hex} is not in ${paletteWhere(a)}; click its custom chip under Paint colours to paint with it`, 'warn', true);
      if (out.color) { state.color = out.color; state.custom = false; state.ramp = null; }
      state.tool = out.tool;
    }
    renderPalette(a); renderToolbarState(); renderReadout();
  }

  /* ---------- selection: a rect, plus floating pixels while it is being moved or transformed ---------- */
  /* float = {region, base, x, y}: base is the sprite without the region; the buffer is base with region drawn at x, y. */
  function lift(copyOnly = false) {
    if (state.float) return;
    const p = work(state.key), m = state.selMask, r = m ? state.sel : selRect(), base = {w: p.w, h: p.h, data: p.data.slice()};
    if (!copyOnly) T.clearMasked(base, r, m);
    state.float = {region: T.extractMasked(p, r, m), base: base.data, x: r.x, y: r.y}; state.sel = {...r};
  }
  /* Magic wand: select the colour under (x, y), touching it or (global) everywhere. */
  function pressWand(x, y, global) {
    const p = work(state.key), hit = T.wand(p, x, y, !global), hex = pixel(p, x, y); stroke = null; state.float = null;
    state.sel = hit && hit.rect; state.selMask = hit && hit.mask;
    if (!hit) return afterSelect();
    let n = 0; for (let i = 3; i < hit.mask.data.length; i += 4) if (hit.mask.data[i]) n++;
    afterSelect(`Selected ${n} px of ${hex ? label(hex, asset()) : 'transparent'}${global ? ' everywhere' : ''} (${hit.rect.w} by ${hit.rect.h})`);
  }
  /* Keep a floating selection overlapping the sprite by at least one pixel. */
  function place(f, x, y) {
    const p = work(state.key); f.x = Math.max(1 - f.region.w, Math.min(p.w - 1, x)); f.y = Math.max(1 - f.region.h, Math.min(p.h - 1, y));
    state.sel = {x: f.x, y: f.y, w: f.region.w, h: f.region.h};
  }
  function compose() { const p = work(state.key), f = state.float; p.data.set(f.base); T.blit(p, f.region, f.x, f.y); }
  function selectAll() { const p = work(state.key); stroke = null; state.float = null; state.sel = T.whole(p); state.selMask = null; state.tool = 'select'; afterSelect(); }
  function deselect() { state.float = null; state.sel = null; state.selMask = null; afterSelect(); }
  function afterSelect(text) { noteSel(); draw(); renderToolbarState(); renderReadout(); announce(text || (state.sel ? `Selected ${state.sel.w} by ${state.sel.h}` : 'Selection cleared')); }
  function deleteSel() {
    if (!writable()) return;
    const r = selRect(); if (!r) return false;
    const p = work(state.key);
    if (state.float) { p.data.set(state.float.base); state.float = null; } else T.clearMasked(p, state.selMask ? state.sel : r, state.selMask);
    done('Delete selection'); return true;
  }
  function copySel() {
    const p = work(state.key), r = selRect();
    clipboard = state.float ? T.copy(state.float.region) : r ? T.extractMasked(p, state.selMask ? state.sel : r, state.selMask) : T.extract(p, T.whole(p));
    status(`Copied ${clipboard.w}×${clipboard.h}${r ? '' : ' (the whole sprite)'}`); renderToolbarState();
  }
  function cutSel() { if (!selRect()) return status('Select an area to cut', 'warn'); copySel(); deleteSel(); }
  function paste() {
    if (!writable()) return;
    if (!clipboard) return status('Copy something first (⌘C / Ctrl+C)', 'warn');
    const a = asset(), p = work(state.key), off = T.offPalette(clipboard, paletteOf(a));
    if (off.length && !state.custom) return status(`The copied pixels use ${off.join(', ')}, which ${paletteWhere(a)} lacks; choose custom to paste them anyway`, 'bad', true);
    const r = selRect(), [cx, cy] = state.kbd ? state.cursor : [0, 0];
    const x = Math.max(0, Math.min(r ? r.x : cx, p.w - clipboard.w)), y = Math.max(0, Math.min(r ? r.y : cy, p.h - clipboard.h));
    state.float = {region: T.copy(clipboard), base: p.data.slice(), x, y}; state.sel = {x, y, w: clipboard.w, h: clipboard.h}; state.selMask = null;
    state.tool = 'select'; compose(); done('Paste');
    announce(`Pasted ${clipboard.w} by ${clipboard.h} at ${x}, ${y}; arrows with Alt move it`);
  }
  /* Flip or rotate the selection (about its centre), or the whole sprite. */
  function transform(label, fn) {
    if (!writable()) return;
    if (!view) return;
    const p = work(state.key);
    if (selRect()) {
      lift(); const f = state.float, r = fn(f.region); if (state.selMask) state.selMask = fn(state.selMask);
      const x = f.x + Math.floor((f.region.w - r.w) / 2), y = f.y + Math.floor((f.region.h - r.h) / 2);
      f.region = r; place(f, x, y); compose();
    } else {
      const r = fn(T.copy(p)); if (r.w !== p.w) return status('This sprite is not square; select a square area to rotate', 'warn');
      p.data.set(r.data); state.float = null;
    }
    done(label);
    if (label === 'Flip horizontal') S.warnFlip?.(asset());  // lint_ui.js: the light now comes from the top right
  }
  let wrapNoted = false;
  /* Alt+arrows: move the selection, or with Wrap on (or no selection) shift the pixels inside it or the sprite. */
  function nudge(dx, dy) {
    if (!writable()) return;
    if (!view) return;
    const p = work(state.key), r = selRect();
    if (r && (!state.wrap || state.selMask)) {  // a magic-wand selection always moves; Wrap shifts rectangles
      if (state.wrap && !wrapNoted) { wrapNoted = true; status('Wrap shifts rectangles; moved the magic-wand selection instead'); }
      lift(); place(state.float, state.float.x + dx, state.float.y + dy); compose();
      return done('Move selection');
    }
    state.float = null; T.shift(p, r || T.whole(p), dx, dy, state.wrap);
    done(r ? 'Shift selection' : 'Shift sprite');
  }
  function replaceColour() {
    if (!writable()) return;
    const from = document.getElementById('replace-from')?.value; if (!from || !view) return;
    const to = state.tool === 'eraser' ? null : paintHex(); if (to === undefined) return;
    const p = work(state.key), r = selRect(); state.float = null;
    const n = r ? T.replace(p, state.selMask ? state.sel : r, from, to, state.selMask) : T.replace(p, T.whole(p), from, to);
    done('Replace colour');
    status(n ? `Replaced ${n} px of ${from} with ${to || 'transparent'}${r ? ' in the selection' : ''}` : `No ${from} pixels to replace`);
  }
  const toggle = (name, label) => { state[name] = !state[name]; savePaintPrefs(); renderToolbarState(); draw(); announce(`${label} ${state[name] ? 'on' : 'off'}`); };
  /* Zoom to the drawn pixels plus a margin, and scroll them into view (a 48 px frame often has empty rows). */
  function fitContent() {
    if (!view) return;
    const a = view.a, b = T.bounds(work(a.key)); if (!b) return status('Nothing to fit: the sprite has no pixels yet', 'warn');
    const m = 2, x0 = Math.max(0, b.x - m), y0 = Math.max(0, b.y - m), w = Math.min(a.width, b.x + b.w + m) - x0, h = Math.min(a.height, b.y + b.h + m) - y0;
    anchor = {px: x0, py: y0, ox: 0, oy: 0}; setZoom(fitZoom({width: w, height: h}));
    announce(`Fitted ${b.w} by ${b.h} pixels at ${view.z}×`);
  }

  /* ---------- paint stage ---------- */
  function cell(e) { return [Math.floor(e.offsetX / view.z), Math.floor(e.offsetY / view.z)]; }
  function draw() {
    if (!view) return;
    const {ctx, a, z} = view, p = composite(a.key);
    decoded[a.key].after.data.set(p.data);
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0); ctx.imageSmoothingEnabled = false; ctx.clearRect(0, 0, a.width * z, a.height * z);
    fillBackdrop(ctx, a.width, a.height, z, backdropFor(a));
    drawPixels(ctx, state.peek ? decoded[a.key].before : p, z);
    if (!state.peek) S.onion?.(ctx, a, z);  // animation.js ghosts the neighbouring clip frames
    drawGrid(ctx, a.width, a.height, z);
    drawIssues(ctx, a.after_metrics, z);
    if (state.guides) drawGuides(ctx, a, z);
    S.overlay?.(ctx, a, z);
    S.drawAttachments?.(ctx, a, z);  // flipbook.js: a clip frame's eye and ground guides
    const ax = axisOf(a); ctx.fillStyle = 'rgba(133,228,182,.8)';
    if (state.mirror) ctx.fillRect(Math.round(ax.x2 * z / 2) - 1, 0, 2, a.height * z);
    if (state.mirrorV) ctx.fillRect(0, Math.round(ax.y2 * z / 2) - 1, a.width * z, 2);
    drawSelection(ctx, z);
    if (state.hover) {
      const [x, y] = state.hover;
      if (state.tool === 'pencil' && !stroke && state.color) {
        ctx.globalAlpha = 0.55; ctx.fillStyle = state.color; ctx.fillRect(x * z, y * z, z, z); ctx.globalAlpha = 1;
        if (z >= 4 && lowContrast(state.color, a)) { ctx.strokeStyle = backdropLuma(backdropFor(a)) > 128 ? '#000' : '#fff'; ctx.lineWidth = Math.max(1, z / 8); ctx.setLineDash([Math.max(2, z / 4), Math.max(2, z / 4)]); ctx.strokeRect(x * z + z / 4, y * z + z / 4, z / 2, z / 2); ctx.setLineDash([]); }
      }
      drawHover(ctx, z);
      if (state.kbd) { ctx.strokeStyle = '#85e4b6'; ctx.lineWidth = 2; ctx.strokeRect(x * z - 3, y * z - 3, z + 6, z + 6); }
    }
    drawTiles();
    S.afterDraw?.();  // flipbook.js: the paused preview and this frame's thumbnail
  }
  /* Tile preview: the working sprite repeated 3×3 beside the canvas, so seams show while nudging with Wrap. */
  function drawTiles() {
    const side = view.c.closest('.paint-work')?.querySelector('.paint-side'); let box = side?.querySelector('#paint-tiles');
    if (!side || !state.tiled) { box?.remove(); return; }
    const a = view.a, p = composite(a.key), s = Math.max(1, Math.floor(192 / (3 * Math.max(a.width, a.height)))), id = `${a.key}@${s}`;
    if (!box || box.dataset.key !== id) {
      box?.remove(); box = document.createElement('div'); box.className = 'pane'; box.id = 'paint-tiles'; box.dataset.key = id;
      box.innerHTML = '<div class="lbl"><span>tiled 3×3</span><span>seams show while Wrap nudges</span></div>';
      const [c] = makeCanvas(3 * a.width * s, 3 * a.height * s); c.setAttribute('role', 'img'); c.setAttribute('aria-label', `${a.key} repeated 3 by 3`);
      Object.assign(c.style, {maxWidth: '100%', height: 'auto', imageRendering: 'pixelated'});
      box.append(c); const colours = side.querySelector('#palette-block'); if (colours) colours.after(box); else side.append(box);
    }
    // Hover moves redraw the canvas; the tiles only change with the pixels, the backdrop or the isolated colour.
    const look = `${backdropFor(a)}|${state.isolate}`, last = box.last;
    if (last && last.look === look && last.data.length === p.data.length && last.data.every((v, i) => v === p.data[i])) return;
    box.last = {look, data: p.data.slice()};
    const [tile, tctx] = box.tile?.dataset.key === id ? [box.tile, box.tile.getContext('2d')] : makeCanvas(a.width * s, a.height * s);
    tile.dataset.key = id; box.tile = tile;
    tctx.setTransform(DPR, 0, 0, DPR, 0, 0); tctx.imageSmoothingEnabled = false; fillBackdrop(tctx, a.width, a.height, s, backdropFor(a)); drawPixels(tctx, p, s); tctx.globalAlpha = 1;
    const c = box.querySelector('canvas'), ctx = c.getContext('2d');
    ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.imageSmoothingEnabled = false;
    for (let ty = 0; ty < 3; ty++) for (let tx = 0; tx < 3; tx++) ctx.drawImage(tile, tx * tile.width, ty * tile.height);
  }
  /* A paint colour that would barely show on the asset's backdrop (ink on black, say) gets a contrast ring. */
  const lowContrast = (hex, a) => !!hex && Math.abs(lumaOf(hex) - backdropLuma(backdropFor(a))) < 48;
  /* Centre guides: the sprite's middle row and column, and the centre of each 8 px tile. */
  function drawGuides(ctx, a, z) {
    ctx.fillStyle = 'rgba(245,199,100,.8)';
    ctx.fillRect(Math.round(a.width * z / 2) - 1, 0, 2, a.height * z); ctx.fillRect(0, Math.round(a.height * z / 2) - 1, a.width * z, 2);
    if (z < 4) return;
    ctx.fillStyle = 'rgba(245,199,100,.55)';
    for (let ty = 4; ty < a.height; ty += 8) for (let tx = 4; tx < a.width; tx += 8) { ctx.fillRect(tx * z - 3, ty * z, 6, 1); ctx.fillRect(tx * z, ty * z - 3, 1, 6); }
  }
  let antsCache = null;
  function drawSelection(ctx, z) {
    const r = state.sel; if (!r) return;
    if (state.selMask) {  // a magic-wand selection: marching ants along the mask's edge, cached until it changes
      const c = antsCache;
      if (!c || c.mask !== state.selMask || c.x !== r.x || c.y !== r.y || c.z !== z) {
        const path = new Path2D(); for (const [x0, y0, x1, y1] of T.maskEdges(r, state.selMask)) { path.moveTo(x0 * z, y0 * z); path.lineTo(x1 * z, y1 * z); }
        antsCache = {mask: state.selMask, x: r.x, y: r.y, z, path};
      }
      const path = antsCache.path;
      ctx.lineWidth = 2; ctx.setLineDash([4, 4]);
      ctx.strokeStyle = '#000'; ctx.lineDashOffset = 0; ctx.stroke(path); ctx.strokeStyle = '#fff'; ctx.lineDashOffset = 4; ctx.stroke(path);
      ctx.setLineDash([]); return;
    }
    ctx.lineWidth = 2; ctx.setLineDash([4, 4]);
    ctx.strokeStyle = '#000'; ctx.lineDashOffset = 0; ctx.strokeRect(r.x * z + 1, r.y * z + 1, r.w * z - 2, r.h * z - 2);
    ctx.strokeStyle = '#fff'; ctx.lineDashOffset = 4; ctx.strokeRect(r.x * z + 1, r.y * z + 1, r.w * z - 2, r.h * z - 2);
    ctx.setLineDash([]);
  }
  function renderReadout() {
    const el = document.getElementById('paint-readout'); if (!el || !view) return;
    const a = view.a, at = state.hover || (state.kbd ? state.cursor : null), parts = [];
    if (at) { const hex = pixel(work(a.key), at[0], at[1]); parts.push(`x ${at[0]}, y ${at[1]} · ${hex ? `${hex} ${label(hex, a)}` : 'transparent'}`); }
    if (state.sel) parts.push(`selection ${state.sel.w}×${state.sel.h} at ${state.sel.x}, ${state.sel.y}${state.float ? ' (floating)' : ''}`);
    if (stroke && !stroke.pointer) parts.push('Enter finishes · Esc cancels');
    el.textContent = parts.join(' · ') || 'Hover or focus the canvas';
  }
  function scrollToCursor() {
    const s = view.scroller, z = view.z, [x, y] = state.cursor;
    if (x * z < s.scrollLeft) s.scrollLeft = x * z - z; else if ((x + 1) * z > s.scrollLeft + s.clientWidth) s.scrollLeft = (x + 2) * z - s.clientWidth;
    if (y * z < s.scrollTop) s.scrollTop = y * z - z; else if ((y + 1) * z > s.scrollTop + s.clientHeight) s.scrollTop = (y + 2) * z - s.clientHeight;
  }
  function moveCursor(dx, dy, e) {
    const a = view.a, [x0, y0] = state.cursor, x = Math.max(0, Math.min(a.width - 1, x0 + dx)), y = Math.max(0, Math.min(a.height - 1, y0 + dy));
    state.kbd = true;
    // Shift+arrows draw; with Shade, Alt+Shift+arrows shade deeper.
    if (e.shiftKey && !stroke && ['pencil', 'eraser', 'shade'].includes(state.tool)) press(x0, y0, state.tool, {pointer: false, held: true, deeper: e.altKey});
    state.cursor = [x, y]; state.hover = [x, y];
    if (stroke && !stroke.pointer) drag(x, y);
    scrollToCursor(); draw(); renderInspector(a); renderReadout();
    const hex = pixel(work(a.key), x, y); announce(`${x}, ${y} ${hex ? label(hex, a) : 'transparent'}`);
  }
  /* Enter (or Space once the keyboard cursor is in use) applies the tool at the cursor. */
  /* Shift+Enter selects everywhere with the magic wand; with any other tool it applies the secondary colour (or a deeper shade). */
  function applyAtCursor(shift = false) {
    const [x, y] = state.cursor; state.kbd = true; state.hover = [x, y];
    if (stroke?.pointer) return;
    if (stroke) return release();
    press(x, y, state.tool, state.tool === 'wand' ? {pointer: false, shift} : {pointer: false, secondary: shift});
    if (stroke?.kind === 'free') release();
    else if (stroke) announce(`${stroke.kind === 'move' ? 'Moving' : 'Started'} at ${x}, ${y}; arrows to move, Enter to finish, Escape to cancel`);
    draw(); renderReadout();
  }
  function canvasKey(e) {
    const mod = e.metaKey || e.ctrlKey, dir = ARROWS[e.key];
    /* Arrows move the keyboard cursor while it shows; otherwise Left/Right change the clip frame (animation.js). */
    if (dir && e.altKey && e.shiftKey && !mod && state.tool === 'shade') moveCursor(...dir, e);
    else if (dir && e.altKey && !mod) nudge(...dir);
    else if (dir && !mod && !e.shiftKey && !state.kbd && !stroke && dir[0] && S.stepFrame?.(dir[0])) { /* changed frame */ }
    else if (dir && !mod) moveCursor(...dir, e);
    else if (e.key === 'Enter' && e.altKey && !stroke && state.tool !== 'select') { if (!e.repeat) pick(...state.cursor); }  // Alt+Enter picks without switching
    else if (e.key === 'Enter' || (e.key === ' ' && !e.shiftKey && (state.kbd || stroke))) { if (!e.repeat) applyAtCursor(e.key === 'Enter' && e.shiftKey); }
    else if (e.key === 'Escape' && (stroke || state.sel)) { if (!cancel()) deselect(); }
    else if (e.key === 'Escape' && state.kbd) { state.kbd = false; draw(); renderReadout(); announce(S.stepFrame ? 'Keyboard cursor hidden; Left and Right change frame' : 'Keyboard cursor hidden'); }
    else return;
    e.preventDefault(); e.stopPropagation();
  }
  function homeCompare() { const wrap = document.getElementById('before-wrap'), refs = document.querySelector('#header-review .refs'); if (wrap && refs && wrap.parentElement !== refs.parentElement) refs.after(wrap); }
  Object.assign(S, {homeCompare});
  /* The workspace skeleton (UX M2-B) is built once per asset and kept: cells sized by CSS (see the style block), so Studio.fitZoom can measure the
   * scroller before the canvas exists, and later renders swap only the canvas and the side column. The flip-book dock (flipbook.js renderDock) is in
   * place before the measurement, so its height is already out of the box. */
  let sk = null, refitRaf = 0, refitWait = false, workspaceControls = null;
  function skeleton(a) {
    const stage = document.getElementById('stage');
    if (sk && sk.key === a.key && sk.work.parentElement === stage) return sk;
    sk?.ro.disconnect(); closeMenu(); cancelAnimationFrame(refitRaf); refitRaf = 0; refitWait = false;
    stage.textContent = '';
    const work = document.createElement('div'); work.className = 'paint-work';
    const pane = document.createElement('div'); pane.className = 'pane paint-canvas';
    pane.innerHTML = '<div class="lbl"><span id="paint-title">paint</span><span class="paint-readout" id="paint-readout"></span></div>';
    const scroller = document.createElement('div'); scroller.className = 'scroller'; pane.append(scroller);
    pane.insertAdjacentHTML('beforeend', '<div id="paint-live" class="vh" aria-live="polite"></div>');
    const dock = document.createElement('div'); dock.className = 'paint-dock';
    const side = document.createElement('div'); side.className = 'paint-side';
    const qbar = quickBar(), shown = JT.coarse() || state.quickbar; qbar.hidden = !shown;
    work.classList.toggle('noq', !shown); work.classList.toggle('lefty', !!state.lefty);
    work.append(pane, qbar, dock, side); if (workspaceControls) work.prepend(workspaceControls.rail); stage.append(work);
    const ro = new ResizeObserver(() => refit());
    ro.observe(scroller);
    sk = {key: a.key, work, pane, scroller, dock, side, ro, fresh: true};
    dock.hidden = !S.renderDock?.(dock, a);
    return sk;
  }
  /* The fit zoom: the largest whole zoom at which the sprite fits the scroller's box (its border box less the border, so scrollbars never count), from
   * 1 to 32. Never computed from the window. `a` may be just {width, height} (Fit content), then the current skeleton is measured. */
  const fitZoomOf = a => {
    if (LOCKED.has(a.kind)) return 1;
    const {scroller} = sk?.work.isConnected ? sk : skeleton(asset()), r = scroller.getBoundingClientRect();
    const w = r.width - 2 * scroller.clientLeft, h = r.height - 2 * scroller.clientTop;
    if (!(w > 0 && h > 0)) return view?.z || 4;  // not laid out (hidden): keep what is shown
    return clampN(Math.floor(Math.min(w / a.width, h / a.height)), 1, 32);
  };
  Object.assign(S, {fitZoom: fitZoomOf});  // compare.html fitZoom() delegates here in Paint
  /* A resize, a rotation, Split View or the guide hiding changes the box; while the artist has not chosen a zoom, fit again. A stroke or gesture
   * in progress finishes first (flushRefit), so the canvas never changes size under the pointer. */
  function refit() {
    if (state.zoom || !view || !sk?.work.isConnected || state.mode !== 'paint' || state.view !== 'detail') return;
    if (stroke || gs || pan || rec?.busy()) { refitWait = true; return; }
    cancelAnimationFrame(refitRaf);
    refitRaf = requestAnimationFrame(() => {
      refitRaf = 0;
      if (state.zoom || !view) return;
      if (stroke || gs || pan || rec?.busy()) { refitWait = true; return; }
      if (fitZoomOf(view.a) !== view.z) renderStage();
    });
  }
  const flushRefit = () => { if (refitWait) { refitWait = false; refit(); } };
  S.renderStage = (a, z) => {
    S.restoreCanvasOptions?.();
    const stage = document.getElementById('stage'), old = view; closeMenu(); homeCompare();
    // A manual zoom/backdrop rebuild may arrive before pointerup. Finish the current
    // gesture against its original canvas before detaching its listeners.
    if (stroke && old?.a.key === state.key) release();
    else if (stroke && old) { work(old.a.key).data.set(stroke.snap.data); stroke = null; refresh(old.a.key); draft(old.a.key); }
    detachCanvas?.(); detachCanvas = null; rec = null; gs = null; pan = null; later = null;
    const hadFocus = old && document.activeElement === old.c, scroll = old && old.a.key === a.key ? [old.scroller.scrollLeft, old.scroller.scrollTop] : null;
    document.getElementById('zoom-label').textContent = `· ${z}×`;
    if (LOCKED.has(a.kind)) {
      sk?.ro.disconnect(); sk = null; view = null; stage.textContent = '';
      stage.innerHTML = '<p class="studio-note">The font atlas is not paintable here yet; edit that PNG in an image editor.</p>'; return;
    }
    if (selKey !== a.key) {  // a keyboard artist stepping through clip frames keeps the cursor where it was
      const keep = hadFocus && state.kbd && state.cursor[0] < a.width && state.cursor[1] < a.height;
      selKey = a.key; stroke = null; state.sel = state.float = state.selMask = null; selSeen = null; state.kbd = keep;
      if (!keep) state.cursor = [a.width >> 1, a.height >> 1];
    }
    ensure(a.key);
    /* Canvas and its flip-book dock (flipbook.js) under it; a side column holds the legend, the colours, the before image, the history and the hint. */
    const {scroller, side, dock, fresh} = skeleton(a);
    sk.fresh = false;
    if (!fresh) dock.hidden = !S.renderDock?.(dock, a);
    document.getElementById('paint-title').textContent = `${a.key} · ${a.width} × ${a.height} · ${z}×`;
    const [c, ctx] = makeCanvas(a.width * z, a.height * z); c.style.cursor = state.tool === 'hand' ? 'grab' : 'crosshair'; c.style.touchAction = 'none';
    Object.assign(c, {tabIndex: 0}); c.setAttribute('role', 'application'); c.setAttribute('aria-roledescription', 'pixel canvas');
    c.setAttribute('aria-label', `${a.key}, ${a.width} by ${a.height} pixels`); c.setAttribute('aria-describedby', 'paint-hint');
    scroller.replaceChildren(c);
    view = {c, ctx, a, z, scroller};
    side.textContent = '';
    if (workspaceControls) { sk.work.prepend(workspaceControls.rail); side.append(workspaceControls.inspector); }
    const legend = document.getElementById('legend'); if (legend) side.append(legend);
    const layers = document.createElement('section'); layers.id = 'paint-layers'; side.prepend(layers); S.renderLayers?.();
    const colours = document.getElementById('palette-block'); if (colours) side.append(colours);
    const details = document.createElement('details'); details.className = 'paint-reference-details'; details.innerHTML = '<summary>History & comparison</summary>'; side.append(details);
    const refs = document.createElement('div'); refs.className = 'paint-refs'; details.append(refs);
    const ref = decoded[a.key].before;
    if (ref) {
      const rz = Math.max(1, Math.min(4, Math.floor(96 / Math.max(a.width, a.height)))), box = document.createElement('div'); box.className = 'pane';
      box.innerHTML = `<div class="lbl"><span>before</span><span>${D.before_label}</span></div>`;
      const wrap = document.getElementById('before-wrap'); if (wrap) box.querySelector('.lbl').after(wrap);
      const [rc, rctx] = makeCanvas(a.width * rz, a.height * rz); fillBackdrop(rctx, a.width, a.height, rz, backdropFor(a)); drawPixels(rctx, ref, rz);
      rc.setAttribute('role', 'img'); rc.setAttribute('aria-label', `${a.key} before (${D.before_label})`);
      box.append(rc); refs.append(box);
    }
    const hist = document.createElement('div'); hist.className = 'pane';
    hist.innerHTML = '<div class="lbl" id="paint-hist-label"><span>history</span><span>click a step to go back</span></div><ol class="paint-hist" id="paint-history" aria-labelledby="paint-hist-label"></ol>';
    hist.querySelector('ol').onclick = e => { const b = e.target.closest('[data-step]'); if (b) restore(Number(b.dataset.step)); };
    refs.append(hist);
    side.insertAdjacentHTML('beforeend', `<details class="paint-hint-details"><summary>Canvas help</summary><p class="studio-note paint-hint" id="paint-hint">${hintHtml()}</p></details>`);
    wireCanvas(c, scroller, a);
    if (anchor) { scroller.scrollLeft = anchor.px * z - anchor.ox; scroller.scrollTop = anchor.py * z - anchor.oy; anchor = null; }
    else if (scroll) [scroller.scrollLeft, scroller.scrollTop] = scroll;
    if (hadFocus) { pointing = true; c.focus({preventScroll: true}); pointing = false; }  // a rebuild keeps the cursor as it was
    renderQuick(); draw(); renderReadout(); renderHistory(); S.mountCanvasOptions?.();
  };
  const DESKTOP_HINT = `Right-click paints the secondary colour, which is transparent until you set one, so it erases (with Fill, the whole touching area) · X swaps colours · Alt-click picks, Alt+right-click picks the secondary · hold Space to peek at before · Ctrl/⌘+wheel zooms; Hand (H) or middle-drag pans.
      Keyboard: Tab to the canvas to show the cursor; arrows move it, Enter or Space applies the tool, Shift+arrows draw, Esc hides it. <kbd>?</kbd> lists every shortcut.`;
  /* Touch wording replaces the mouse wording on coarse pointers (RFC-007); the keyboard sentence stays for a keyboard-attached iPad. */
  const KEYBOARD_HINT = DESKTOP_HINT.slice(DESKTOP_HINT.indexOf('Keyboard:'));
  function hintHtml() {
    if (!JT.coarse()) return DESKTOP_HINT;
    const lead = JT.pencilOnly() ? 'Pencil only is on: the Pencil paints; one finger pans and tapping it shows that pixel; two fingers pan and pinch to zoom.' : 'The Pencil and one finger paint; two fingers pan and pinch to zoom.';
    return `${lead} Two-finger tap undoes, three-finger tap redoes. Hold a finger still to pick a colour and keep the tool. The quick bar stands in for right-click and the modifier keys: hold a button while you draw, or tap it to keep it on. The secondary colour is transparent until you set one, so 2nd erases.
      ${KEYBOARD_HINT}`;
  }
  const renderHint = () => { const el = document.getElementById('paint-hint'); if (el) el.innerHTML = hintHtml(); };
  /* Canvas input goes through JelliTouch.gestures (touch.js): the mouse, the Pencil and (unless Pencil only is on) one finger
   * paint; two fingers pan and pinch; a two-finger tap undoes, three fingers redo; a held finger picks a colour.
   * The quick bar's modifiers (mods) join the mouse buttons and keys: 2nd is the right button, Pick is Alt, Constrain is Shift. */
  /* A quick bar button: a finger or the Pencil acts when it lifts on the button (a tap's click is hit-tested again after the lift, and
   * a page that reflows then aims it at another control), the mouse and keyboard on click. */
  function onTap(b, fn) {
    let armed = false, fired = -1e9;
    b.addEventListener('pointerdown', e => { armed = JT.kind(e) !== 'mouse' && !b.disabled; });
    b.addEventListener('pointerup', e => {
      const r = b.getBoundingClientRect();
      if (!armed || e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) return;
      armed = false; fired = performance.now(); fn();
    });
    b.addEventListener('click', () => { if (performance.now() - fired > 1000) fn(); });
  }
  const clampN = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
  function wireCanvas(c, scroller, a) {
    detachCanvas?.(); gs = null; later = null; const z = view.z;
    /* The pixel under a client point; measured from the canvas box, so a pinch preview does not matter. */
    const at = e => { const r = c.getBoundingClientRect(), k = (r.width / (a.width * z)) || 1; return [Math.floor((e.clientX - r.left) / k / z), Math.floor((e.clientY - r.top) / k / z)]; };
    const track = e => {
      const [x, y] = at(e);
      if (state.hover?.[0] === x && state.hover?.[1] === y && !stroke) return;
      state.hover = [x, y]; state.kbd = false; state.cursor = [clampN(x, 0, a.width - 1), clampN(y, 0, a.height - 1)];
      if (stroke?.pointer) drag(x, y, e.shiftKey || modOn('constrain')); else draw();
      renderInspector(a); renderReadout();
    };
    const begin = () => { gs ||= {rect: c.getBoundingClientRect(), z, ox: null, oy: null}; return gs; };
    const showPixel = e => {
      const [x, y] = at(e); state.hover = [x, y]; state.kbd = false; state.cursor = [clampN(x, 0, a.width - 1), clampN(y, 0, a.height - 1)];
      draw(); renderReadout(); const hex = pixel(work(a.key), x, y); announce(`${x}, ${y} ${hex ? label(hex, a) : 'transparent'}`);
    };
    const history = (dir) => { const e = edits[state.key]; if (!e || !(dir < 0 ? e.hist.at > 0 : e.hist.at < e.hist.entries.length - 1)) return announce(dir < 0 ? 'Nothing to undo' : 'Nothing to redo'); dir < 0 ? undo() : redo(); };
    rec = JT.gestures({
      stroke: {
        start(e) {
          if (state.tool === 'hand' || (JT.kind(e) === 'mouse' && e.button === 1)) { pan = {x: e.clientX, y: e.clientY, l: scroller.scrollLeft, t: scroller.scrollTop}; return; }
          later = null; if (stroke) return;
          for (const k in mods) if (mods[k].held) mods[k].used = true;  // a modifier held for a stroke is a hold, however short
          const [x, y] = at(e), secondary = e.button === 2 || modOn('second'), alt = e.altKey || modOn('pick'), shift = e.shiftKey || modOn('constrain');
          // Alt-click picks except with Select (Alt-drag copies there); Alt+right-click picks the secondary with any tool.
          const tool = alt && (state.tool !== 'select' || secondary) ? 'picker' : state.tool;
          state.kbd = false; state.cursor = [x, y]; state.hover = [x, y];
          const go = () => press(x, y, tool, {secondary, deeper: shift, alt, shift});
          // Fill, Wand and Pick finish at once, so a finger runs them when it lifts: a second finger or a long press that
          // turns the touch into a gesture then leaves nothing to undo.
          if (JT.kind(e) === 'touch' && (tool === 'fill' || tool === 'picker' || tool === 'wand')) { later = go; draw(); return; }
          go();
        },
        move(e) { if (pan) { scroller.scrollLeft = pan.l - (e.clientX - pan.x); scroller.scrollTop = pan.t - (e.clientY - pan.y); return; } track(e); },
        end(e) {
          const go = later; later = null; if (pan) { pan = null; return flushRefit(); }
          if (go) return go();
          if (stroke?.pointer) { const [x, y] = at(e); drag(x, y, e.shiftKey || modOn('constrain')); release(); }  // the lift point counts even if its move was coalesced
        },
        cancel(e, reason) {
          if (pan) { pan = null; return flushRefit(); }
          const quiet = ['second-finger', 'long-press', 'pen'].includes(reason);
          later = null; if (stroke?.pointer) cancel(quiet);
        }},
      pan(dx, dy) { begin(); scroller.scrollLeft -= dx; scroller.scrollTop -= dy; },
      pinch(g) {  // a CSS preview about the pinch point; the whole-number zoom is committed once, in gestureEnd
        const s = begin(); if (s.ox === null) { s.ox = g.x0 - s.rect.left; s.oy = g.y0 - s.rect.top; }
        s.cx = g.cx; s.cy = g.cy;  // the fingers' midpoint while both are down: lifting one finger first moves the final midpoint
        c.style.transformOrigin = `${s.ox}px ${s.oy}px`; c.style.transform = `scale(${clampN(g.scale, 1 / z, 32 / z)})`;
      },
      gestureEnd(g) {
        const s = gs; gs = null; if (!s) return flushRefit();
        c.style.transform = ''; c.style.transformOrigin = '';
        const next = clampN(Math.round(s.z * g.scale), 1, 32); if (s.ox === null || next === s.z || !c.isConnected) return flushRefit();
        const r = scroller.getBoundingClientRect();
        anchor = {px: s.ox / s.z, py: s.oy / s.z, ox: (s.cx ?? g.cx) - r.left, oy: (s.cy ?? g.cy) - r.top};  // the pinched pixel stays under the fingers
        setZoom(next); announce(`Zoom ${next}×`);
      },
      tap(fingers) { history(fingers === 2 ? -1 : 1); },
      point: showPixel,
      longPress(e) {
        const [x, y] = at(e); if (x < 0 || y < 0 || x >= a.width || y >= a.height) return;
        const hex = pixel(work(a.key), x, y); pick(x, y, modOn('second'));
        announce(`Picked ${hex ? label(hex, a) : 'transparent'}`);
      }}, {offerPencilOnly: true});
    detachCanvas = rec.attach(c);
    c.oncontextmenu = e => e.preventDefault();
        c.onpointerdown = e => { e.preventDefault(); pointing = true; c.focus({preventScroll: true}); pointing = false; };
    c.onpointermove = e => { if (JT.kind(e) !== 'touch' && !rec.busy()) track(e); };  // hover: a mouse or a Pencil above the glass
    c.onpointerleave = e => { if (JT.kind(e) !== 'touch' && !stroke && !state.kbd) { state.hover = null; draw(); renderReadout(); } };  // a finger leaves its last pixel in the readout
    c.onkeydown = canvasKey;
    c.onfocus = () => { if (!pointing && !state.kbd) { state.kbd = true; state.hover = [...state.cursor]; draw(); renderReadout(); } };
    c.onkeyup = e => { if (e.key === 'Shift' && stroke?.held) release(); };
    c.onblur = () => { if (stroke && !stroke.pointer) release(); };
    c.addEventListener('wheel', e => {
      if (!(e.ctrlKey || e.metaKey)) return;
      e.preventDefault(); wheel += e.deltaY; if (Math.abs(wheel) < 30) return;
      const z = view.z, next = Math.max(1, Math.min(32, z + (wheel < 0 ? 1 : -1))); wheel = 0;
      if (next === z) return;
      anchor = {px: e.offsetX / z, py: e.offsetY / z, ox: e.offsetX - scroller.scrollLeft, oy: e.offsetY - scroller.scrollTop};
      setZoom(next);
    }, {passive: false});
  }
  /* ---------- quick bar (RFC-007): the touch and Pencil form of right-click, modifier keys, Space and X ---------- */
  /* Each modifier is {latched, held}: holding a button applies it while held (a thumb on the bar, the Pencil on the canvas);
   * a short tap latches it until tapped again. */
  const mods = {pick: {}, second: {}, constrain: {}, peek: {}};
  const MOD_NAMES = {pick: 'Pick', second: 'Secondary colour', constrain: 'Constrain', peek: 'Peek at before'};
  const CONSTRAIN = {line: 'Snap', rect: 'Square', ellipse: 'Circle', shade: 'Deeper', wand: 'Everywhere'};
  const modOn = k => !!(mods[k].latched || mods[k].held);
  function applyPeek() {
    const on = modOn('peek'); if (!!state.peek === on) return;
    state.peek = on; document.body.classList.toggle('peeking', on); draw();
  }
  function wireMod(b, k) {
    let t0 = 0, was = false;
    const changed = latch => { renderQuick(); if (k === 'peek') applyPeek(); if (latch) announce(`${MOD_NAMES[k]} ${mods[k].latched ? 'on' : 'off'}`); };
    b.addEventListener('pointerdown', e => {
      if (e.button) return; e.preventDefault();
      try { b.setPointerCapture(e.pointerId); } catch { /* synthetic pointer */ }
      t0 = performance.now(); was = !!mods[k].latched; mods[k].held = true; mods[k].used = false; changed(false);
    });
    const up = e => {
      if (!mods[k].held) return; mods[k].held = false;
      const tap = e.type === 'pointerup' && !mods[k].used && performance.now() - t0 < JT.T.TAP_UP_MS;
      if (tap) mods[k].latched = !was;
      changed(tap);
    };
    b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up);
    b.addEventListener('click', e => { if (e.detail === 0) { mods[k].latched = !mods[k].latched; changed(true); } });  // the keyboard
  }
  function quickBar() {
    const bar = document.createElement('div'); bar.className = 'paint-qbar'; bar.id = 'paint-qbar'; bar.setAttribute('role', 'toolbar'); bar.setAttribute('aria-label', 'Quick bar');
    const q = (id, text, title, label = '') => `<button type="button" id="qb-${id}" title="${title}"${label ? ` aria-label="${label}"` : ''}>${text}</button>`;
    const nudges = [['left', '←', 'ArrowLeft'], ['up', '↑', 'ArrowUp'], ['down', '↓', 'ArrowDown'], ['right', '→', 'ArrowRight']];
    bar.innerHTML = `<div class="qb-group" role="group" aria-label="History">${q('undo', 'Undo', 'Undo the last step (two-finger tap)')}${q('redo', 'Redo', 'Redo (three-finger tap)')}</div><span class="qb-sep"></span>
      <div class="qb-group" role="group" aria-label="Hold while you draw, or tap to keep on">${q('pick', 'Pick', 'Touch the canvas to pick a colour and keep the tool. Hold it, or tap to keep it on')}${q('second', '2nd', 'Paint with the secondary colour, like right-click. Hold it, or tap to keep it on', '2nd colour')}${q('constrain', 'Constrain', 'Constrain the shape, like Shift. Hold it, or tap to keep it on')}${q('peek', 'Peek', 'Show the before image while held, like Space. Tap to keep it on')}${q('swap', 'Swap', 'Swap the main and secondary colours (X)')}</div><span class="qb-sep"></span>
      <div class="qb-group" id="qb-sel" role="group" aria-label="Selection">${nudges.map(([w, g]) => q(`nudge-${w}`, g, `Move the selection 1 px ${w}`, `Nudge ${w}`)).join('')}${q('deselect', 'Deselect', 'Drop the selection (Escape)')}</div>
      <div class="qb-group" role="group" aria-label="Input">${q('pencil', 'Pencil only', 'Pencil only: fingers pan, zoom and pick instead of painting; the Pencil paints', 'Pencil only: fingers pan, zoom and pick instead of painting')}</div>`;
    const el = id => /** @type {HTMLButtonElement} */ (bar.querySelector('#qb-' + id));
    onTap(el('undo'), () => { const e = edits[state.key]; if (e && e.hist.at > 0) undo(); });
    onTap(el('redo'), () => { const e = edits[state.key]; if (e && e.hist.at < e.hist.entries.length - 1) redo(); });
    onTap(el('swap'), swapColours); onTap(el('deselect'), deselect); onTap(el('pencil'), () => JT.setPencilOnly(!JT.pencilOnly()));
    for (const [w, , key] of nudges) onTap(el(`nudge-${w}`), () => nudge(...ARROWS[key]));
    for (const k of Object.keys(mods)) wireMod(el(k), k);
    return bar;
  }
  /* Show or hide the bar, and bring every button's label and state up to date. */
  function renderQuick() {
    const bar = document.getElementById('paint-qbar'); if (!bar) return;
    bar.hidden = !(JT.coarse() || state.quickbar);
    // A hidden bar cannot show or clear a latched modifier, so a mouse user would keep painting with 2nd or Pick on.
    if (bar.hidden) { for (const k in mods) mods[k].latched = mods[k].held = false; applyPeek(); }
    const work = bar.closest('.paint-work'); work?.classList.toggle('lefty', !!state.lefty); work?.classList.toggle('noq', bar.hidden);
    const q = id => /** @type {HTMLButtonElement} */ (bar.querySelector('#qb-' + id)), pressed = (id, on) => q(id).setAttribute('aria-pressed', String(!!on)), e = edits[state.key];
    q('undo').disabled = !(e && e.hist.at > 0); q('redo').disabled = !(e && e.hist.at < e.hist.entries.length - 1);
    const sel = state.tool === 'select', word = CONSTRAIN[state.tool];
    q('pick').textContent = sel ? 'Copy' : 'Pick'; q('pick').setAttribute('aria-label', sel ? 'Copy: drag the selection to copy it' : 'Pick colour');
    q('constrain').textContent = word || 'Constrain'; q('constrain').disabled = !word; q('constrain').setAttribute('aria-label', `Constrain${word ? `: ${word}` : ''}`);
    for (const k of Object.keys(mods)) pressed(k, modOn(k));
    pressed('pencil', JT.pencilOnly());
    /** @type {HTMLElement} */ (bar.querySelector('#qb-sel')).hidden = !(view && state.sel);
  }
  JT.onPencilOnly(() => { renderQuick(); renderHint(); });
  window.matchMedia?.('(any-pointer: coarse)').addEventListener?.('change', () => { renderQuick(); renderHint(); });
  JT.registerGestures([
    {gesture: 'Pencil', description: 'Paint with the current tool (a finger paints too unless Pencil only is on)'},
    {gesture: 'Two-finger drag', description: 'Paint: pan the canvas (one finger with Pencil only)'},
    {gesture: 'Pinch', description: 'Paint: zoom at the pinch point'},
    {gesture: 'Two-finger tap', description: 'Paint: undo'}, {gesture: 'Three-finger tap', description: 'Paint: redo'},
    {gesture: 'Long press the canvas', description: 'Paint: pick a colour and keep the tool (a finger only)'},
    {gesture: 'Long press a colour', description: 'Paint: menu to use it as secondary, isolate it or edit its slot'},
    {gesture: 'Quick bar: hold', description: 'Paint: Pick, 2nd, Constrain and Peek apply while held; tap one to keep it on'}]);

  /* Long-pressing a palette colour (a finger or the Pencil) opens this menu in place of right-click and Shift-click. */
  function closeMenu() { menu?.remove(); menu = null; }
  function openMenu(anchorEl, items) {
    closeMenu();
    const m = document.createElement('div'); m.className = 'paint-menu'; m.setAttribute('role', 'menu'); m.setAttribute('aria-label', 'Colour actions');
    for (const [text, fn] of items) {
      const b = document.createElement('button'); b.type = 'button'; b.setAttribute('role', 'menuitem'); b.textContent = text;
      b.onclick = () => { closeMenu(); fn(); }; m.append(b);
    }
    m.onkeydown = e => { if (e.key === 'Escape') { e.stopPropagation(); closeMenu(); anchorEl.focus(); } };
    document.body.append(m); menu = m;
    const r = anchorEl.getBoundingClientRect(), w = m.offsetWidth, h = m.offsetHeight;
    m.style.left = `${clampN(r.left, 8, innerWidth - w - 8)}px`; m.style.top = `${r.bottom + 4 + h < innerHeight ? r.bottom + 4 : Math.max(8, r.top - h - 4)}px`;
    announce('Colour menu'); m.querySelector('button').focus({preventScroll: true});
  }
  document.addEventListener('pointerdown', e => { if (menu && !menu.contains(/** @type {Node} */ (e.target))) closeMenu(); }, true);

  /* Rebuild a list while keeping keyboard focus on the matching control. */
  function keepFocus(el, attr, build, want) {
    const had = el.contains(document.activeElement) ? document.activeElement.getAttribute(attr) : null;
    build();
    if (had !== null) el.querySelector(`[${attr}="${want ?? had}"]`)?.focus({preventScroll: true});
  }
  function renderHistory() {
    const el = document.getElementById('paint-history'), e = edits[state.key]; if (!el || !e) return;
    keepFocus(el, 'data-step', () => {
      el.innerHTML = e.hist.entries.map((h, i) => `<li><button data-step="${i}" aria-current="${i === e.hist.at ? 'step' : 'false'}"${i > e.hist.at ? ' class="redo"' : ''}>${i + 1}. ${h.label}</button></li>`).join('');
    }, e.hist.at);
    const cur = el.querySelector('[aria-current=step]');
    if (cur) el.scrollTop = Math.max(0, cur.offsetTop - el.clientHeight / 2);
  }

  /* ---------- toolbar, header and palette ---------- */
  const toolIcon = id => window.JelliIcons?.svg(id) || '';
  const btn = (id, text, title, keys, extra = '') => `<button id="${id}" title="${title}${keys ? ` (${keys})` : ''}"${keys ? ` aria-keyshortcuts="${keys.replace(/⌘/g, 'Meta+').replace(/⇧/g, 'Shift+')}"` : ''}${extra}>${text}</button>`;
  S.renderToolbar = extra => {
    if (LOCKED.has(asset().kind)) return;
    /* Everyday tools stay in view; the rest folds into More tools (remembered) so the canvas sits near the top. */
    extra.innerHTML = `<div class="paint-commandbar" id="paint-commandbar" role="toolbar" aria-label="Paint commands">
      <div><div class="lbl">History</div><div class="seg" role="group" aria-label="History">${btn('undo', `${toolIcon('undo')}<span>Undo</span>`, 'Undo', '⌘Z')}${btn('redo', `${toolIcon('redo')}<span>Redo</span>`, 'Redo', '⇧⌘Z')}</div></div>
      <div><div class="lbl">File</div><div class="seg" role="group" aria-label="File"><span id="paint-save-status" class="save-state" role="status" aria-live="polite" aria-atomic="true">Saved</span>${btn('save', `${toolIcon('save')}<span>Save</span>`, 'Save this asset', '⌘S', ' class="primary"')}</div></div></div>
      <div class="paint-context" id="paint-context" role="toolbar" aria-label="Tool options"><span id="paint-active-tool" class="paint-active-tool">Pencil</span><div class="seg" role="group" aria-label="Drawing options">${btn('filled', 'Filled', 'Filled rectangles and ellipses', '⇧F')}${btn('perfect', 'Pixel-perfect', 'Keep strokes 1 px wide, without doubled diagonal corners', '⇧P')}${btn('mirror', 'Mirror', 'Mirror left/right', 'M')}</div><span id="paint-tool-help" class="paint-tool-help"></span></div>
      <div id="paint-selection-context" class="paint-selection-context" role="toolbar" aria-label="Selection actions"><span class="lbl" id="paint-selection-label">Selection</span><div class="seg" role="group" aria-label="Selection">${btn('sel-all', 'All', 'Select the whole sprite', '⌘A')}${btn('sel-copy', 'Copy', 'Copy the selection, or the whole sprite', '⌘C')}${btn('sel-cut', 'Cut', 'Cut the selection', '⌘X')}${btn('sel-paste', 'Paste', 'Paste as a floating selection', '⌘V')}${btn('sel-delete', 'Delete', 'Clear selected pixels', 'Delete')}${btn('sel-none', 'Deselect', 'Drop the selection', 'Escape')}</div></div>
      <div class="paint-toolrail" id="paint-toolrail" role="toolbar" aria-label="Paint tools"><div class="seg" id="tools" role="group" aria-label="Tools">${TOOLS.map(([id, name, key, what]) =>
        `<button data-tool="${id}" title="${name} (${key}): ${what}" aria-label="${name}" aria-keyshortcuts="${key}"><span class="paint-tool-icon" aria-hidden="true">${toolIcon(id)}</span><span class="paint-tool-name">${name}</span><kbd aria-hidden="true">${key}</kbd></button>`).join('')}</div></div>
      <details class="paint-more" id="paint-more"${store.get('paint-more', false) ? ' open' : ''}><summary>Properties <span class="lbl">transform · symmetry · view</span></summary>
      <div class="paint-tools" role="toolbar" aria-label="More paint tools">
      <div><div class="lbl">Transform</div><div class="seg" role="group" aria-label="Transform">${btn('flip-h', '⇋', 'Flip left/right: the selection, or the whole sprite', '⇧H', ' aria-label="Flip horizontally"')}${btn('flip-v', '⇅', 'Flip top/bottom: the selection, or the whole sprite', '⇧V', ' aria-label="Flip vertically"')}${btn('rot-cw', '↻', 'Rotate 90° clockwise', '⇧R', ' aria-label="Rotate clockwise"')}${btn('rot-ccw', '↺', 'Rotate 90° anticlockwise', '', ' aria-label="Rotate anticlockwise"')}</div></div>
      <div><div class="lbl">Nudge</div><div class="seg" role="group" aria-label="Nudge">${[['ArrowLeft', '←', 'left'], ['ArrowUp', '↑', 'up'], ['ArrowDown', '↓', 'down'], ['ArrowRight', '→', 'right']].map(([k, g, w]) =>
        btn(`nudge-${w}`, g, `Move the selection (or shift the sprite) 1 px ${w}`, `Alt+${k}`, ` aria-label="Nudge ${w}" data-nudge="${k}"`)).join('')}${btn('wrap', 'Wrap', 'Nudges wrap pixels round inside the selection or sprite', 'W')}</div></div>
      <div><div class="lbl"><label for="replace-from">Replace colour</label></div><div class="seg" role="group" aria-label="Replace colour"><select id="replace-from"></select>${btn('replace', '→ paint colour', 'Replace this colour with the paint colour (or transparent with the eraser), in the selection or the whole sprite', '')}</div></div>
      <div><div class="lbl">Mirror <span class="paint-readout" id="mirror-axis"></span></div><div class="seg" role="group" aria-label="Mirror">${btn('mirror-v', 'Top/bottom', 'Mirror top/bottom as well (M mirrors left/right)', '⇧M')}${
        [['axis-left', '◀', 'Move the left/right axis half a pixel left', -1, 0], ['axis-right', '▶', 'Move the left/right axis half a pixel right', 1, 0],
          ['axis-up', '▲', 'Move the top/bottom axis half a pixel up', 0, -1], ['axis-down', '▼', 'Move the top/bottom axis half a pixel down', 0, 1]].map(([id, g, t, dx, dy]) =>
          btn(id, g, t, '', ` aria-label="${t}" data-axis="${dx},${dy}"`)).join('')}${btn('axis-centre', 'Centre', 'Put the mirror axes back in the middle', '')}</div></div>
      <div><div class="lbl">View</div><div class="seg" role="group" aria-label="View">${btn('guides', 'Guides', 'Centre and tile-centre guides', '⇧G')}${btn('fit-content', 'Fit content', 'Zoom to the drawn pixels plus a 2 px margin', 'Z')}${btn('tiled', 'Tile 3×3', 'Show the sprite repeated 3×3 beside the canvas, to check Wrap seams', '⇧T')}</div></div>
      <div><div class="lbl">Touch</div><div class="seg" role="group" aria-label="Touch">${btn('quickbar', 'Quick bar', 'Show the quick bar with a mouse too; touch screens always show it', '')}${btn('lefty', 'Left-handed', 'Put the quick bar on the right', '')}</div></div>
      <div><div class="lbl">Clean up</div>${btn('tidy', 'Tidy outline', 'Closed 1px outline, remove specks, bottom shadow', '')}</div><div><div class="lbl">Restore</div><div class="seg" role="group" aria-label="Restore pixels">${btn('revert', `${toolIcon('revert')}<span>Revert</span>`, 'Discard unsaved edits', '')}${btn('reset', 'Use before', 'Load the before image into the canvas', '')}</div></div></div></details>`;
    extra.querySelector('#paint-more').ontoggle = e => store.set('paint-more', e.target.open);
    const on = (id, fn) => { extra.querySelector('#' + id).onclick = fn; };
    extra.querySelector('#tools').onclick = e => { const t = e.target.closest('button')?.dataset.tool; if (t) setTool(t); };
    on('filled', () => toggle('filled', 'Filled shapes')); on('perfect', () => toggle('perfect', 'Pixel-perfect')); on('mirror', () => toggle('mirror', 'Mirror'));
    on('sel-all', selectAll); on('sel-copy', copySel); on('sel-cut', cutSel); on('sel-paste', paste); on('sel-delete', deleteSel); on('sel-none', deselect);
    on('flip-h', () => transform('Flip horizontal', T.flipH)); on('flip-v', () => transform('Flip vertical', T.flipV));
    on('rot-cw', () => transform('Rotate clockwise', r => T.rotate(r, true))); on('rot-ccw', () => transform('Rotate anticlockwise', r => T.rotate(r, false)));
    extra.querySelectorAll('[data-nudge]').forEach(b => { b.onclick = () => nudge(...ARROWS[b.dataset.nudge]); });
    on('wrap', () => toggle('wrap', 'Wrap')); on('replace', replaceColour); on('guides', () => toggle('guides', 'Guides')); on('fit-content', fitContent); on('tiled', () => toggle('tiled', 'Tile preview'));
    on('quickbar', () => toggle('quickbar', 'Quick bar')); on('lefty', () => toggle('lefty', 'Left-handed')); on('mirror-v', () => toggle('mirrorV', 'Mirror top/bottom')); on('axis-centre', () => moveAxis(0, 0, true));
    extra.querySelectorAll('[data-axis]').forEach(b => { b.onclick = () => moveAxis(...b.dataset.axis.split(',').map(Number)); });
    on('undo', undo); on('redo', redo);
    const tidyButton = extra.querySelector('#tidy');
    tidyButton.onclick = tidy;
    if (ownPalette(asset())) { tidyButton.disabled = true; tidyButton.title = 'Tidy inks with the shared palette; this sprite has its own palette'; }
    on('revert', () => { const e = edits[state.key]; if (e && dirty(state.key)) { state.float = null; e.doc = L.restore(e.saved); done('Revert'); S.renderLayers?.(); } });
    on('reset', () => { const b = decoded[state.key].before; if (b && writable()) { state.float = null; work(state.key).data.set(b.data); done('Use before'); } });
    on('save', save);
    const inspector = extra.querySelector('#paint-more'), groups = [...inspector.querySelector('.paint-tools').children];
    /** @type {[string, number[]][]} */
    const sections = [['Transform & selection', [0, 1, 2]], ['Symmetry', [3]], ['View & input', [4, 5]], ['Clean up & restore', [6, 7]]];
    for (const [title, indices] of sections) {
      const section = document.createElement('details'); section.className = 'paint-property';
      section.open = store.get(`paint-property-${title}`, title === 'Transform & selection');
      const summary = document.createElement('summary'); summary.textContent = title; section.append(summary);
      for (const index of indices) if (groups[index]) section.append(groups[index]);
      section.ontoggle = () => store.set(`paint-property-${title}`, section.open);
      inspector.append(section);
    }
    inspector.querySelector('.paint-tools').remove();
    workspaceControls?.rail.remove(); workspaceControls?.inspector.remove();
    workspaceControls = {rail: extra.querySelector('#paint-toolrail'), inspector};
    if (sk?.work.isConnected) { sk.work.prepend(workspaceControls.rail); sk.side.prepend(inspector); }
    renderToolbarState();
  };
  /* Move the mirror axes by half pixels (or back to the middle), kept per asset in this browser. */
  function moveAxis(dx, dy, centre = false) {
    if (!view) return;
    const a = view.a, ax = axisOf(a);
    if (centre) delete axes[a.key];
    else axes[a.key] = {x2: Math.max(1, Math.min(2 * a.width - 1, ax.x2 + dx)), y2: Math.max(1, Math.min(2 * a.height - 1, ax.y2 + dy))};
    if (axes[a.key]?.x2 === a.width && axes[a.key]?.y2 === a.height) delete axes[a.key];
    store.set('mirror-axes', axes); renderToolbarState(); draw();
    const now = axisOf(a); announce(`Mirror axes at x ${now.x2 / 2}, y ${now.y2 / 2}`);
  }
  function setTool(tool) {
    if (tool === state.tool) return;
    if (stroke) cancel();
    state.tool = tool; if (view) view.c.style.cursor = tool === 'hand' ? 'grab' : 'crosshair'; renderToolbarState(); renderPalette(asset()); draw(); renderReadout();
    announce(`${TOOLS.find(t => t[0] === tool)[1]} tool`);
  }
  function renderToolbarState() {
    if (view) view.c.style.cursor = state.tool === 'hand' ? 'grab' : 'crosshair';
    document.querySelectorAll('#tools button').forEach(b => b.setAttribute('aria-pressed', String(b.getAttribute('data-tool') === state.tool)));
    for (const id of ['mirror', 'filled', 'perfect', 'wrap', 'guides', 'tiled', 'quickbar', 'lefty']) document.getElementById(id)?.setAttribute('aria-pressed', String(!!state[id]));
    document.getElementById('mirror-v')?.setAttribute('aria-pressed', String(!!state.mirrorV));
    const axisEl = document.getElementById('mirror-axis'), ax = view && axisOf(view.a);
    if (axisEl && ax) axisEl.textContent = `x ${ax.x2 / 2} · y ${ax.y2 / 2}`;
    const e = edits[state.key], isDirty = dirty(state.key), save = /** @type {HTMLButtonElement} */ (document.getElementById('save'));
    if (save) {
      const saving = pendingSaves.has(state.key);
      save.disabled = !isDirty || saving;
      const saveState = document.getElementById('paint-save-status'), failure = paintSaveErrors.get(state.key);
      if (saveState) { saveState.textContent = saving ? 'Saving…' : failure ? (failure.startsWith('Saved; commit failed:') ? 'Commit failed' : 'Save failed') : isDirty ? 'Unsaved' : 'Saved'; saveState.dataset.state = saving ? 'saving' : failure ? 'error' : isDirty ? 'dirty' : 'saved'; saveState.title = failure || ''; }
    }
    const able = (id, ok) => { const b = /** @type {HTMLButtonElement} */ (document.getElementById(id)); if (b) b.disabled = !ok; };
    able('undo', e && e.hist.at > 0); able('redo', e && e.hist.at < e.hist.entries.length - 1);
    const p = work(state.key), sel = view && selRect();
    const tool = TOOLS.find(t => t[0] === state.tool), title = document.getElementById('paint-active-tool'), help = document.getElementById('paint-tool-help');
    if (title) title.textContent = tool ? `${tool[1]} · ${tool[2]}` : '';
    if (help) help.textContent = state.tool === 'hand' ? 'Drag to pan · Space peeks at before' : state.tool === 'select' ? 'Drag to select · Alt-drag to copy' : state.tool === 'wand' ? 'Click a colour · Shift selects it everywhere' : state.tool === 'shade' ? 'Lighter · Shift or right-click for deeper' : SHAPES.has(state.tool) ? 'Drag to draw · Shift constrains' : 'Alt-click picks a colour';
    /** @type {[string, boolean][]} */
    const contextualOptions = [['filled', ['rect', 'ellipse'].includes(state.tool)], ['perfect', ['pencil', 'shade'].includes(state.tool)], ['mirror', !['hand', 'picker', 'select', 'wand'].includes(state.tool)]];
    for (const [id, relevant] of contextualOptions) {
      const control = /** @type {HTMLButtonElement} */ (document.getElementById(id)); if (control) { control.hidden = !relevant; control.disabled = !relevant; }
    }
    const selection = document.getElementById('paint-selection-context');
    if (selection) selection.hidden = !(sel || clipboard || ['select', 'wand'].includes(state.tool));
    const selectionLabel = document.getElementById('paint-selection-label');
    if (selectionLabel) selectionLabel.textContent = sel ? `${state.float ? 'Floating' : 'Selection'} ${sel.w} × ${sel.h}` : 'Selection';
    for (const id of ['sel-cut', 'sel-delete', 'sel-none']) able(id, !!sel);
    able('sel-paste', !!clipboard); able('rot-cw', sel || p?.w === p?.h); able('rot-ccw', sel || p?.w === p?.h);
    renderQuick();
    const from = document.getElementById('replace-from'); if (!from || !p) return;
    const a = asset(), keep = from.value, list = sel ? T.colours(p, state.selMask ? state.sel : sel, state.selMask) : T.colours(p);
    from.innerHTML = list.map(([hex, n]) => `<option value="${hex}">${label(hex, a)} ${hex} · ${n} px</option>`).join('') || '<option value="">no colours</option>';
    if (list.some(([hex]) => hex === keep)) from.value = keep;
    able('replace', list.length > 0);
  }
  S.decorateHeader = (a, items) => {
    if (dirty(a.key)) items.push(['worse', 'unsaved edits']);
    if (a.hand_painted) items.push(['good', 'hand-painted · recipe leaves it alone']);
  };
  const activate = el => { el.onkeydown = e => { if ((e.key === 'Enter' || e.key === ' ') && e.target === el) { e.preventDefault(); e.stopPropagation(); el.click(); } }; };
  S.renderPalette = (el, a) => {
    closeMenu();
    const own = ownPalette(a), pal = paletteOf(a), where = paletteWhere(a);
    // Arriving at a sprite: keep a custom colour the artist chose, otherwise start from one of its palette colours.
    if (S.paletteKey !== a.key) { S.paletteKey = a.key; if (state.color && !pal.includes(state.color) && !state.custom) state.color = inkOf(a); }
    el.previousElementSibling.innerHTML = own ? `Paint colours <span class="lbl">· ${a.palette_name}</span>`
      : 'Paint colours <span class="lbl">· shared palette</span>';
    const peers = D.assets.filter(x => own ? x.palette_name === a.palette_name : !ownPalette(x));
    const counts = a.after_metrics.colors, usage = hex => peers.filter(x => x.after_metrics.colors[hex]).length;
    if (state.color2 && !pal.includes(state.color2) && !state.custom2) state.color2 = null;  // the secondary keeps to this palette too
    /* Right-click on a chip (Shift+F10 or the menu key from the keyboard) sets the secondary colour; null is transparent. */
    const setSecondary = (hex, custom) => { state.color2 = hex; state.custom2 = custom; S.renderPalette(el, a); draw(); announce(`Secondary colour ${hex ? label(hex, a) : 'transparent'}`); };
    /* Row labels come from build_slice.py PALETTE_ROWS (sent as palette_rows), which keeps ramp names off them. */
    const ROWS = {unramped: 'other', unramped_only: 'palette', extras: 'more', ...D.palette_rows};
    const ramps = rampsOf(a), inRamp = (name, hex) => !!ramps.find(r => r.name === name)?.colours.includes(hex);
    /* ramp: the ramp row the chip sits in. A colour in two rows (cream) is one chip per row, and the row clicked last is the
     * one the Shade tool follows (state.ramp), so only that row's chip shows as chosen. */
    const chip = (hex, title, sub, extra = '', custom = false, parent = el, ramp = null) => {
      const b = document.createElement('div'); b.className = 'paint-chip' + extra; b.tabIndex = 0; b.title = title;
      b.dataset.chip = ramp ? `${ramp}:${hex}` : hex || 'eraser';
      const here = !ramp || !state.ramp || state.ramp === ramp || !inRamp(state.ramp, hex);
      const active = hex === null ? state.tool === 'eraser' : state.tool !== 'eraser' && state.color === hex && here;
      if (active && hex && lowContrast(hex, a)) { b.classList.add('faint'); title += ` · hard to see on the ${backdropFor(a)} backdrop`; b.title = title; }
      if (state.color2 === hex) { b.classList.add('second'); title += ' · secondary colour'; b.title = title; }
      const [chipName, ...chipDetails] = title.split(' · ');
      // Include the visible usage count so voice-control names match the chip's text.
      b.setAttribute('aria-pressed', String(active)); b.setAttribute('role', 'button');
      b.setAttribute('aria-label', [`${chipName} ${sub}`, ...chipDetails].join(' · '));
      b.innerHTML = `<span class="sw ${hex ? '' : 'eraser-sw'}" style="${hex ? `background:${hex}` : ''}"></span><span>${chipName}</span> <span>${sub}</span>`;
      b.onclick = e => {
        if (chipNoClick) { chipNoClick = false; return; }  // the click that ends a long press
        if (e.shiftKey && hex) { state.isolate = state.isolate === hex ? null : hex; return draw(); }
        if (e.altKey) return setSecondary(hex, custom);
        if (hex === null) state.tool = 'eraser';
        else { state.color = hex; state.custom = custom; state.ramp = ramp; if (['eraser', 'picker', 'select', 'wand'].includes(state.tool)) state.tool = 'pencil'; }
        S.renderPalette(el, a); renderToolbarState(); draw();
      };
      /* A long press (a finger or the Pencil) opens the colour menu; where the browser sends a touch contextmenu instead, that does the same. */
      const menuFor = () => {
        const input = /** @type {HTMLInputElement | null | undefined} */ (b.closest('.paint-slot')?.querySelector('input[type=color]'));
        openMenu(b, [['Use as secondary', () => setSecondary(hex, custom)],
          ...(hex ? [[state.isolate === hex ? 'Show all colours' : 'Isolate', () => { state.isolate = state.isolate === hex ? null : hex; draw(); }]] : []),
          ...(input ? [['Edit slot', () => input.click()]] : [])]);
      };
      b.dataset.touchSurface = '';
      b.addEventListener('pointerdown', e => {
        chipNoClick = false; clearTimeout(chipHold); if (e.pointerType === 'mouse') return;
        const x0 = e.clientX, y0 = e.clientY;
        const stop = ev => {
          if (ev.type === 'pointermove' && Math.hypot(ev.clientX - x0, ev.clientY - y0) <= JT.T.LONG_PRESS_MOVE_PX) return;
          clearTimeout(chipHold); for (const t of ['pointermove', 'pointerup', 'pointercancel']) b.removeEventListener(t, stop);
        };
        for (const t of ['pointermove', 'pointerup', 'pointercancel']) b.addEventListener(t, stop);
        chipHold = setTimeout(() => { chipNoClick = true; menuFor(); }, JT.T.LONG_PRESS_MS);
      });
      b.oncontextmenu = e => { e.preventDefault(); if (e.pointerType === 'touch' || e.pointerType === 'pen') { chipNoClick = true; if (!menu) menuFor(); return; } setSecondary(hex, custom); };
      activate(b);
      // Keyboard right-click: Shift+F10 or the menu key (macOS fires no contextmenu for them, so handle them here).
      const enter = b.onkeydown;
      b.onkeydown = e => { if (e.target === b && (e.key === 'ContextMenu' || (e.key === 'F10' && e.shiftKey))) { e.preventDefault(); e.stopPropagation(); return setSecondary(hex, custom); } enter(e); };
      parent.append(b); return b;
    };
    /* A labelled row of chips: one per ramp (light to deep), then the palette colours in no ramp, then the extras. */
    const row = name => {
      const r = document.createElement('div'); r.className = 'paint-ramp'; r.setAttribute('role', 'group'); r.setAttribute('aria-label', name);
      const n = document.createElement('span'); n.className = 'ramp-name'; n.textContent = name; r.append(n); el.append(r); return r;
    };
    const swatch = hex => `<span class="sw ${hex ? '' : 'eraser-sw'}" style="${hex ? `background:${hex}` : ''}"></span>`;
    keepFocus(el, 'data-chip', () => {
      el.textContent = '';
      const pair = document.createElement('div'); pair.className = 'paint-pair'; pair.setAttribute('role', 'group'); pair.setAttribute('aria-label', 'Main and secondary colours');
      pair.innerHTML = `<button type="button" class="paint-colour-current" id="paint-main-colour" data-chip="main" aria-label="Main ${state.color || 'Transparent'}. Choose a colour from the palette" title="Main colour · choose a swatch below">${swatch(state.color)}<span>Main <strong>${state.color || 'Transparent'}</strong></span></button>
        <button type="button" class="paint-colour-current" id="paint-secondary-colour" data-chip="secondary" aria-label="Secondary ${state.color2 || 'Transparent'}. Colour actions" title="Secondary colour · right-click a palette swatch to set it">${swatch(state.color2)}<span>Secondary <strong>${state.color2 || 'Transparent'}</strong></span></button>${btn('swap-colours', '⇄', 'Swap the main and secondary colours', 'X', ' aria-label="Swap main and secondary colours" data-chip="swap"')}`;
      /** @type {HTMLButtonElement} */ (pair.querySelector('#swap-colours')).onclick = swapColours;
      /** @type {HTMLButtonElement} */ (pair.querySelector('#paint-main-colour')).onclick = () => { const target = el.querySelector(`.paint-chip[aria-pressed="true"]`) || el.querySelector('.paint-chip'); target?.focus({preventScroll: true}); target?.scrollIntoView({block: 'nearest'}); announce('Choose a palette swatch for the main colour'); };
      const secondary = /** @type {HTMLButtonElement} */ (pair.querySelector('#paint-secondary-colour'));
      secondary.onclick = () => openMenu(secondary, [['Swap with main colour (X)', swapColours], ['Use transparent', () => setSecondary(null, false)], ['Set to main colour', () => setSecondary(state.color, state.custom)]]);
      el.append(pair);
      const ramped = new Set(ramps.flatMap(r => r.colours)), rest = pal.filter(h => !ramped.has(h));
      const lines = [...ramps.map(r => [r.name, r.colours.filter(h => pal.includes(h))]), ...(rest.length ? [[ramps.length ? ROWS.unramped : ROWS.unramped_only, rest]] : [])];
      lines.forEach(([name, colours], n) => { const r = row(name); colours.forEach(hex => slotChip(hex, pal.indexOf(hex), r, n < ramps.length ? name : null)); });
      const extras = row(ROWS.extras);
      chip(null, 'eraser · transparent', state.color2 === null ? 'right-click' : 'eraser', '', false, extras);
      for (const hex of Object.keys(counts).filter(h => !pal.includes(h)))
        chip(hex, `custom · ${hex} · not in ${where}; release builds reject it until a slot uses this colour`, `${counts[hex]} px`, ' off', true, extras);
      if (!pal.includes(state.color) && state.color && !counts[state.color])
        chip(state.color, `custom · ${state.color} · not in ${where}`, state.color.slice(1), ' off', true, extras);
      const custom = document.createElement('div'); custom.className = 'paint-chip'; custom.title = 'Paint with any colour, outside the palette';
      custom.tabIndex = 0; custom.setAttribute('role', 'button'); custom.setAttribute('aria-label', 'Custom pick… choose a colour'); custom.dataset.chip = 'custom';
      custom.innerHTML = `<span class="sw" style="background:conic-gradient(#fa8c99,#f5c764,#85e4b6,#a47bdb,#fa8c99)"></span><span>Custom pick…</span><input type="color" value="${state.color || '#ffffff'}" hidden>`;
      const input = custom.querySelector('input'); custom.onclick = e => { if (e.target !== input) input.click(); };
      custom.oncontextmenu = e => e.preventDefault();  // the secondary takes palette colours only
      input.onchange = e => { state.color = e.target.value.toLowerCase(); state.custom = true; state.ramp = null; state.tool = 'pencil'; S.renderPalette(el, a); renderToolbarState(); draw(); };
      activate(custom); extras.append(custom);
      const note = document.createElement('p'); note.className = 'studio-note';
      note.textContent = own
        ? `Click a colour to paint with it; right-click it, press Shift+F10 on it, or choose it and press X to make it the secondary colour. Rows are the ramps from assets.json, light to deep, which the Shade tool (T) steps along. This sprite uses ${where}, so the shared palette and its ✎ do not apply. Tools paint only these colours until you choose custom; custom colours work in the live game only. Shift-click isolates a colour.`
        : 'Click a colour to paint with it; right-click it, press Shift+F10 on it, or choose it and press X to make it the secondary colour. Rows are the ramps from assets.json, light to deep, which the Shade tool (T) steps along. ✎ changes that palette colour in every sprite. Tools paint only palette colours until you choose custom; custom colours work in the live game, and to ship one, put it in a palette slot. Shift-click isolates a colour.';
      const help = document.createElement('details'); help.className = 'paint-colour-help'; help.innerHTML = '<summary>About the colours</summary>'; help.append(note); el.append(help);
    });
    /* One palette slot's chip; the shared palette also gets its ✎. */
    function slotChip(hex, i, parent, ramp) {
      const b = chip(hex, `${slotName(a, i)} · ${hex} · used by ${usage(hex)} assets`, counts[hex] ? `${counts[hex]} px` : '—', '', false, parent, ramp);
      if (own) return;  // only the shared palette is editable here; a named palette is edited in assets.json
      // The ✎ button is the chip's sibling: a button inside a role=button chip is nested-interactive.
      const slot = document.createElement('div'); slot.className = 'paint-slot'; b.replaceWith(slot); slot.append(b);
      slot.insertAdjacentHTML('beforeend', `<button class="edit" title="Change ${slotLabel(i)} everywhere" aria-label="Change ${slotLabel(i)} in every sprite">✎</button><input type="color" value="${hex}" hidden>`);
      const input = slot.querySelector('input'); slot.querySelector('.edit').onclick = e => { e.stopPropagation(); input.click(); };
      input.onchange = e => editSlot(i, e.target.value);
    }
  };

  /* ---------- shortcut docs: the shell's ? overlay lists them; the handlers are S.keydown and canvasKey ---------- */
  window.JelliShell?.registerShortcuts('Paint', [
    {keys: ['P', 'E', 'F', 'C'], description: 'Pencil, eraser, fill, pick colour'}, {keys: ['L', 'R', 'U'], description: 'Line, rectangle, ellipse'},
    {keys: ['T'], description: 'Shade: step pixels lighter along their ramp (right-click or Shift+drag: deeper)'},
    {keys: ['V'], description: 'Select'}, {keys: ['Q'], description: 'Magic wand; Shift-click (or Shift+Enter at the cursor) selects the colour everywhere'},
    {keys: ['Shift+F'], description: 'Filled shapes on/off'}, {keys: ['Shift+P'], description: 'Pixel-perfect pencil on/off (1 px strokes, no doubled corners)'},
    {keys: ['M'], description: 'Mirror left/right (More tools moves the axes)'}, {keys: ['Shift+M'], description: 'Mirror top/bottom as well'},
    {keys: ['X'], description: 'Swap the main and secondary colours'},
    {keys: ['Right-click'], description: 'Paint with the secondary colour (transparent by default, so it erases; with Fill it erases the touching area). On a palette colour: make it the secondary'},
    {keys: ['Shift+F10'], description: 'On a focused palette colour: make it the secondary colour (the context-menu key works too)'},
    {keys: ['Alt+Click', 'Alt+Enter'], description: 'Pick a colour and keep the current tool (any tool except Select)'},
    {keys: ['Alt+Right-click'], description: 'Pick the secondary colour, with any tool'},
    {keys: ['Shift+Drag'], description: 'Snap lines to clean ratios (flat, 3:1, 2:1, 1:1, 1:2, 1:3, upright); square rectangles and circles'},
    {keys: ['Shift+T'], description: 'Tile preview 3×3 on/off'},
    {keys: ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'], description: 'Canvas focused: move the keyboard cursor'},
    {keys: ['Enter', 'Space'], description: 'Canvas focused: apply the tool; shapes and select take one press to start and one to finish'},
    {keys: ['Shift+Enter'], description: 'Canvas focused: apply the tool with the secondary colour, or shade deeper'},
    {keys: ['Shift+ArrowRight'], description: 'Draw with the pencil, eraser or shade while moving the cursor'},
    {keys: ['Alt+Shift+ArrowRight'], description: 'Shade deeper while moving the cursor (Shade tool)'},
    {keys: ['Escape'], description: 'Cancel the shape, then clear the selection'},
    {keys: ['Mod+A'], description: 'Select all'}, {keys: ['Mod+C', 'Mod+X', 'Mod+V'], description: 'Copy, cut, paste (paste floats; palette-checked)'},
    {keys: ['Delete', 'Backspace'], description: 'Clear the selected pixels'}, {keys: ['Alt+Drag'], description: 'Copy the selection while moving it'},
    {keys: ['Alt+ArrowLeft', 'Alt+ArrowRight', 'Alt+ArrowUp', 'Alt+ArrowDown'], description: 'Move the selection 1 px, or shift the sprite'},
    {keys: ['W'], description: 'Wrap nudges round inside the selection or sprite'},
    {keys: ['Shift+H', 'Shift+V'], description: 'Flip horizontally, vertically (selection or sprite)'}, {keys: ['Shift+R'], description: 'Rotate 90° clockwise'},
    {keys: ['[', ']', '0'], description: 'Zoom out, in, fit'}, {keys: ['Mod+Wheel'], description: 'Zoom at the pointer'}, {keys: ['H', 'Middle-drag'], description: 'Hand tool / pan'},
    {keys: ['Z'], description: 'Fit content: zoom to the drawn pixels'},
    {keys: ['G'], description: 'Pixel grid'}, {keys: ['Shift+G'], description: 'Centre and tile-centre guides'},
    {keys: ['Space'], description: 'Hold to peek at the before image (until the keyboard cursor is in use)'},
    {keys: ['Mod+Z'], description: 'Undo; the history list jumps to any of the last 40 steps'}, {keys: ['Mod+Shift+Z', 'Mod+Y'], description: 'Redo'},
    {keys: ['Mod+S'], description: 'Save the asset'}]);

  /* ---------- server-backed actions ---------- */
  async function save() {
    const a = asset(), p = composite(a.key);
    if (!dirty(a.key) || pendingSaves.has(a.key)) return;
    if (!p.data.some((v, i) => i % 4 === 3 && v)) return status('An asset needs at least one pixel', 'bad');
    const submitted = L.snapshot(ensure(a.key).doc), document = L.pack(ensure(a.key).doc), pixels = [];
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) pixels.push(pixel(p, x, y));
    paintSaveErrors.delete(a.key); pendingSaves.add(a.key); ++writeEpoch; renderToolbarState();
    try {
      const e = edits[a.key], send = base => api('POST', '/api/save', {key: a.key, pixels, artist: state.artist, base, document});
      const res = await send(e.base ?? a.sha).catch(async err => {
        if (err.status !== 409 || !await askOverwrite(a.key)) throw err;
        return send(err.body.current);
      });
      // Only the submitted pixels reached disk; painting may have continued while
      // awaiting the request or conflict dialog. Keep those later edits and drafts.
      e.saved = submitted; e.savedSignature = signature(L.restore(submitted)); a.paint_document = document; e.base = a.sha = res.sha; a.hand_painted = true; D.version = res.version;
      draft(a.key);
      if (asset()?.key === a.key) renderHeader(a);
      renderList();
      const newer = dirty(a.key) ? ' Newer edits are still unsaved.' : '';
      if (res.git_error) {
        paintSaveErrors.set(a.key, `Saved; commit failed: ${res.git_error}`);
        status(`Saved ${a.key}, but the commit failed: ${res.git_error}${newer}`, 'bad', true);
      }
      else status(`Saved ${a.key}${res.commit ? ` (commit ${res.commit})` : ''}.${newer}`, '', false, {keep: true});
    } catch (err) { paintSaveErrors.set(a.key, err.message); status(`Save failed: ${err.message}`, 'bad', true); }
    finally { pendingSaves.delete(a.key); ++writeEpoch; renderToolbarState(); }
  }
  async function askOverwrite(key) {
    const text = `${key} changed on disk since you started editing it. Overwrite it with your version?`;
    return ask(text, {title: 'File changed on disk', confirmLabel: 'Overwrite', danger: true});
  }
  async function tidy() {
    if (!writable()) return;
    const a = asset(), p = work(a.key), pixels = [];
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) pixels.push(pixel(p, x, y));
    try {
      const res = await api('POST', '/api/tidy', {key: a.key, pixels});
      if (S.previewTidy && !await S.previewTidy(a, pixels, res.pixels)) return;  // lint_ui.js: before/after, Apply or Cancel
      // A reload may have swapped the buffer, or the sprite changed, while the request or preview was open.
      const now = work(a.key);
      if (asset()?.key !== a.key || !now || pixels.some((hex, i) => pixel(now, i % now.w, Math.floor(i / now.w)) !== hex))
        return status('The sprite changed while Tidy was open; run Tidy again', 'warn');
      state.float = null;
      res.pixels.forEach((hex, i) => T.set(now, i % now.w, Math.floor(i / now.w), hex)); done('Tidy outline');
      status('Tidied: closed outline, specks removed. Undo if you prefer the old version.');
    } catch (err) { status(`Tidy failed: ${err.message}`, 'bad', true); }
  }
  async function editSlot(index, color) {
    color = color.toLowerCase(); const old = D.palette[index], name = slotLabel(index);
    if (color === old) return;
    if (Object.keys(edits).some(dirty)) return status('Save or revert your edits before changing the palette', 'warn', true);
    const users = count(D.assets.filter(x => x.after_metrics.colors[old]).length, 'asset');
    const text = `Change ${name} from ${old} to ${color} in every sprite?\n\n${users} use it. This rewrites their PNGs and the shared palette; git can undo it.`;
    if (!await ask(text, {title: `Change ${name} in ${users}`, confirmLabel: 'Change colour', danger: true})) return S.renderPalette(document.getElementById('palette'), asset());
    // The dialog does not block the page, so a reload may have changed the slot meanwhile; the server checks `base` too.
    if (D.palette[index] !== old) { rerender(); return status(`${name} changed to ${D.palette[index]} while you were deciding; nothing was rewritten`, 'warn', true); }
    try {
      const res = await api('POST', '/api/palette', {index, color, base: old, artist: state.artist});
      if (state.color === old) state.color = color;
      if (state.color2 === old) state.color2 = color;
      await reload(); rerender(); status(`${name} is now ${color} in ${count(res.changed.length, 'asset')}`, '', false, {keep: true});
    } catch (err) {
      if (err.status !== 409) return status(`Palette change failed: ${err.message}`, 'bad', true);
      await reload(); rerender();
      status(`${name} is now ${err.body.current} on disk, so nothing was rewritten. Pick the new colour again if you still want it.`, 'warn', true);
    }
  }

  /* ---------- keyboard ---------- */
  const SHIFTED = {h: () => transform('Flip horizontal', T.flipH), v: () => transform('Flip vertical', T.flipV), r: () => transform('Rotate clockwise', r => T.rotate(r, true)),
    f: () => toggle('filled', 'Filled shapes'), p: () => toggle('perfect', 'Pixel-perfect'), g: () => toggle('guides', 'Guides'),
    m: () => toggle('mirrorV', 'Mirror top/bottom'), t: () => toggle('tiled', 'Tile preview')};
  const MODDED = {a: selectAll, c: copySel, x: cutSel, v: paste};
  S.keydown = e => {
    const mod = e.metaKey || e.ctrlKey, k = e.key.toLowerCase();
    if (e.repeat && !(mod && k === 'z')) return false;
    if (state.mode !== 'paint' || state.view !== 'detail' || !view) return false;
    if (mod && k === 's') { e.preventDefault(); save(); return true; }
    if (mod && k === 'z') { e.preventDefault(); e.shiftKey ? redo() : undo(); return true; }
    if (mod && k === 'y') { e.preventDefault(); redo(); return true; }
    if (state.mode !== 'paint' || state.view !== 'detail' || !view) return false;
    if (mod && !e.altKey && !e.shiftKey && MODDED[k]) { e.preventDefault(); MODDED[k](); return true; }
    if (mod) return false;
    if (e.altKey && ARROWS[e.key]) { e.preventDefault(); nudge(...ARROWS[e.key]); return true; }
    if (e.altKey) return false;
    if (e.key === 'Escape') return cancel() || (state.sel ? (deselect(), true) : false);
    if (e.key === 'Delete' || e.key === 'Backspace') { e.preventDefault(); return deleteSel(); }
    if (e.shiftKey) { if (SHIFTED[k]) { SHIFTED[k](); return true; } return false; }
    const tool = {p: 'pencil', e: 'eraser', f: 'fill', c: 'picker', t: 'shade', l: 'line', r: 'rect', u: 'ellipse', v: 'select', q: 'wand', h: 'hand'}[k];
    if (tool) { setTool(tool); return true; }
    if (k === 'x') { swapColours(); return true; }  // Paint only: Review keeps X for "needs work"
    if (k === 'm') { toggle('mirror', 'Mirror'); return true; }
    if (k === 'w') { toggle('wrap', 'Wrap'); return true; }
    if (k === 'z') { fitContent(); return true; }
    return false;
  };
})();

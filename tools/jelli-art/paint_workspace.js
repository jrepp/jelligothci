/* Contextual, asset-focused Paint workspace; reference layers never enter exported pixels. */
'use strict';
(() => {
  const S = window.Studio, L = window.JelliLayers, shell = window.JelliShell;
  if (!S) return;
  const esc = x => String(x).replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);
  let origin = null, browsing = false, advanced = false;
  let viewOptions = null, viewOptionsOpen = false, workspaceOpen = false;
  const restoreViewOptions = () => { if (viewOptions) { viewOptionsOpen = viewOptions.open; for (const node of [...viewOptions.children].slice(1)) document.querySelector('#detail>.bar').insertBefore(node, document.getElementById('mode-extra')); viewOptions.remove(); viewOptions = null; } };
  const closeLibrary = () => {
    browsing = false; document.documentElement.classList.remove('paint-browse'); window.JelliTouch.setList(false);
    document.getElementById('paint-browse')?.setAttribute('aria-pressed', 'false');
    document.getElementById('paint-browse')?.focus({preventScroll: true});
  };
  const close = document.createElement('button'); close.id = 'paint-library-close'; close.textContent = 'Return to Paint'; close.onclick = closeLibrary;
  document.querySelector('aside').prepend(close);
  document.querySelector('aside').addEventListener('click', e => { if (state.mode === 'paint' && window.JelliTouch.drawerOn() && /** @type {Element} */(e.target).closest('.item')) closeLibrary(); });
  S.openPaint = (key, context = {}) => {
    if (!byKey[key] || byKey[key].kind === 'font') return;
    origin = {label: context.label || 'artwork', view: state.view, mode: state.mode, key: state.key, ...context};
    browsing = workspaceOpen = false; window.JelliTouch.setList(false); state.key = key; shell.activateMode('paint');
    document.getElementById('paint-back')?.focus({preventScroll: true});
  };
  const back = () => {
    const from = origin; origin = null;
    if (from?.back) from.back();
    else { state.view = from?.view || 'detail'; state.mode = from?.mode === 'paint' ? 'side' : from?.mode || 'side'; state.key = from?.key || state.key; renderView(); savePrefs(); }
  };
  document.head.insertAdjacentHTML('beforeend', `<style>
    html:not([data-mode=paint]) #paint-library-close{display:none}
    #paint-library-close{min-height:44px;margin:8px}
    html.paint-contextual .layout{grid-row:2}
    html.paint-contextual:not(.paint-workspace-open) body>header,html.paint-contextual:not(.paint-workspace-open) .shell-guide{display:none}
    html[data-mode=paint]:not(.paint-browse) .layout{grid-template-columns:minmax(0,1fr)}
    html[data-mode=paint]:not(.paint-browse):not([data-list=open]) .layout>aside{display:none}
    html[data-mode=paint] #detail>.title,html[data-mode=paint] #legend,html[data-mode=paint] .paint-tool-help{display:none}
    .paint-view-options>summary{cursor:pointer;font-size:12px;padding:8px 0}
    .paint-view-options>div{margin-bottom:8px}
    #paint-tools-more{font-size:11px;padding:4px;white-space:normal;min-height:44px}
    @container stage (max-width:649px){.paint-toolrail{display:flex;gap:4px;align-items:start}.paint-toolrail #tools{flex:1;min-width:0}#paint-tools-more{max-width:50px}}
    html[data-mode=paint] .paint-work{--side:280px}
    html[data-mode=paint] .paint-side{gap:12px}
    .paint-navigation{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:8px}
    .paint-navigation strong{flex:1;overflow-wrap:anywhere;font-size:14px}
    .paint-navigation button,.paint-layer-actions button,.paint-layer button{min-height:44px}
    #paint-layers{min-width:0;border:1px solid var(--line);border-radius:8px;padding:8px}
    #paint-layers h3{font-size:13px;margin:0 0 8px}
    .paint-layer-list{list-style:none;padding:0;margin:0;display:grid;gap:4px;max-height:270px;overflow:auto}
    .paint-layer{display:grid;grid-template-columns:minmax(0,1fr) 44px 44px;gap:4px}
    .paint-layer>button:first-child{display:flex;align-items:center;gap:8px;text-align:left;min-width:0}
    .paint-layer span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
    .paint-layer canvas{width:28px;height:28px;object-fit:contain;image-rendering:pixelated;background:repeating-conic-gradient(#555 0 25%,#29292d 0 50%) 0 0/8px 8px;flex:none}
    .paint-layer [aria-pressed=true]{border-color:var(--accent)}
    .paint-layer-actions{display:flex;gap:4px;flex-wrap:wrap;margin-top:8px}
    #paint-layer-name{width:100%;box-sizing:border-box;min-height:36px;margin-top:8px}
    .paint-reference-layer{display:flex;gap:8px;align-items:center;margin-top:10px;font-size:12px}
    .paint-reference-layer input{min-width:24px;min-height:24px}
    .paint-reference-details>summary,.paint-hint-details>summary{font-size:12px;cursor:pointer;padding:8px 0}
    html[data-mode=paint] #tools:not(.paint-advanced) button:not([data-tool=pencil]):not([data-tool=eraser]):not([data-tool=fill]):not([data-tool=select]):not([data-tool=hand]):not([aria-pressed=true]){display:none}
    .paint-toolrail .paint-tool-name{font-size:11px}
    .paint-toolrail kbd{display:none}
    #paint-attachments{margin-top:10px;border-top:1px solid var(--line);padding-top:8px;font-size:12px}
    #paint-attachments summary{cursor:pointer;padding:6px 0}
    #paint-attachments select{width:100%;min-width:0;min-height:44px}
    #paint-attachments .paint-layer-actions{margin:6px 0}
    .paint-attachment-coordinates{display:flex;gap:12px}
    #paint-attachments label{display:block;margin:6px 0}
    #paint-attachments input[type=number]{width:70px;min-height:36px}
    @media(max-width:600px){html[data-mode=paint] main{padding:8px}.paint-navigation strong{flex-basis:100%;order:-1}.paint-work{gap:8px}.paint-side{display:flex;flex-direction:column}#paint-layers{order:-1}}
  </style>`);
  S.renderLayers = () => {
    const root = document.getElementById('paint-layers'); if (!root) return;
    const focused = root.contains(document.activeElement) ? document.activeElement : null;
    const focusId = focused?.id, focusLayer = focused?.getAttribute('data-layer'), focusVisible = focused?.getAttribute('data-visible'), focusLock = focused?.getAttribute('data-lock');
    const doc = S.layerDocument(), active = doc.layers[doc.active];
    root.innerHTML = `<h3>Layers <span class="lbl">${doc.layers.length}/${L.limit}</span></h3><ol class="paint-layer-list" aria-label="Artwork layers, top first">${doc.layers.map((l, i) => ({l, i})).reverse().map(({l, i}) => `<li class="paint-layer"><button data-layer="${i}" aria-pressed="${i === doc.active}" aria-label="Select ${esc(l.name)}"><canvas width="${asset().width}" height="${asset().height}" aria-hidden="true"></canvas><span>${esc(l.name)}</span></button><button data-visible="${i}" aria-label="Show ${esc(l.name)}" aria-pressed="${l.visible}">${l.visible ? '◉' : '○'}</button><button data-lock="${i}" aria-label="Lock ${esc(l.name)}" aria-pressed="${l.locked}">${l.locked ? '▣' : '□'}</button></li>`).join('')}</ol>
      <input id="paint-layer-name" aria-label="Layer name" maxlength="48" required value="${esc(active.name)}">
      <div class="paint-layer-actions"><button id="paint-layer-add" ${doc.layers.length >= L.limit ? 'disabled' : ''}>+ Layer</button><button id="paint-layer-copy" ${doc.layers.length >= L.limit ? 'disabled' : ''}>Duplicate</button><button id="paint-layer-up" aria-label="Move layer up" ${doc.active === doc.layers.length - 1 ? 'disabled' : ''}>↑</button><button id="paint-layer-down" aria-label="Move layer down" ${!doc.active ? 'disabled' : ''}>↓</button><button id="paint-layer-delete" ${doc.layers.length === 1 ? 'disabled' : ''}>Delete layer</button></div>
      ${window.JelliAnimation?.paintContext(asset().key) ? `<label class="paint-reference-layer"><input id="paint-onion-layer" type="checkbox" ${state.onionSkin ? 'checked' : ''}>Onion skin <span class="lbl">reference · not exported</span></label>` : ''}`;
    root.querySelectorAll('[data-layer]').forEach(b => {
      const i = Number(b.getAttribute('data-layer')), c = b.querySelector('canvas');
      c.getContext('2d').putImageData(new ImageData(doc.layers[i].data, c.width, c.height), 0, 0);
      b.addEventListener('click', () => S.selectLayer(i));
    });
    const change = (label, fn) => S.changeLayers(label, fn);
    for (const [attr, prop] of [['visible', 'visible'], ['lock', 'locked']]) root.querySelectorAll(`[data-${attr}]`).forEach(b => {
      b.addEventListener('click', () => change(prop === 'visible' ? 'Layer visibility' : 'Layer lock', d => { const l = d.layers[Number(b.getAttribute(`data-${attr}`))]; l[prop] = !l[prop]; }));
    });
    const on = (id, fn) => { root.querySelector(`#paint-layer-${id}`).addEventListener('click', fn); };
    on('add', () => change('Add layer', d => { d.layers.splice(++d.active, 0, {name: `Layer ${d.layers.length + 1}`, visible: true, locked: false, data: new Uint8ClampedArray(active.data.length)}); }));
    on('copy', () => change('Duplicate layer', d => { d.layers.splice(++d.active, 0, {...active, name: `${active.name.slice(0, 43)} copy`, locked: false, data: active.data.slice()}); }));
    on('delete', () => change('Delete layer', d => { d.layers.splice(d.active, 1); d.active = Math.min(d.active, d.layers.length - 1); }));
    for (const [id, delta] of [['up', 1], ['down', -1]]) on(id, () => change('Reorder layer', d => { const i = d.active, j = i + delta; [d.layers[i], d.layers[j]] = [d.layers[j], d.layers[i]]; d.active = j; }));
    root.querySelector('#paint-layer-name').addEventListener('change', e => {
      const input = /** @type {HTMLInputElement} */(e.target), name = input.value.trim();
      if (name) change('Rename layer', d => { d.layers[d.active].name = name; }); else { input.value = active.name; input.reportValidity(); }
    });
    root.querySelector('#paint-onion-layer')?.addEventListener('change', e => { state.onionSkin = /** @type {HTMLInputElement} */(e.target).checked; store.set('onion-skin', state.onionSkin); renderStage(); });
    S.renderAttachments?.(root, asset());
    if (focused) {
      const target = focusId ? document.getElementById(focusId) : root.querySelector(focusLayer !== null ? `[data-layer="${focusLayer}"]` : focusVisible !== null ? `[data-visible="${focusVisible}"]` : `[data-lock="${focusLock}"]`);
      /** @type {HTMLElement} */(target)?.focus({preventScroll:true});
    }
  };
  S.restoreCanvasOptions = restoreViewOptions;
  S.mountCanvasOptions = () => {
    restoreViewOptions();
    const inspector = document.querySelector('.paint-side');
    if (inspector) {
      viewOptions = document.createElement('details'); viewOptions.className = 'paint-view-options'; viewOptions.open = viewOptionsOpen; viewOptions.innerHTML = '<summary>Canvas options</summary>';
      for (const id of ['backdrops', 'grid']) { const node = document.getElementById(id)?.closest('.bar > div'); if (node && node.closest('.bar')) viewOptions.append(node); }
      inspector.append(viewOptions);
    }
  };
  const previous = renderView;
  renderView = () => {
    restoreViewOptions();
    const result = previous(), painting = state.view === 'detail' && state.mode === 'paint';
    if (!painting) origin = null;
    document.documentElement.classList.toggle('paint-contextual', painting && !!origin);
    document.documentElement.classList.toggle('paint-workspace-open', workspaceOpen);
    document.documentElement.classList.toggle('paint-browse', browsing && painting);
    document.getElementById('paint-navigation')?.remove();
    document.getElementById('review-paint')?.remove();
    if (!painting) {
      if (state.view === 'detail' && asset()?.kind !== 'font') {
        const button = document.createElement('button'); button.id = 'review-paint'; button.textContent = 'Paint this artwork';
        button.onclick = () => S.openPaint(state.key, {label: 'review'});
        document.querySelector('#detail .title').append(button);
      }
      return result;
    }
    const nav = document.createElement('nav'); nav.id = 'paint-navigation'; nav.className = 'paint-navigation'; nav.setAttribute('aria-label', 'Paint asset navigation');
    nav.innerHTML = `<button id="paint-back">← Back to ${esc(origin?.label || 'review')}</button><strong>${esc(asset().key)}</strong><button id="paint-browse" aria-pressed="${browsing}">Browse assets</button>${origin ? `<button id="paint-workspace" aria-expanded="${workspaceOpen}">Workspace</button>` : ''}`;
    document.getElementById('detail').prepend(nav);
    nav.querySelector('#paint-back').addEventListener('click', back);
    nav.querySelector('#paint-workspace')?.addEventListener('click', () => { workspaceOpen = !workspaceOpen; document.documentElement.classList.toggle('paint-workspace-open', workspaceOpen); nav.querySelector('#paint-workspace').setAttribute('aria-expanded', String(workspaceOpen)); if (workspaceOpen) document.getElementById('tab-paint')?.focus(); });
    nav.querySelector('#paint-browse').addEventListener('click', () => { browsing = !browsing; document.documentElement.classList.toggle('paint-browse', browsing); nav.querySelector('#paint-browse').setAttribute('aria-pressed', String(browsing)); if (window.JelliTouch.drawerOn()) window.JelliTouch.setList(browsing); else if (browsing) document.getElementById('query').focus(); });
    const rail = document.getElementById('paint-toolrail');
    if (rail && !rail.querySelector('#paint-tools-more')) {
      const more = document.createElement('button'); more.id = 'paint-tools-more'; more.textContent = 'More tools'; more.setAttribute('aria-expanded', String(advanced)); rail.append(more);
      more.onclick = () => { advanced = !advanced; document.getElementById('tools').classList.toggle('paint-advanced', advanced); more.setAttribute('aria-expanded', String(advanced)); };
    }
    document.getElementById('tools')?.classList.toggle('paint-advanced', advanced);
    S.mountCanvasOptions();
    S.renderLayers();
    return result;
  };
})();

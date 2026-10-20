/* Scene authoring and source-pixel motion preview; see RFC-011. */
'use strict';
(() => {
  const S = window.Studio, shell = window.JelliShell;
  if (!S) return;
  const clone = value => JSON.parse(JSON.stringify(value));
  const esc = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'}[c]));
  let work, loaded, base, limits = {scenes:32, layers:12}, editable = false;
  let selected = 0, layerIndex = 0, saving = false, loading = false, frame = 0, previousTime = 0;
  let travel = 0, tiltX = 0, tiltY = 0, playing = false, speed = 8, showPet = true;
  let solo = false, variationOpen = false, undo = [], redo = [], checkpoint = null;
  const snapshot = () => ({data:clone(work), selected, layerIndex});
  function resetHistory() { undo=[]; redo=[]; checkpoint=work ? snapshot() : null; }
  function remember() {
    if (!work) return;
    if (checkpoint && JSON.stringify(checkpoint.data)!==JSON.stringify(work)) {
      undo.push(checkpoint); if(undo.length>50) undo.shift(); redo=[];
    }
    checkpoint=snapshot();
  }
  function restoreHistory(back) {
    if(!editable || loading) return;
    const from=back?undo:redo, to=back?redo:undo;
    if(!from.length) return;
    to.push(snapshot()); const entry=from.pop();
    work=clone(entry.data); selected=entry.selected; layerIndex=entry.layerIndex;
    checkpoint=snapshot(); render(); button(back?'#scene-undo':'#scene-redo').focus();
  }
  const dirty = () => !!work && JSON.stringify(work) !== JSON.stringify(loaded);
  const oldDirty = S.clipsDirty;
  S.clipsDirty = () => oldDirty?.() || dirty();
  shell.registerDirty('scenes', {label:'Scenes', mode:'scenes', check:dirty});
  document.getElementById('views').insertAdjacentHTML('beforeend', '<button data-view="scenes" aria-pressed="false">Scenes</button>');
  document.querySelector('main').insertAdjacentHTML('beforeend', '<section id="scenes" class="hidden"></section>');
  shell.registerViewRoot('scenes');
  const root = document.getElementById('scenes');
  // The shared shortcut router skips form fields; saving must still work there.
  root.addEventListener('keydown',e=>{
    if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='s'){e.preventDefault();save();}
  });
  /** @type {any} */ (window).JelliDesign?.watch(root);
  const input = selector => /** @type {HTMLInputElement} */ (root.querySelector(selector));
  const button = selector => /** @type {HTMLButtonElement} */ (root.querySelector(selector));
  document.head.insertAdjacentHTML('beforeend', `<style>
    #scenes{container:scenes/inline-size; padding:12px}
    .scene-layout{display:grid;grid-template-columns:200px minmax(240px,1fr) 240px;gap:20px;align-items:start}
    .scene-layout section,.scene-layout aside{min-width:0}
    .scene-layout fieldset{min-width:0;margin:0}
    .scene-layout label{display:grid;gap:4px;margin:10px 0}
    .scene-layout input,.scene-layout select{width:100%;min-width:0;box-sizing:border-box}
    .scene-layout input[type=checkbox]{width:auto}
    .scene-layout button{min-height:44px}
    .scene-list{display:grid;gap:6px;margin:12px 0}
    .scene-list button{text-align:left;overflow-wrap:anywhere;display:flex;align-items:center;gap:8px;min-width:0}
    .scene-list img{width:32px;height:32px;object-fit:contain;image-rendering:pixelated;flex:none}
    .scene-layout summary{cursor:pointer;min-height:44px;display:flex;align-items:center;font-weight:bold}
    .scene-layout details summary::before{content:'▸';margin-right:8px}
    .scene-layout details[open] summary::before{content:'▾'}
    .scene-preview{position:sticky;top:12px}
    @media(max-height:800px){.scene-preview{position:static}}
    .scene-inspector .scene-actions button{flex:1 1 90px}
    .scene-list [aria-pressed=true]{outline:2px solid var(--accent)}
    #scene-canvas{display:block;width:100%;max-width:512px;aspect-ratio:1;image-rendering:pixelated;background:#30312b;border-radius:50%;margin:auto}
    .scene-actions{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0}
    .scene-help{color:var(--muted);font-size:13px}
    .scene-controls{display:grid;grid-template-columns:1fr 1fr;gap:0 16px}
    @container scenes (max-width:850px){.scene-layout{grid-template-columns:170px minmax(0,1fr)}.scene-inspector{grid-column:1/-1}}
    @container scenes (max-width:520px){.scene-layout{display:flex;flex-direction:column}.scene-layout>*{width:100%}.scene-preview{order:-1;position:static}.scene-list{grid-template-columns:1fr 1fr}}
  </style>`);
  const scene = () => work?.scenes[selected];
  const layer = () => scene()?.layers[layerIndex];
  const layerName = l => l.name || l.asset.replace('backgrounds.','');
  const variation = l => l.random || {seed:0,x:0,y:0,travel:0,chance:100};
  const layerLabel = (l,i) => `${i+1}. ${layerName(l)}${l.visible?'':' (hidden)'}`;
  const assets = () => D.assets.filter(a => a.kind === 'backgrounds');
  const options = (items, chosen) => items.map(([value, label]) => `<option value="${esc(value)}"${value===chosen?' selected':''}>${esc(label)}</option>`).join('');
  function changed() { shell.sync(); }
  function status() {
    remember();
    for(const [id,stack] of [['undo',undo],['redo',redo]]) {
      const control=button(`#scene-${id}`); if(control)control.disabled=!editable||loading||!stack.length;
    }
    root.querySelectorAll('[data-layer-label]').forEach(el=>{
      const i=Number(el.getAttribute('data-layer-label'));el.textContent=layerLabel(scene().layers[i],i);
    });
    const option=root.querySelector('#scene-select option:checked'); if(option)option.textContent=scene().name;

    const el = root.querySelector('#scene-status');
    if (el) el.textContent = saving ? 'Saving…' : dirty() ? 'Unsaved changes' : base === null ? 'Starter scene — save to create' : 'Saved';
    const save = button('#scene-save');
    if (save) save.disabled = saving || (!dirty() && base !== null) || !editable;
    const engine = button('#scene-engine');
    if (engine) {
      engine.textContent=dirty()||base===null?'Save and test in game':'Test saved scene in game';
      engine.disabled=saving||loading||((dirty()||base===null)&&!editable);
    }
    const resolved = root.querySelector('#scene-resolved');
    if (resolved && scene() && layer()) {
      const value=resolveSceneLayer(scene(),layer());
      resolved.textContent=`This seed: ${value.visible?'shown':'not shown'} · position ${value.x}, ${value.y} · travel ${value.travel}%`;
    }
    changed();
  }
  async function load(discard = false) {
    if (loading || saving) return;
    if (discard && dirty() && !await shell.confirm('Discard unsaved scene edits?', {confirmLabel:'Reload saved'})) return;
    loading = true;
    const startedWith = JSON.stringify(work);
    try {
      const result = await S.api('GET', '/api/scenes');
      // A load must never replace edits made while the response was pending.
      if (JSON.stringify(work) === startedWith) {
        loaded = clone(result.data); work = clone(loaded); base = result.base;
        limits = result.limits; editable = result.editable; selected = 0; layerIndex = 0;
        resetHistory();
        if (discard) window.JelliDrafts?.drop('scenes', 'scenes');
      } else S.status('Kept scene edits made while reload was pending.', 'warn');
    } catch (e) { S.status(e.message, 'bad', true); }
    finally { loading = false; if (state.view === 'scenes') render(); }
  }
  async function save() {
    if (saving || loading || !editable) return false;
    const invalid=/** @type {HTMLInputElement} */ (root.querySelector('input:invalid'));
    if(invalid){invalid.closest('details')?.setAttribute('open','');invalid.reportValidity();invalid.focus();return false;}
    if(!dirty() && base!==null) return true;
    const sent = clone(work), sentBase = base;
    saving = true; status();
    try {
      const result = await S.saveGuarded(current => S.api('POST', '/api/scenes',
        {data:sent, base:current ? current.scenes : sentBase, artist:state.artist}), 'Scenes');
      if (!result) return false;
      loaded = sent; base = result.base;
      if (!dirty()) window.JelliDrafts?.drop('scenes', 'scenes');
      S.status(result.git_error ? `Saved; commit pending: ${result.git_error}` : 'Scenes saved', result.git_error ? 'warn' : '');
      return !dirty();
    } catch (e) { S.status(e.message, 'bad', true); }
    finally { saving = false; status(); }
  }
  function draw(now) {
    if (state.view !== 'scenes' || !scene()) return;
    const canvas = /** @type {HTMLCanvasElement} */ (root.querySelector('#scene-canvas'));
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (playing && previousTime) travel += Math.min(100, now - previousTime) / 1000 * speed;
    previousTime = now;
    // Integral percentages have a common 6400px period for the authored 64px kit.
    // Keep preview travel bounded; arbitrary image widths are supported below.
    if (Math.abs(travel) > 1000000) { travel = 0; playing = false; }
    ctx.imageSmoothingEnabled = false; ctx.fillStyle = scene().fill; ctx.fillRect(0,0,64,64);
    for (const authored of scene().layers) {
      if(solo && authored!==layer()) continue;
      const l = resolveSceneLayer(scene(), authored);
      if (!l.visible) continue;
      const p = decoded[l.asset]?.after;
      if (!p) continue;
      const x = Math.round(l.x - tiltX * l.tilt_x - travel * l.travel / 100);
      const y = Math.round(l.y - tiltY * l.tilt_y);
      if (l.repeat_x) {
        const start = ((x % p.w) + p.w) % p.w - p.w;
        for (let at = start; at < 64; at += p.w) ctx.drawImage(p.img, at, y);
      } else ctx.drawImage(p.img, x, y);
    }
    if (showPet) {
      const pet = D.assets.find(a => a.kind === 'creatures');
      const p = pet && decoded[pet.key]?.after;
      if (p) {
        // Use the lowest opaque row, not transparent canvas padding, as the ground.
        let bottom=0;
        for(let y=0;y<p.h;y++) for(let x=0;x<p.w;x++) if(p.data[(y*p.w+x)*4+3]) bottom=y+1;
        const scale=6*64/466; // The home actor's 6x pixel scale on the 466px surface.
        ctx.drawImage(p.img,32-p.w*scale/2,256*64/466-bottom*scale,p.w*scale,p.h*scale);
      }
    }
    const readout = root.querySelector('#scene-travel-value');
    if (readout) readout.textContent = `${travel.toFixed(1)} px`;
    frame = requestAnimationFrame(draw);
  }
  function render() {
    cancelAnimationFrame(frame); previousTime = 0;
    if (!work) {
      root.innerHTML = '<h2>Background scenes</h2><p>Load a scene document to start.</p><button id="scene-load">Load scenes</button>';
      button('#scene-load').onclick = () => load(); return;
    }
    const s = scene(), l = layer();
    root.innerHTML = `<h2>Background scenes</h2>
      <p class="scene-help">Build places from reusable artwork. Preview tilt and travel here, then save and test the scene in the engine.</p>
      <div class="scene-actions"><button id="scene-save">Save</button><button id="scene-undo">Undo</button><button id="scene-redo">Redo</button><button id="scene-reload" ${saving?'disabled':''}>Reload saved</button><button id="scene-engine">Test saved scene in game</button><span id="scene-status" role="status"></span></div>
      <div class="scene-layout"><section aria-label="Scenes and layers">
      <label>Scene<select id="scene-select">${options(work.scenes.map(s=>[s.key,s.name]),s.key)}</select></label>
      <button id="scene-copy" ${work.scenes.length>=limits.scenes?'disabled':''}>Duplicate scene</button>

      <h3>Layers</h3><p class="scene-help">Back to front. Select a layer to edit its motion.</p>
      <div class="scene-list">${s.layers.map((l,i)=>`<button data-layer="${i}" aria-pressed="${i===layerIndex}"><img alt="" src="${esc(D.assets.find(a=>a.key===l.asset)?.after||'')}"><span data-layer-label="${i}">${esc(layerLabel(l,i))}</span></button>`).join('')}</div>
      <label>Art library<select id="scene-library">${options(assets().map(a=>[a.key,a.key.replace('backgrounds.','')]),l.asset)}</select></label>
      <button id="scene-add" ${s.layers.length>=limits.layers?'disabled':''}>Add artwork layer</button>
      <p class="scene-help">Artwork is shared between scenes. Paint changes every scene using it.</p><fieldset ${!editable||loading?'disabled':''}><legend>Scene settings</legend>      <label>Scene name<input id="scene-name" maxlength="48" required value="${esc(s.name)}"></label>
      <label>Scene seed<input id="scene-seed" type="number" min="0" max="65535" step="1" value="${s.seed||0}"></label>
      <button id="scene-reroll">Try another variation</button>
      <p class="scene-help">Set layer variation first, then change the seed to try another arrangement. Save the seed to reproduce it in Test in game.</p>
<label>Base color<input id="scene-fill" type="color" value="${s.fill}"></label></fieldset></section>
      <section class="scene-preview" aria-label="Motion preview"><canvas id="scene-canvas" width="64" height="64" role="img" aria-label="Circular scene and pet preview"></canvas>
      <div class="scene-controls"><label>Tilt left / right<input id="scene-tx" type="range" min="-1" max="1" step=".05" value="${tiltX}"></label>
      <label>Tilt up / down<input id="scene-ty" type="range" min="-1" max="1" step=".05" value="${tiltY}"></label>
      <label>Travel speed (px/s)<input id="scene-speed" type="range" min="-32" max="32" value="${speed}"></label>
      <label>Pet reference<input id="scene-pet" type="checkbox" ${showPet?'checked':''}></label></div>
      <div class="scene-actions"><button id="scene-play">${playing?'Pause travel':'Play travel'}</button><button id="scene-reset">Reset motion</button><output id="scene-travel-value"></output><button id="scene-solo" aria-pressed="${solo}">${solo?'Show all layers':'Preview selected layer only'}</button></div></section>
      <section class="scene-inspector" aria-label="Layer properties"><fieldset ${!editable||loading?'disabled':''}><legend>Selected layer</legend>
      <label>Layer name<input id="scene-layer-name" maxlength="48" required value="${esc(layerName(l))}"></label>
      <div class="scene-actions"><button id="scene-duplicate" ${s.layers.length>=limits.layers?'disabled':''}>Duplicate layer</button><button id="scene-back" ${layerIndex===0?'disabled':''}>Move back</button><button id="scene-front" ${layerIndex===s.layers.length-1?'disabled':''}>Move forward</button><button id="scene-remove" ${s.layers.length===1?'disabled':''}>Remove layer</button></div>
      <label>Artwork<select id="scene-asset">${options(assets().map(a=>[a.key,a.key]),l.asset)}</select></label>
      <button id="scene-paint">Paint artwork</button>
      ${[['x','Horizontal position',-256,256],['y','Vertical position',-256,256],['tilt_x','Horizontal tilt (px)',0,16],['tilt_y','Vertical tilt (px)',0,16],['travel','Travel speed (%)',0,100]].map(([key,label,min,max])=>`<label>${label}<input data-number="${key}" type="number" min="${min}" max="${max}" step="1" value="${l[key]}"></label>`).join('')}
      <details id="scene-variation" ${variationOpen?'open':''}><summary>Layer variation</summary>
      ${[['seed','Layer seed',65535],['x','Horizontal jitter (±px)',16],['y','Vertical jitter (±px)',16],['travel','Travel variation (±percentage points)',100],['chance','Appearance chance (%)',100]].map(([key,label,max])=>`<label>${label}<input data-random="${key}" type="number" min="0" max="${max}" step="1" value="${variation(l)[key]}"></label>`).join('')}
      <p class="scene-help">Jitter and speed variation are bounded around the values above; appearance is chosen per layer, not per repeat. Keep ground layers at zero vertical jitter and 100% appearance. Layer seeds stay with layers when reordered.</p>
      <p id="scene-resolved" class="scene-help"></p></details>
      <label>Repeat horizontally<input id="scene-repeat" type="checkbox" ${l.repeat_x?'checked':''}></label>
      <label>Visible<input id="scene-visible" type="checkbox" ${l.visible?'checked':''}></label>

      <p class="scene-help">Positive tilt moves scenery against the tilt. Faster near layers create depth. Moving layers repeat; leave room above and below for tilt.</p></fieldset></section></div>`;
    button('#scene-save').onclick = save;
    button('#scene-undo').onclick=()=>restoreHistory(true);
    button('#scene-redo').onclick=()=>restoreHistory(false);
    button('#scene-solo').onclick=()=>{solo=!solo;render();};
    root.querySelector('#scene-variation').addEventListener('toggle',e=>{variationOpen=/** @type {HTMLDetailsElement} */(e.target).open;});
    button('#scene-engine').onclick = async () => {
      if((dirty()||base===null) && !await save()) return;
      if(dirty()){S.status('You have newer edits. Save again to test them.','warn');return;}
      /** @type {any} */ (S).previewScene(s.key);
    };
    button('#scene-duplicate').onclick=()=>{
      const copy=clone(l);copy.name=`${layerName(l).slice(0,43)} copy`;
      copy.random={...variation(l)};s.layers.splice(layerIndex+1,0,copy);layerIndex++;render();
    };
    button('#scene-reload').onclick = () => load(true);
    input('#scene-select').onchange = () => { selected = work.scenes.findIndex(s=>s.key===input('#scene-select').value); layerIndex=0; travel=0; render(); };
    root.querySelectorAll('[data-layer]').forEach((el) => {
      /** @type {HTMLButtonElement} */ (el).onclick = () => { layerIndex = Number(el.getAttribute('data-layer')); render(); };
    });
    button('#scene-copy').onclick = () => {
      const copy = clone(s); let n = 1;
      while (work.scenes.some(s=>s.key===`scene-${n}`)) n++;
      copy.key=`scene-${n}`; copy.name=`${s.name.slice(0,42)} copy`;
      work.scenes.push(copy); selected=work.scenes.length-1; layerIndex=0; render();
    };
    button('#scene-add').onclick = () => {
      const asset=input('#scene-library').value;
      let seed=0; while(s.layers.some(l=>variation(l).seed===seed)) seed++;
      s.layers.push({name:asset.replace('backgrounds.',''),asset,x:0,y:0,tilt_x:0,tilt_y:0,travel:0,repeat_x:false,visible:true,
        random:{seed,x:0,y:0,travel:0,chance:100}});
      layerIndex=s.layers.length-1; render();
    };
    input('#scene-name').oninput = () => { s.name=input('#scene-name').value;input('#scene-name').setCustomValidity(s.name.trim()?'':'Enter a scene name.');status(); };
    input('#scene-layer-name').oninput = () => {
      l.name=input('#scene-layer-name').value;input('#scene-layer-name').setCustomValidity(l.name.trim()?'':'Enter a layer name.');
      status();
    };
    input('#scene-seed').oninput = () => {
      const field=input('#scene-seed');
      if (!field.checkValidity() || field.value==='') {return;}
      s.seed=Number(field.value);status();
    };
    button('#scene-reroll').onclick = () => {s.seed=((s.seed||0)+1)%65536;travel=0;render();};
    root.querySelectorAll('[data-random]').forEach(el => {
      const field=/** @type {HTMLInputElement} */ (el);
      field.oninput=()=>{
        if(!field.checkValidity() || field.value===''){return;}
        l.random={...variation(l),[field.dataset.random]:Number(field.value)};
        if(l.random.travel){l.repeat_x=true;input('#scene-repeat').checked=true;}
        status();
      };
    });
    input('#scene-fill').oninput = () => { s.fill=input('#scene-fill').value; status(); };
    input('#scene-asset').onchange = () => { l.asset=input('#scene-asset').value; status(); };
    button('#scene-paint').onclick = () => { state.key=l.asset; shell.activateMode('paint'); };
    root.querySelectorAll('[data-number]').forEach(el => {
      const field = /** @type {HTMLInputElement} */ (el);
      field.oninput = () => {
        if (!field.checkValidity() || field.value==='') { return; }
        l[field.dataset.number]=Number(field.value);
        if (l.travel) { l.repeat_x=true; input('#scene-repeat').checked=true; }
        status();
      };
    });
    input('#scene-repeat').onchange = () => { l.repeat_x=input('#scene-repeat').checked; if (!l.repeat_x) {l.travel=0; input('[data-number=travel]').value='0';if(l.random)l.random.travel=0;input('[data-random=travel]').value='0';} status(); };
    input('#scene-visible').onchange = () => {l.visible=input('#scene-visible').checked;status();};
    for (const [id, step] of [['back',-1],['front',1]]) button(`#scene-${id}`).onclick = () => {
      const to=layerIndex+Number(step); [s.layers[to],s.layers[layerIndex]]=[s.layers[layerIndex],s.layers[to]]; layerIndex=to; render();
    };
    button('#scene-remove').onclick = () => {s.layers.splice(layerIndex,1);layerIndex=Math.max(0,layerIndex-1);render();};
    input('#scene-tx').oninput=()=>{tiltX=Number(input('#scene-tx').value);};
    input('#scene-ty').oninput=()=>{tiltY=Number(input('#scene-ty').value);};
    input('#scene-speed').oninput=()=>{speed=Number(input('#scene-speed').value);};
    input('#scene-pet').onchange=()=>{showPet=input('#scene-pet').checked;};
    button('#scene-play').onclick=()=>{playing=!playing;button('#scene-play').textContent=playing?'Pause travel':'Play travel';};
    button('#scene-reset').onclick=()=>{playing=false;travel=tiltX=tiltY=0;render();};
    // Read-only trials must disable structural edits as well as property edits.
    for (const id of ['copy','add']) if (!editable || loading) button(`#scene-${id}`).disabled=true;
    status(); frame=requestAnimationFrame(draw);
  }
  const boot = S.boot;
  S.boot = async () => {
    await boot(); await load();
    const draft = window.JelliDrafts?.restore('scenes','scenes',base);
    if (work && draft?.data?.version === 1 && draft.data.scenes?.length) {
      work=clone(draft.data);base=draft.base;selected=layerIndex=0;resetHistory();
      shell.notify('Restored unsaved scene edits. Save to keep them.',{tone:'warn'});
    }
    window.JelliDrafts?.track('scenes','scenes',{dirty,base:()=>base,data:()=>work});
    if(state.view==='scenes')render();
  };
  shell.registerShortcuts?.('Scenes', [
    {keys:['Mod+S'],description:'Save scene edits'},
    {keys:['Mod+Z'],description:'Undo scene edit (outside text fields)'},
    {keys:['Mod+Shift+Z','Mod+Y'],description:'Redo scene edit (outside text fields)'}
  ]);
  const previous = renderView, keys = S.keydown;
  renderView = () => {
    cancelAnimationFrame(frame);
    if (state.view !== 'scenes') {root.classList.add('hidden');return previous();}
    shell.showOnly('scenes'); render();
  };
  S.keydown = e => {
    if(state.view!=='scenes')return keys?.(e);
    const typing=/** @type {HTMLElement} */(e.target)?.closest('input,textarea,[contenteditable=true]');
    if(!typing&&(e.metaKey||e.ctrlKey)&&['z','y'].includes(e.key.toLowerCase())){
      e.preventDefault();restoreHistory(e.key.toLowerCase()==='z'&&!e.shiftKey);return true;
    }
    if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='s'){e.preventDefault();save();}
    return true;
  };
})();

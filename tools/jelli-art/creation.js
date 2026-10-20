/* A single explicit creation flow; saved artifacts immediately join the shared editors. */
'use strict';
(() => {
  const S = window.Studio;
  if (!S) return;
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  document.querySelector('main').insertAdjacentHTML('beforeend', '<section id="creation" class="hidden" aria-label="Create artwork and game content"></section>');
  const root = document.getElementById('creation');
  window.JelliShell?.registerViewRoot('creation');
  window.JelliShell?.registerMode({id:'creation', label:'New', view:'creation'});
  /** @type {any} */ (window).JelliDesign?.watch(root);
  let kind = 'pet', snapshot, pendingCreated;
  const options = (items, value, label) => items.map(item => `<option value="${esc(value(item))}">${esc(label(item))}</option>`).join('');
  const field = (label, html) => `<label style="display:block;margin:12px 0">${label} ${html}</label>`;
  function render() {
    if (root.querySelector('form')) return;
    snapshot = D.creation;
    if (!snapshot) { root.innerHTML = '<h2>New</h2><p>Creation is unavailable for this served checkout.</p>'; return; }
    root.innerHTML = `<h2>New</h2><p>Create saved artwork, then paint it and test the saved scene in the engine.</p>
      <form id="creation-form" style="max-width:640px">
      ${field('Create', `<select name="kind"><option value="pet">Pet and form</option><option value="icons">Icon artwork</option><option value="props">Prop artwork</option><option value="prizes">Present artwork</option><option value="backgrounds">Background and location</option></select>`)}
      ${field('Identity', '<input name="key" required pattern="[a-z][a-z0-9-]{0,39}" maxlength="40" placeholder="my-pet">')}
      ${field('Display name', '<input name="name" required pattern="[A-Z0-9 ]{1,16}" maxlength="16" placeholder="MY PET">')}
      <div id="creation-fields"></div><button type="submit">Create and paint</button>
      <button type="button" id="creation-reload">Reload creation sources</button><p id="creation-message" role="status" aria-live="polite"></p></form>`;
    const form = root.querySelector('form'), selector = /** @type {HTMLSelectElement} */ (form.elements.namedItem('kind'));
    selector.value = kind;
    selector.onchange = () => { kind = selector.value; details(); };
    form.onsubmit = submit;
    document.getElementById('creation-reload').onclick = async () => {
      if (pendingCreated) { await openCreated(); return; }
      try { await S.reload(); snapshot = D.creation; document.getElementById('creation-message').textContent='Sources reloaded. Check your choices before creating.'; }
      catch(error) { document.getElementById('creation-message').textContent=error.message; }
    };
    details();
  }
  function details() {
    const fields = document.getElementById('creation-fields');
    if (kind === 'pet') {
      fields.innerHTML = `<p>Copy an explicitly selected artwork template into a distinct pet and form. Existing pet identities stay stable. This increment creates a single form with no evolution; choose its unlock prize and behavior.</p>
        ${field('Artwork template', `<select name="template_form" required><option value="">Choose template</option>${options(snapshot.templates, f=>f.art, f=>`${f.name} (${f.art})`)}</select>`)}
        ${field('Canvas', '<select name="size"><option>32</option><option>48</option></select>')}
        ${field('Gameplay policy', '<select name="gameplay_policy" required><option value="">Choose</option><option value="copy-template">Copy template activity costs, eligibility, bonuses and behavior repertoire</option></select>')}
        ${field('Evolution', '<select name="evolution" required><option value="">Choose</option><option value="single-form">Single form, no evolution</option></select>')}
        ${field('Behavior template', `<select name="behavior" required><option value="">Choose</option>${options(snapshot.behaviors, x=>x, x=>x)}</select>`)}
        ${['scale','icon_scale','portrait_scale'].map(name=>field(name.replaceAll('_',' '), `<input name="${name}" type="number" required min="1" max="8">`)).join('')}
        ${field('Unlock prize', `<select name="unlock_prize" required><option value="">Choose</option>${options(snapshot.prizes, p=>p.id+1, p=>p.name)}</select>`)}
        ${field('Unlock hint', '<input name="hint" required pattern="[A-Z0-9 ]{1,24}" maxlength="24">')}
        <p>Scale values are explicit. The validator checks the full copied pose set at the selected canvas size.</p>`;
      /** @type {HTMLSelectElement} */ (fields.querySelector('[name=template_form]')).onchange = event => {
        const profile = snapshot.profiles.find(p=>p.art===/** @type {HTMLSelectElement} */ (event.target).value);
        if (!profile) return;
        for (const name of ['behavior','scale','icon_scale','portrait_scale']) /** @type {HTMLInputElement | HTMLSelectElement} */ (fields.querySelector(`[name=${name}]`)).value = profile[name];
      };
    } else {
      fields.innerHTML = `${field('Canvas', `<select name="size">${snapshot.kinds[kind].map(n=>`<option>${n}</option>`).join('')}</select>`)}
        ${field('Palette (#rrggbb, separated by spaces)', `<textarea name="palette" rows="3" required style="width:100%">${esc(snapshot.palette.join(' '))}</textarea>`)}
        ${field('Placement pivot x', '<input name="pivot_x" type="number" required min="0">')}
        ${field('Placement pivot y', '<input name="pivot_y" type="number" required min="0">')}
        ${kind==='prizes'?field('Collectible registration', '<select name="collectible" required><option value="">Choose</option><option value="false">Artwork only</option><option value="true">Register as a collectible prize</option></select>'):''}
        ${kind==='backgrounds'?`${field('Location identity (letters and underscores)', '<input name="location_key" required pattern="[a-z][a-z_]*">')}${field('Location name', '<input name="location_name" required maxlength="12" pattern="[A-Z0-9 ]+">')}${field('Environment', '<select name="outdoors" required><option value="">Choose</option><option value="false">Indoors</option><option value="true">Outdoors</option></select>')}`:''}
        <p>A visible starter marker makes the new artwork paintable. Present artwork becomes collectible only when registration is selected.</p>`;
      const size = /** @type {HTMLSelectElement} */ (fields.querySelector('[name=size]'));
      const pivot = () => { const n=Number(size.value); /** @type {HTMLInputElement} */ (fields.querySelector('[name=pivot_x]')).value=String(Math.floor(n/2)); /** @type {HTMLInputElement} */ (fields.querySelector('[name=pivot_y]')).value=String(n-2); };
      size.onchange=pivot; pivot();
    }
  }
  async function submit(event) {
    event.preventDefault();
    const form = event.currentTarget, values = Object.fromEntries(new FormData(form));
    const request = {type:kind==='pet'?'pet':'asset',key:values.key,name:values.name,size:Number(values.size)};
    if (kind==='pet') {
      for (const key of ['template_form','evolution','gameplay_policy','behavior','hint']) request[key]=values[key];
      for (const key of ['scale','icon_scale','portrait_scale','unlock_prize']) request[key]=Number(values[key]);
    } else {
      request.kind=kind; request.palette=String(values.palette).trim().split(/\s+/);
      request.pivot=[Number(values.pivot_x),Number(values.pivot_y)];
      if(kind==='prizes')request.collectible=values.collectible==='true';
      if(kind==='backgrounds')request.location={key:values.location_key,name:values.location_name,outdoors:values.outdoors==='true'};
    }
    const button=form.querySelector('button[type=submit]'), message=document.getElementById('creation-message');
    button.disabled=true;message.textContent='Validating artwork and game content…';
    try {
      const result=await S.api('POST','/api/create',{request,bases:snapshot.bases,artist:state.artist});
      pendingCreated=result;await openCreated();
    } catch(error) {message.textContent=error.message;button.disabled=false;}
  }
  async function openCreated() {
    const created = pendingCreated;
    try {
      await S.reload();
      root.innerHTML='';pendingCreated=null;state.key=created.key;state.view='detail';state.mode='paint';savePrefs();
      S.rerender();S.status(created.git_error?`Created; commit pending: ${created.git_error}`:'Created. Paint the starter artwork, save it, then Test in game.');
    } catch(error) {
      document.getElementById('creation-message').textContent=`Created and saved ${created.key}, but refreshing the editor failed: ${error.message}. Use Reload creation sources to open the saved artwork.`;
    }
  }
  Object.assign(S, {duplicateCreature(art) {
    const source = D.creation?.templates.find(form => form.art === art);
    if (!source) return S.status('Choose a creature to duplicate.', 'warn');
    const frames = D.assets.filter(asset => asset.form === art);
    if (frames.some(asset => S.paintDirty(asset.key)) || (D.clips || []).some(clip => clip.key.startsWith(`${art}.`) && S.cr.clipDirty(clip.key)) || S.sizeDirty?.())
      return S.status('Save or revert this creature’s paint, clips and size edits before duplicating its saved content.', 'warn', true);
    kind = 'pet'; root.innerHTML = ''; window.JelliShell.activateMode('creation');
    const profile = snapshot.profiles.find(item => item.art === art);
    const set = snapshot.evolution_sets.find(item => item.forms.includes(source.id));
    const entry = snapshot.entries.find(item => item.evolution_set === set?.id);
    let suffix = 1, key, name;
    do { const tail = suffix === 1 ? '-copy' : `-copy-${suffix}`; key = art.slice(0,40-tail.length)+tail; name = `${source.name.slice(0,10)} COPY${suffix === 1 ? '' : suffix}`.slice(0,16); suffix++; }
    while (snapshot.templates.some(form => form.art === key || form.name === name) || snapshot.entries.some(entry => entry.name === name));
    const values = {key, name, template_form:art, size:frames[0]?.width || 32,
      gameplay_policy:'copy-template', evolution:'single-form', behavior:profile.behavior,
      scale:profile.scale, icon_scale:profile.icon_scale, portrait_scale:profile.portrait_scale,
      unlock_prize:entry?.unlock_prize || '', hint:entry?.hint || ''};
    const form = root.querySelector('form');
    for (const [name, value] of Object.entries(values)) /** @type {HTMLInputElement | HTMLSelectElement} */ (form.elements.namedItem(name)).value = String(value);
    document.getElementById('creation-message').textContent = `Duplicating ${source.name}: saved frames, clips, sizes and gameplay settings. This copy is a separate single-form pet. Review its name and unlock prize, then create it.`;
    /** @type {HTMLInputElement} */ (form.elements.namedItem('key')).focus();
  }});
  const previous=renderView;
  renderView=()=>{root.classList.toggle('hidden',state.view!=='creation');if(state.view!=='creation')return previous();window.JelliShell?.showOnly('creation');render();};
})();

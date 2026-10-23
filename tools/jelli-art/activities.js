/* Shared activity recipes: eligibility, time windows, effects and reusable prop motion. */
'use strict';
(() => {
  const S = window.Studio;
  if (!S) return;
  const esc = v => String(v).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const clone = v => JSON.parse(JSON.stringify(v));
  let work, loaded, base, selected = 0, previewForm = 0, stale = false, frame;
  const dirty = () => !!work && JSON.stringify(work) !== JSON.stringify(loaded);
  const oldDirty = S.clipsDirty;
  S.clipsDirty = () => oldDirty?.() || dirty();
  /* shell.js: the "Activities" unsaved item counts the recipes that differ (new ones included) plus the shared pet costs. */
  S.activitiesDirty = () => {
    if (!dirty()) return 0;
    const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
    const changed = work.moments.filter((m, i) => !same(m, loaded.moments[i])).length + Math.max(0, loaded.moments.length - work.moments.length);
    return changed + (same(work.pet_costs, loaded.pet_costs) ? 0 : 1) || 1;
  };
  document.getElementById('views').insertAdjacentHTML('beforeend', '<button data-view="activities" aria-pressed="false">Activities</button>');
  document.querySelector('main').insertAdjacentHTML('beforeend', '<div id="activities" class="hidden"></div>');
  const root = document.getElementById('activities');
  const design=/** @type {any} */(window).JelliDesign;
  design.guardFields(root,'#ac-revert');
  window.JelliShell?.registerViewRoot('activities');
  /** @type {any} */ (window).JelliDesign.watch(root);
  document.head.insertAdjacentHTML('beforeend', `<style>
    #activities{container:ac/inline-size}
    .ac-grid{display:grid;grid-template-columns:190px minmax(260px,600px) 220px;gap:24px}
    .ac-list{display:grid;gap:6px;align-content:start}.ac-fields label{display:block;margin:12px 0}
    .ac-fields input,.ac-fields select{margin-left:8px;max-width:260px}.ac-fields input[type=number]{width:90px}
    #ac-preview{image-rendering:pixelated;background:#171b26;border-radius:50%;width:220px;height:220px}
    /* The view's own width decides (RFC-007): list beside the fields with the preview above them, and under 800 px
     * one column with the preview pinned on top. */
    @container ac (max-width:1200px){
      .ac-grid{grid-template-columns:190px minmax(0,1fr);grid-template-areas:"lst prev" "lst fld"}
      .ac-grid>:nth-child(1){grid-area:lst}.ac-grid>:nth-child(2){grid-area:fld}.ac-grid>:nth-child(3){grid-area:prev;align-self:start}
    }
    @container ac (max-width:800px){
      .ac-grid{display:flex;flex-direction:column;gap:16px}.ac-grid>*{min-width:0}.ac-list{grid-template-columns:repeat(3,1fr)}
      .ac-grid>aside{order:-1;position:sticky;top:0;z-index:5;background:var(--bg);padding:6px 0;border:0;flex-direction:row;align-items:center;gap:12px}
      .ac-grid>aside p{display:none}.ac-grid>aside label{display:flex;align-items:center;gap:8px}
      #ac-preview{width:130px;height:130px}
    }
  </style>`);
  function sync() {
    if (base === D.activity_sha) return;
    if (dirty()) { stale = true; return; }
    work = clone(D.activity_data); loaded = clone(work); base = D.activity_sha; stale = false;
  }
  const opts = (values, chosen) => values.map(([v,n]) => `<option value="${esc(v)}" ${String(chosen)===String(v)?'selected':''}>${esc(n)}</option>`).join('');
  function field(label, key, type='number', extra='') {
    return `<label>${label}<input data-field="${key}" type="${type}" value="${esc(work.moments[selected][key])}" ${extra}></label>`;
  }
  function selectField(label,key,values) {
    return `<label>${label}<select data-field="${key}">${opts(values,work.moments[selected][key]??'')}</select></label>`;
  }
  const meters = ['satiety','energy','hygiene','amusement','social','hydration','bond'];
  const timeString = m => `${String(Math.floor(m/60)).padStart(2,'0')}:${String(m%60).padStart(2,'0')}`;
  function render() {
    sync(); cancelAnimationFrame(frame);
    if (!work) { root.textContent = 'Activity content is unavailable.'; return; }
    const m = work.moments[selected], forms = D.behavior_vocab?.forms || [];
    const sprites = D.assets.filter(a => ['menus','icons','health','props','effects','prizes'].includes(a.kind)).map(a=>[a.id,a.key]);
    root.innerHTML = `<h2>Activities</h2><p>Share one activity, icon and animation across any pets. Random activities rotate hourly. Unlocks use local time; completion prerequisites reset daily.</p>
      <p id="ac-save-status" class="save-state" role="status" data-save-state="${saveState}">${saveState==='saving'?'Saving…':saveState==='error'?'Save failed':stale?'The activity file changed on disk while you were editing. Save asks before it overwrites the newer file; Reload saved loads it instead.':dirty()?'Unsaved activity changes':'Saved activity recipes'}</p>
      <button id="ac-save" class="primary" title="Save activities (⌘S / Ctrl+S)" ${saveState==='saving'||!dirty()||!D.behavior_editable?'disabled':''}>${window.JelliIcons.svg("save")}Save activities</button>
      <button id="ac-revert">${window.JelliIcons.svg("reload")}Reload saved</button> <button id="ac-add" ${work.moments.length>=32?'disabled':''}>Add activity</button><p>New activities copy visible recipe fields, with no custom pose or prize and rewards on every run. Choose these policies explicitly below.</p>
      <div class="ac-grid"><nav class="ac-list" aria-label="Activity recipes">${work.moments.map((a,i)=>`<button data-activity="${i}" aria-pressed="${i===selected}">${esc(a.name)}</button>`).join('')}</nav>
      <div class="ac-fields"><h3>${esc(m.name)}</h3>
      ${field('Name','name','text',selected<loaded.moments.length?'readonly':'maxlength="12"')}
      ${selectField('Activity kind','kind',[['play','Play / relaxation'],['feed','Meal']])}
      ${field('Duration (seconds)','duration_s','number','min="1" max="3600"')}
      <fieldset><legend>Time window</legend><p>${timeString(m.start_minute)}–${timeString(m.end_minute)} (end excluded)</p>
      <label>Opens at<input type="time" data-time="start_minute" value="${timeString(m.start_minute)}"></label>
      <label>Closes at<input type="time" data-time="end_minute" value="${timeString(m.end_minute%1440)}"></label><p>Midnight closing means the end of the day.</p></fieldset>
      ${selectField('After completing today','requires',[[-1,'No prerequisite'],...work.moments.slice(0,selected).map(a=>[a.id,a.name])])}
      ${field('Random weight (0 = always offered)','random_weight','number','min="0" max="100"')}
      <fieldset><legend>Eligible pets (none selected = all)</legend>${forms.map(f=>`<label><input type="checkbox" data-form="${f.id}" ${m.forms.includes(f.id)?'checked':''}>${esc(f.name)}</label>`).join('')}</fieldset>
      ${selectField('Shared animation','animation',(D.activity_animations||[]).map(a=>[a,a]))}
      <fieldset><legend>Completion policy</legend>
      <label><input id="ac-daily" type="checkbox" ${m.daily_reward?'checked':''}>Costs and rewards only on the first run each local day</label><p>Later runs that day play freely without costs, gains or a prize. The first reward is claimed at acceptance, including interrupted runs.</p>
      ${selectField('Custom pose','pose',[['','Use animation pose'],...(D.creature_poses||[]).map(p=>[p,p]),...(D.state_poses||[]).map(p=>[p.name,p.name])])}
      ${selectField('Completion prize','prize',[['','None'],...(D.prize_data?.prizes||D.assets.filter(a=>a.kind==='prizes').map(a=>({key:a.key.replace(/^prizes\./,''),name:a.key}))).map(p=>[p.key,p.name])])}</fieldset>
      ${selectField('Menu icon','icon',sprites)}${selectField('Animated prop','prop',[[0,'None'],...sprites])}
      <fieldset><legend>Allowed locations</legend>${(D.activity_locations||[]).map(p=>`<label><input type="checkbox" data-place="${esc(p.key)}" ${m.locations.includes(p.key)?'checked':''}>${esc(p.name)}</label>`).join('')}
      <label><input id="ac-random-place" type="checkbox" ${m.randomize_location?'checked':''}>Choose a random allowed location at start</label><p>Otherwise stay here if allowed, or travel to the first allowed location in the catalog.</p></fieldset>
      <p>Meter points use a 0–100 scale. Costs are paid when accepted; rewards arrive on completion. Interrupted activities keep their costs and give no rewards.${m.kind==='feed'?' Meals also consume one food at start.':''}</p>
      ${['gains','costs'].map(effect=>`<fieldset><legend>${effect==='gains'?'Provides on completion':'Consumes at start'}</legend>${meters.map(n=>`<label>${n}<input data-effect="${effect}" data-meter="${n}" type="number" min="0" max="100" step="0.1" value="${(m[effect][n]||0)/10}"></label>`).join('')}</fieldset>`).join('')}
      ${field('Reward jitter (±%)','jitter_pct','number','min="0" max="25"')}
      <fieldset><legend>Pet reward bonuses (%)</legend><p>Overrides the pet’s behavior affinity for this activity. Leave blank to inherit it. Costs stay fixed.</p>${forms.map(f=>`<label>${esc(f.name)}<input data-bonus="${f.id}" type="number" min="0" max="100" placeholder="Inherit" value="${m.bonuses.find(b=>b.form===f.id)?.percent??''}"></label>`).join('')}</fieldset>
      <fieldset><legend>Pet activity costs (all recipes)</legend><p>100% is the base cost. Younger Mint spends extra energy; Axolotl spends extra hydration.</p>${forms.map(f=>{const c=work.pet_costs.find(c=>c.form===f.id);return `<h4>${esc(f.name)}</h4>${['energy','hydration'].map(n=>`<label>${n} cost %<input data-pet-cost="${f.id}" data-cost-meter="${n}_pct" type="number" min="0" max="300" value="${c?.[n+'_pct']??100}"></label>`).join('')}`;}).join('')}</fieldset>
      <p>Bond is slow progress: most recipes provide 0.1–0.4 points; other meters change by whole points.</p>
      <p>Reward ranges include jitter and this pet’s activity bonus. Existing sleep-habit effects on fun/connection and favorite-time extras also apply; meters cap at 100.</p>
      <div id="ac-effects"></div></div>
      <aside><label>Preview pet<select id="ac-form">${opts(forms.map(f=>[f.id,f.name]),previewForm)}</select></label><canvas id="ac-preview" width="220" height="220" aria-label="Approximate activity animation thumbnail"></canvas><p>Approximate animation thumbnail. Actor and prop sizes and motion differ from the engine. Save and use Test in game to verify actual placement and location.</p></aside></div>`;
    root.querySelectorAll('input[type=number]:not([data-bonus]),input[type=time]').forEach(el=>{/** @type {HTMLInputElement} */(el).required=true;});
    /** @type {NodeListOf<HTMLButtonElement>} */ (root.querySelectorAll('button[data-activity]')).forEach(b => b.onclick=()=>{selected=Number(b.dataset.activity);render();});
    const formSelect = /** @type {HTMLSelectElement} */ (root.querySelector('#ac-form'));
    formSelect.onchange=()=>{previewForm=Number(formSelect.value);render();};
    /** @type {NodeListOf<HTMLInputElement>} */ (root.querySelectorAll('input[data-time]')).forEach(el=>el.onchange=()=>{const [h,min]=el.value.split(':').map(Number); m[el.dataset.time]=(h*60+min)||(el.dataset.time==='end_minute'?1440:0);render();});
    /** @type {NodeListOf<HTMLInputElement | HTMLSelectElement>} */ (root.querySelectorAll('input[data-field],select[data-field]')).forEach(el=>el.onchange=()=>{const k=el.dataset.field; m[k]=['name','kind','location','animation','pose','prize'].includes(k)?el.value:Number(el.value);if(['pose','prize'].includes(k)&&!el.value)delete m[k];render();});
    /** @type {NodeListOf<HTMLInputElement>} */ (root.querySelectorAll('input[data-effect]')).forEach(el=>el.onchange=()=>{m[el.dataset.effect][el.dataset.meter]=Math.round(Number(el.value)*10);render();});
    /** @type {NodeListOf<HTMLInputElement>} */ (root.querySelectorAll('input[data-bonus]')).forEach(el=>el.onchange=()=>{const form=Number(el.dataset.bonus);m.bonuses=m.bonuses.filter(b=>b.form!==form);if(el.value!=='')m.bonuses.push({form,percent:Number(el.value)});render();});
    /** @type {NodeListOf<HTMLInputElement>} */ (root.querySelectorAll('input[data-place]')).forEach(el=>el.onchange=()=>{m.locations=Array.from(/** @type {NodeListOf<HTMLInputElement>} */ (root.querySelectorAll('input[data-place]:checked')),x=>x.dataset.place);render();});
    const daily = /** @type {HTMLInputElement} */ (root.querySelector('#ac-daily'));
    daily.onchange=()=>{m.daily_reward=daily.checked;render();};
    const randomPlace = /** @type {HTMLInputElement} */ (root.querySelector('#ac-random-place'));
    randomPlace.onchange=()=>{m.randomize_location=randomPlace.checked;render();};
    /** @type {NodeListOf<HTMLInputElement>} */ (root.querySelectorAll('input[data-pet-cost]')).forEach(el=>el.onchange=()=>{const c=work.pet_costs.find(c=>c.form===Number(el.dataset.petCost));c[el.dataset.costMeter]=Number(el.value);render();});
    /** @type {NodeListOf<HTMLInputElement>} */ (root.querySelectorAll('input[data-form]')).forEach(el=>el.onchange=()=>{m.forms=Array.from(/** @type {NodeListOf<HTMLInputElement>} */ (root.querySelectorAll('input[data-form]:checked')),x=>Number(x.dataset.form));render();});
    /** @type {HTMLButtonElement} */ (root.querySelector('#ac-save')).onclick=save;
    /** @type {HTMLButtonElement} */ (root.querySelector('#ac-revert')).onclick=async()=>{
      if (dirty() && !await S.ask('Discard your unsaved activity edits and reload the saved recipes?', {title: 'Discard activity edits', confirmLabel: 'Discard edits', danger: true})) return;
      work=null;base=null;window.JelliDrafts?.drop('activities','activities');render();};
    /** @type {HTMLButtonElement} */ (root.querySelector('#ac-add')).onclick=()=>{const {pose, prize, daily_reward, ...copy}=clone(m);  // explicit new recipe policy
      work.moments.push({...copy,id:work.moments.length,name:`ACTIVITY ${work.moments.length}`,suggest_hour:-1,requires:-1,daily_reward:false});selected=work.moments.length-1;render();};
    effectPreview(m, forms);
    preview();
  }
  function effectPreview(m, forms) {
    const bonus=m.bonuses.find(b=>b.form===previewForm);
    const form=forms.find(f=>f.id===previewForm);
    const affinity=(D.behavior_data?.repertoires||[]).find(r=>(r.forms||[]).includes(form?.name))?.affinities?.find(a=>a.moment===m.name);
    const percent=bonus?.percent??affinity?.bonus_pct??0;
    const profile=work.pet_costs.find(c=>c.form===previewForm);
    const cost=n=>Math.ceil((m.costs[n]||0)*(profile?.[n+'_pct']??100)/100)/10;
    const rows=meters.filter(n=>m.gains[n]||m.costs[n]).map(n=>{
      const gain=m.gains[n]||0, factor=100+percent;
      const low=Math.round(gain*factor*(100-m.jitter_pct)/10000)/10;
      const high=Math.round(gain*factor*(100+m.jitter_pct)/10000)/10;
      return `<tr><td>${n}</td><td>${gain?`+${low.toFixed(1)}–${high.toFixed(1)}`:'—'}</td><td>${m.costs[n]?`−${cost(n)}`:'—'}</td></tr>`;
    }).join('');
    root.querySelector('#ac-effects').innerHTML=`<h4>${esc(form?.name||'Pet')} reward range (+${percent}%)</h4><table><thead><tr><th>Meter</th><th>Provides</th><th>Consumes</th></tr></thead><tbody>${rows}</tbody></table>`;
  }
  function preview() {
    if (state.view !== 'activities') return;
    const canvas=/** @type {HTMLCanvasElement} */ (root.querySelector('#ac-preview')); if (!canvas) return;
    const ctx=canvas.getContext('2d'), m=work.moments[selected];
    ctx.clearRect(0,0,220,220);
    const form=D.behavior_vocab.forms.find(f=>f.id===previewForm);
    const pose=m.pose||(m.animation==='rest'?'asleep':['think','watch'].includes(m.animation)?'curious':['breathe','dream'].includes(m.animation)?'content':'happy');
    const actor=D.assets.find(a=>a.form===form?.art&&a.pose===pose)||D.assets.find(a=>a.form===form?.art);
    const actorPixels=actor&&decoded[actor.key]?.after;
    if(actorPixels){const off=document.createElement('canvas');off.width=actorPixels.w;off.height=actorPixels.h;off.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(actorPixels.data),actorPixels.w,actorPixels.h),0,0);ctx.imageSmoothingEnabled=false;ctx.drawImage(off,46,55,128,128);}
    const a=D.assets.find(a=>a.id===m.prop), pix=a&&decoded[a.key]?.after;
    let x=110,y=142,b=window.matchMedia('(prefers-reduced-motion: reduce)').matches?0:Math.floor(performance.now()/500)%4;
    const offsets={hold:[[0,0],[0,0],[0,0],[0,0]],sip:[[0,-12],[0,-12],[0,0],[0,0]],
      watch:[[55,0],[55,0],[55,0],[55,0]],breathe:[[0,-4],[0,-4],[0,4],[0,4]],
      jog:[[-12,0],[-12,-6],[12,0],[12,-6]],cast:[[30,-18],[36,-14],[42,-10],[48,-6]],
      dream:[[0,-90],[4,-95],[8,-100],[12,-105]],rest:[[36,-75],[36,-78],[36,-81],[36,-84]],
      think:[[-8,0],[-8,0],[8,0],[8,0]],lift:[[0,-18],[0,-18],[0,0],[0,0]],
      sketch:[[-12,0],[0,-6],[12,0],[0,6]],dig:[[24,-16],[24,0],[24,12],[24,0]],
      kick:[[-16,10],[12,0],[42,-15],[62,-5]],volley:[[-30,-35],[0,-75],[30,-35],[0,0]],
      swim:[[-30,0],[-10,-8],[10,0],[30,-8]],swing:[[-35,-15],[-10,-25],[20,-10],[50,5]],
      catch:[[-30,-35],[-10,-50],[20,-45],[35,-20]],mix:[[-8,-8],[8,-8],[8,8],[-8,8]]};
    const offset=(offsets[m.animation]||offsets.hold)[b];x+=offset[0];y+=offset[1];
    if(pix){const off=document.createElement('canvas');off.width=pix.w;off.height=pix.h;off.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(pix.data),pix.w,pix.h),0,0);ctx.imageSmoothingEnabled=false;ctx.drawImage(off,x-24,y-24,48,48);}
    frame=requestAnimationFrame(preview);
  }
  let saveState = 'idle';
  async function save() {
    if (saveState === 'saving' || !design.validFields(root) || !dirty()) return;
    const sent = clone(work);
    saveState = 'saving'; render();
    try {
      const res=await S.saveGuarded(current=>S.api('POST','/api/content',{docs:{activities:sent},bases:{activities:current?.activities??base},artist:state.artist}),'Activities');
      if(!res)return;  // Cancel keeps the edits and the newer file
      loaded=clone(sent);D.activity_data=clone(sent);D.activity_sha=base=res.shas.activities;stale=false;D.version=res.version;if(!dirty())window.JelliDrafts?.drop('activities','activities');
      S.status(res.git_error?`Saved; commit pending: ${res.git_error}`:'Activities saved',res.git_error?'warn':'');render();
    } catch(e){saveState='error';S.status(e.message,'bad',true);}
    finally { if(saveState==='saving')saveState='idle';render(); }
  }
  /* Drafts (drafts.js): unsaved recipes survive a reload or a Safari tab eviction. */
  function restoreDraft() {
    const Drafts=window.JelliDrafts, d=Drafts?.restore('activities','activities',D.activity_sha); if (!d) return;
    sync();
    const doc=d.data;
    if (!work || !doc || !Array.isArray(doc.moments) || !Array.isArray(doc.pet_costs)) return Drafts.drop('activities','activities');
    work=clone(doc);
    if (!dirty()) { work=clone(loaded); return Drafts.drop('activities','activities'); }
    if (d.stale) base=d.base;  // sync() marks it stale; Save sends this base, the server answers 409 and the page asks before overwriting
    window.JelliShell?.notify(`Restored unsaved activity edits from your last visit${d.stale?' (the file changed on disk since)':''}.`,{tone:'warn',id:'drafts-activities',hint:d.stale?'Save asks before it overwrites the newer file; Reload saved discards your edits.':'Save to keep them, or Reload saved to discard them.'});
  }
  const baseBoot=S.boot;
  S.boot=async()=>{
    await baseBoot();
    restoreDraft();
    window.JelliDrafts?.track('activities','activities',{dirty,base:()=>base,data:()=>work});
  };
  const previous=renderView, oldKeys=S.keydown;
  renderView=()=>{const on=state.view==='activities';root.classList.toggle('hidden',!on);cancelAnimationFrame(frame);if(!on)return previous();
    window.JelliShell?.showOnly('activities');
    /** @type {NodeListOf<HTMLButtonElement>} */ (document.querySelectorAll('#views button')).forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.view==='activities')));render();};
  S.keydown=e=>{if(state.view!=='activities')return oldKeys?.(e);if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='s'){e.preventDefault();if(dirty())save();}return true;};
})();

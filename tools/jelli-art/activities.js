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
  document.getElementById('views').insertAdjacentHTML('beforeend', '<button data-view="activities" aria-pressed="false">Activities</button>');
  document.querySelector('main').insertAdjacentHTML('beforeend', '<div id="activities" class="hidden"></div>');
  const root = document.getElementById('activities');
  document.head.insertAdjacentHTML('beforeend', `<style>
    .ac-grid{display:grid;grid-template-columns:190px minmax(260px,600px) 220px;gap:24px}
    .ac-list{display:grid;gap:6px;align-content:start}.ac-fields label{display:block;margin:12px 0}
    .ac-fields input,.ac-fields select{margin-left:8px;max-width:260px}.ac-fields input[type=number]{width:90px}
    #ac-preview{image-rendering:pixelated;background:#171b26;border-radius:50%;width:220px;height:220px}
    @media(max-width:1000px){.ac-grid{grid-template-columns:1fr}.ac-list{grid-template-columns:repeat(3,1fr)}}
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
    return `<label>${label}<select data-field="${key}">${opts(values,work.moments[selected][key])}</select></label>`;
  }
  const timeString = m => `${String(Math.floor(m/60)).padStart(2,'0')}:${String(m%60).padStart(2,'0')}`;
  function render() {
    sync(); cancelAnimationFrame(frame);
    if (!work) { root.textContent = 'Activity content is unavailable.'; return; }
    const m = work.moments[selected], forms = D.behavior_vocab?.forms || [];
    const sprites = D.assets.filter(a => ['menus','icons','health','props','effects','prizes'].includes(a.kind)).map(a=>[a.id,a.key]);
    root.innerHTML = `<h2>Activities</h2><p>Share one activity, icon and animation across any pets. Random activities rotate hourly. Unlocks use local time; completion prerequisites reset daily.</p>
      <p role="status">${stale?'Content changed elsewhere. Reload before saving.':dirty()?'Unsaved activity changes':'Saved activity recipes'}</p>
      <button id="ac-save" ${stale||!dirty()||!D.behavior_editable?'disabled':''}>Save activities</button>
      <button id="ac-revert">Reload saved</button> <button id="ac-add" ${work.moments.length>=32?'disabled':''}>Add activity</button>
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
      ${selectField('Menu icon','icon',sprites)}${selectField('Animated prop','prop',[[0,'None'],...sprites])}
      ${selectField('Location','location',[['stay','Stay here'],['home','Home'],['garden','Garden']])}
      <fieldset><legend>Additional need gains</legend>${['satiety','energy','hygiene','amusement','social'].map(n=>`<label>${n}<input data-gain="${n}" type="number" min="0" max="1000" value="${m.gains[n]||0}"></label>`).join('')}</fieldset></div>
      <aside><label>Preview pet<select id="ac-form">${opts(forms.map(f=>[f.id,f.name]),previewForm)}</select></label><canvas id="ac-preview" width="220" height="220" aria-label="Shared prop animation preview"></canvas><p>Shared prop motion. In-game placement follows each pet’s bounds.</p></aside></div>`;
    root.querySelectorAll('[data-activity]').forEach(b => b.onclick=()=>{selected=Number(b.dataset.activity);render();});
    root.querySelector('#ac-form').onchange=e=>{previewForm=Number(e.target.value);};
    root.querySelectorAll('[data-time]').forEach(el=>el.onchange=()=>{const [h,min]=el.value.split(':').map(Number); m[el.dataset.time]=(h*60+min)||(el.dataset.time==='end_minute'?1440:0);render();});
    root.querySelectorAll('[data-field]').forEach(el=>el.onchange=()=>{const k=el.dataset.field; m[k]=['name','kind','location','animation'].includes(k)?el.value:Number(el.value);render();});
    root.querySelectorAll('[data-gain]').forEach(el=>el.onchange=()=>{m.gains[el.dataset.gain]=Number(el.value);render();});
    root.querySelectorAll('[data-form]').forEach(el=>el.onchange=()=>{m.forms=Array.from(root.querySelectorAll('[data-form]:checked'),x=>Number(x.dataset.form));render();});
    root.querySelector('#ac-save').onclick=save;
    root.querySelector('#ac-revert').onclick=()=>{work=null;base=null;render();};
    root.querySelector('#ac-add').onclick=()=>{work.moments.push({...clone(m),id:work.moments.length,name:`ACTIVITY ${work.moments.length}`,suggest_hour:-1,requires:-1});selected=work.moments.length-1;render();};
    preview();
  }
  function preview() {
    if (state.view !== 'activities') return;
    const canvas=root.querySelector('#ac-preview'); if (!canvas) return;
    const ctx=canvas.getContext('2d'), m=work.moments[selected];
    ctx.clearRect(0,0,220,220);
    const form=D.behavior_vocab.forms.find(f=>f.id===previewForm);
    const pose=m.animation==='rest'?'asleep':['think','watch'].includes(m.animation)?'curious':['breathe','dream'].includes(m.animation)?'content':'happy';
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
  async function save() {
    try {
      const res=await S.api('POST','/api/content',{docs:{activities:work},bases:{activities:base},artist:state.artist});
      loaded=clone(work);D.activity_data=clone(work);D.activity_sha=base=res.shas.activities;D.version=res.version;
      S.status(res.git_error?`Saved; commit pending: ${res.git_error}`:'Activities saved',res.git_error?'warn':'');render();
    } catch(e){S.status(e.message,'bad',true);}
  }
  const previous=renderView, oldKeys=S.keydown;
  renderView=()=>{const on=state.view==='activities';root.classList.toggle('hidden',!on);cancelAnimationFrame(frame);if(!on)return previous();
    for(const id of ['detail','sheet','creature','behaviour'])document.getElementById(id)?.classList.add('hidden');
    document.querySelectorAll('#views button').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.view==='activities')));render();};
  S.keydown=e=>{if(state.view!=='activities')return oldKeys?.(e);if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='s'){e.preventDefault();if(dirty()&&!stale)save();}return true;};
})();

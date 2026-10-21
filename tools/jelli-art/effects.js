/* Celebration authoring, sharing shell, Paint, creature clips, drafts and touch controls. */
'use strict';
(() => {
  const S=window.Studio, shell=window.JelliShell, M=/** @type {any} */(window).JelliEffects;
  if(!S)return;
  const clone=x=>JSON.parse(JSON.stringify(x));
  const esc=x=>String(x).replace(/[&<>"']/g,c=>`&#${c.charCodeAt(0)};`);
  const opts=(items,value)=>items.map(([k,n])=>`<option value="${esc(k)}" ${k===value?'selected':''}>${esc(n)}</option>`).join('');
  let work,loaded,base,editable=false,saving=false,loading=false,loadingTask=null;
  let selected=0,kind='tracks',index=0,form='',point=0,time=0,playing=false,last=0,raf=0;
  let undo=[],redo=[],checkpoint,showPoints=true,placing=false;
  const dirty=()=>!!work&&JSON.stringify(work)!==JSON.stringify(loaded);
  const effect=()=>work.effects[selected];
  const item=()=>effect()[kind][index];
  const forms=()=>[...new Set(D.assets.filter(a=>a.kind==='creatures').map(a=>a.form))].filter(f=>!work||work.hardpoints.every(p=>p.positions[f]));
  const art=()=>D.assets.filter(a=>['effects','props','icons'].includes(a.kind));
  const oldDirty=S.clipsDirty;
  S.clipsDirty=()=>oldDirty?.()||dirty();
  shell.registerDirty('effects',{label:'Effects & hardpoints',mode:'effects',check:dirty});
  document.getElementById('views').insertAdjacentHTML('beforeend','<button data-view="effects" aria-pressed="false">Effects</button>');
  document.querySelector('main').insertAdjacentHTML('beforeend','<section id="effects" class="hidden"></section>');
  shell.registerViewRoot('effects');
  const root=document.getElementById('effects');
  const el=id=>/** @type {HTMLInputElement} */(root.querySelector(`#fx-${id}`));
  const on=(id,fn)=>{if(el(id))el(id).onclick=fn;};
  /** @type {any} */(window).JelliDesign?.watch(root);
  document.head.insertAdjacentHTML('beforeend',`<style>
    #effects{container:effects/inline-size;padding:12px}
    #effects h2{margin:0 0 8px}.fx-stage{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:14px}.fx-stage h3{margin:0 0 8px}
    .fx-layout{display:grid;grid-template-columns:minmax(280px,1fr) 300px;gap:20px;align-items:start}
    .fx-layout>*{min-width:0}.fx-layout label{display:grid;gap:4px;margin:8px 0}
    #effects input[type=range]{width:100%}
    #effects input,#effects select{min-width:0;max-width:100%;box-sizing:border-box}
    #effects fieldset{min-width:0}.fx-actions{display:flex;flex-wrap:wrap;gap:8px;margin:10px 0}
    #effects button{min-height:44px}.fx-fields{display:grid;grid-template-columns:1fr 1fr;gap:8px}
    #fx-canvas{display:block;width:100%;max-width:466px;aspect-ratio:1;border-radius:50%;image-rendering:pixelated;background:#30312b;margin:auto;touch-action:none}
    .fx-row{display:grid;grid-template-columns:130px minmax(0,1fr);gap:8px;align-items:center;margin:6px 0}
    .fx-row button{overflow-wrap:anywhere;text-align:left}.fx-bar{position:relative;background:var(--line);height:28px;border-radius:4px;overflow:hidden}
    .fx-span{position:absolute;height:100%;background:var(--accent);opacity:.6}.fx-playhead{position:absolute;height:100%;width:2px;background:var(--ink)}
    #effects [aria-pressed=true]{outline:2px solid var(--accent)}.fx-note{color:var(--muted);font-size:13px}
    .fx-frame{display:grid;grid-template-columns:minmax(0,1fr) 75px;gap:6px;margin:8px 0}
    .fx-frame img{width:32px;height:32px;image-rendering:pixelated}
    @container effects (max-width:520px){.fx-stage{grid-template-columns:1fr}}
    @container effects (max-width:760px){.fx-layout{grid-template-columns:1fr}.fx-row{grid-template-columns:100px minmax(0,1fr)}}
  </style>`);
  function remember() {
    if(checkpoint&&JSON.stringify(checkpoint)!==JSON.stringify(work)){undo.push(checkpoint);if(undo.length>50)undo.shift();redo=[];}
    checkpoint=clone(work);
  }
  function history(back) {
    const from=back?undo:redo,to=back?redo:undo;
    if(!editable||loading||!from.length)return;
    to.push(clone(work));work=from.pop();checkpoint=clone(work);selected=Math.min(selected,work.effects.length-1);point=Math.min(point,work.hardpoints.length-1);render();
  }
  function status() {
    if(!work)return;
    remember();shell.sync();
    const chosen=root.querySelector('#fx-select option:checked');if(chosen)chosen.textContent=effect().name;
    const pt=root.querySelector('#fx-point option:checked');if(pt)pt.textContent=work.hardpoints[point].name;
    if(el('status'))el('status').textContent=saving?'Saving…':dirty()?'Unsaved changes':base===null?'Starter — save to create':'Saved';
    if(el('save'))el('save').disabled=!editable||loading||saving||(!dirty()&&base!==null);
    if(el('undo'))el('undo').disabled=!editable||loading||!undo.length;
    if(el('redo'))el('redo').disabled=!editable||loading||!redo.length;
    if(el('engine')){el('engine').disabled=saving||loading;el('engine').textContent=dirty()||base===null?'Save and test in game':'Test saved effect in game';}
    root.querySelectorAll('[data-track-name]').forEach(n=>{n.textContent=effect().tracks[Number(n.getAttribute('data-track-name'))]?.name||'';});
  }
  function load(discard=false) {
    if(loadingTask)return loadingTask;
    loadingTask=fetchEffects(discard).finally(()=>{loadingTask=null;});
    return loadingTask;
  }
  async function fetchEffects(discard=false) {
    if(loading||saving)return;
    if(discard&&dirty()&&!await shell.confirm('Discard unsaved effect and hardpoint edits?',{confirmLabel:'Reload saved'}))return;
    loading=true;
    const before=JSON.stringify(work);
    try {
      const r=await S.api('GET','/api/effects');
      if(before!==JSON.stringify(work))return;
      work=clone(r.data);loaded=clone(work);base=r.base;editable=r.editable;selected=index=point=0;form=forms()[0];
      undo=[];redo=[];checkpoint=clone(work);
      if(discard)window.JelliDrafts?.drop('effects','effects');
    } catch(e){S.status(e.message,'bad',true);}
    finally{loading=false;if(state.view==='effects')render();}
  }
  async function save() {
    if(!editable||saving||loading)return false;
    const invalid=/** @type {HTMLInputElement} */(root.querySelector('input:invalid'));
    if(invalid){invalid.reportValidity();invalid.focus();return false;}
    if(!dirty()&&base!==null)return true;
    const sent=clone(work),sentBase=base;saving=true;status();
    try {
      const r=await S.saveGuarded(current=>S.api('POST','/api/effects',{data:sent,base:current?current.effects:sentBase,artist:state.artist}),'Effects');
      if(!r)return false;
      loaded=sent;base=r.base;
      if(!dirty())window.JelliDrafts?.drop('effects','effects');
      S.status(r.git_error?`Saved; commit pending: ${r.git_error}`:'Effects saved',r.git_error?'warn':'');
      return !dirty();
    } catch(e){S.status(e.message,'bad',true);return false;}
    finally{saving=false;status();}
  }
  function number(label,key,value,min,max){return `<label>${label}<input required data-fx="${key}" type="number" min="${min}" max="${max}" step="1" value="${value}"></label>`;}
  function inspector() {
    const t=item();if(!t)return '<p>Add a sprite track or creature cue to begin.</p>';
    return `<label>${kind==='tracks'?'Track':'Cue'} name<input type="text" required maxlength="48" data-text="name" value="${esc(t.name)}"></label>
      <div class="fx-actions"><button id="fx-duplicate">Duplicate ${kind==='tracks'?'track':'cue'}</button><button id="fx-remove">Remove</button>${kind==='tracks'?'<button id="fx-earlier">Move back</button><button id="fx-later">Move forward</button>':''}</div>
      <div class="fx-fields">${number('Start (ms)','start_ms',t.start_ms,0,29999)}${number('Duration (ms)','duration_ms',t.duration_ms,1,30000)}</div>
      ${kind==='cues'?`<label>Creature pose<select data-text="pose">${opts(M.poses.map(p=>[p,p]),t.pose)}</select></label><p class="fx-note">Restarts this pose’s clip at the cue’s start. Cues cannot overlap.</p>`:`
      <label>Render layer<select data-text="layer">${opts(M.layers.map((k,i)=>[k,['Behind pet','In front of pet','Foreground'][i]]),t.layer)}</select></label>
      <label>Attachment<select data-text="anchor">${opts([['screen','Screen origin'],...work.hardpoints.map(p=>[p.key,p.name])],t.anchor)}</select></label>
      <p class="fx-note">Positions are display-pixel offsets from the attachment. Tracks in each layer draw in list order.</p>
      <div class="fx-fields">${[['x','Start X'],['y','Start Y'],['end_x','End X'],['end_y','End Y']].map(([k,n])=>number(n,k,t[k],-512,512)).join('')}${number('Sprite scale','scale',t.scale,1,8)}</div>
      <label>Motion<select data-text="easing">${opts([['linear','Linear'],['smooth','Ease in and out']],t.easing)}</select></label>
      <label>Repeat frames<input id="fx-loop" type="checkbox" ${t.loop?'checked':''}></label>
      <h4>Sprite frames</h4>${t.frames.map((f,i)=>`<div class="fx-frame"><label>Frame ${i+1}<select data-frame-asset="${i}">${opts(art().map(a=>[a.key,a.key]),f.asset)}</select></label><label>Hold (ms)<input required type="number" min="1" max="30000" data-frame-hold="${i}" value="${f.duration_ms}"></label><button data-paint="${i}">Paint frame ${i+1}</button><button data-remove-frame="${i}" ${t.frames.length===1?'disabled':''}>Remove</button></div>`).join('')}
      <button id="fx-add-frame" ${t.frames.length>=6?'disabled':''}>Add sprite frame</button>`}`;
  }
  function triggers() {
    return `<fieldset ${!editable||loading?'disabled':''}><legend>Automatic celebrations</legend>
      <p class="fx-note">Choose an effect for live gameplay events. Choose Disabled to turn one off. Playback waits for the home view; time spent offline never triggers celebrations.</p>
      ${[['evolution','Pet evolves'],['present-caught','New gift acquired'],['present-accepted','Pet accepts present']].map(([key,label])=>`<label>${label}<select data-trigger="${key}">${opts([['','Disabled'],...work.effects.map(e=>[e.key,e.name])],work.triggers?.[key]||'')}</select></label>`).join('')}</fieldset>`;
  }
  function timeline() {
    return ['cues','tracks'].map(k=>effect()[k].map((t,i)=>`<div class="fx-row"><button data-select="${k}:${i}" aria-pressed="${kind===k&&index===i}"><span ${k==='tracks'?`data-track-name="${i}"`:''}>${esc(t.name)}</span></button><div class="fx-bar" title="${t.start_ms}–${t.start_ms+t.duration_ms} ms"><span class="fx-span" style="left:${t.start_ms/effect().duration_ms*100}%;width:${t.duration_ms/effect().duration_ms*100}%"></span><span class="fx-playhead"></span></div></div>`).join('')).join('');
  }
  function render() {
    cancelAnimationFrame(raf);last=0;
    if(!work){root.innerHTML='<h2>Celebration effects</h2><button id="fx-load">Load effects</button>';on('load',()=>load());return;}
    index=Math.min(index,Math.max(0,effect()[kind].length-1));
    root.innerHTML=`<h2>Celebration effects</h2><p class="fx-note">Choreograph creature poses and layered sprite animation. Hardpoints are shared across celebrations and authored for each pet form.</p>
      <div class="fx-actions"><button id="fx-save">Save</button><button id="fx-undo">Undo</button><button id="fx-redo">Redo</button><button id="fx-reload">Reload saved</button><button id="fx-engine">Test saved effect in game</button><span id="fx-status" role="status"></span></div>
      <div class="fx-layout"><section aria-label="Celebration preview and timeline">
      <div class="fx-fields"><label>Celebration<select id="fx-select">${opts(work.effects.map(e=>[e.key,e.name]),effect().key)}</select></label>
      <label>Preview pet<select id="fx-form">${opts(forms().map(f=>[f,f]),form)}</select></label>
      </div><div class="fx-stage"><div><canvas id="fx-canvas" width="466" height="466" role="img" aria-label="Celebration and attachment preview"></canvas>
      <div class="fx-actions"><button id="fx-play">${playing?'Pause':'Play'}</button><button id="fx-restart">Replay</button><button id="fx-back" aria-label="Step back 50 milliseconds">−50 ms</button><button id="fx-next" aria-label="Step forward 50 milliseconds">+50 ms</button><output id="fx-time"></output></div>
      <label>Timeline (milliseconds)<input id="fx-scrub" type="range" min="0" max="${effect().duration_ms}" step="1" value="${time}"></label>
      </div><div><h3>Timeline</h3><div id="fx-timeline">${timeline()}</div></div></div>
      <fieldset ${!editable||loading?'disabled':''}><legend>Timeline authoring</legend><div class="fx-actions"><button id="fx-add-track" ${effect().tracks.length>=16?'disabled':''}>Add sprite track</button><button id="fx-add-cue" ${effect().cues.length>=16?'disabled':''}>Add creature cue</button><button id="fx-copy" ${work.effects.length>=32?'disabled':''}>Duplicate celebration</button></div>
      <label>Celebration name<input id="fx-name" type="text" required maxlength="48" value="${esc(effect().name)}"></label><label>Total duration (ms)<input id="fx-duration" required type="number" min="1" max="30000" value="${effect().duration_ms}"></label></fieldset>
      ${triggers()}<fieldset ${!editable||loading?'disabled':''}><legend>Pet hardpoints — ${esc(form)}</legend><label>Show attachment guides<input id="fx-guides" type="checkbox" ${showPoints?'checked':''}></label>
      <label>Hardpoint<select id="fx-point">${opts(work.hardpoints.map((p,i)=>[String(i),p.name]),String(point))}</select></label>
      <p class="fx-note">Select a hardpoint, enable placement, then tap the preview to place it. Coordinates use source pixels relative to the pet’s ground point; negative Y is above its feet. Changes affect every celebration for this form.</p>
      <button id="fx-place" aria-pressed="${placing}">${placing?'Finish placing hardpoint':'Place selected hardpoint'}</button><div class="fx-fields"><label>Hardpoint X<input id="fx-point-x" required type="number" min="-64" max="64" value="${work.hardpoints[point].positions[form][0]}"></label><label>Hardpoint Y<input id="fx-point-y" required type="number" min="-64" max="64" value="${work.hardpoints[point].positions[form][1]}"></label></div>
      <button id="fx-add-point" ${work.hardpoints.length>=8?'disabled':''}>Add named hardpoint</button><label>Hardpoint name<input id="fx-point-name" type="text" required maxlength="48" value="${esc(work.hardpoints[point].name)}"></label></fieldset></section>
      <section aria-label="Selected track or cue"><fieldset ${!editable||loading?'disabled':''}><legend>${kind==='tracks'?'Sprite track':'Creature cue'}</legend>${inspector()}</fieldset></section></div>`;
    bind();status();raf=requestAnimationFrame(draw);
  }
  function updateTimeline(){el('timeline').innerHTML=timeline();bindSelect();}
  function bindSelect(){root.querySelectorAll('[data-select]').forEach(n=>n.addEventListener('click',()=>{const parts=n.getAttribute('data-select').split(':');kind=parts[0];index=Number(parts[1]);render();}));}
  function bind() {
    on('save',save);on('undo',()=>history(true));on('redo',()=>history(false));on('reload',()=>load(true));
    on('engine',async()=>{const key=effect().key;if((dirty()||base===null)&&!await save())return;if(!dirty())/** @type {any} */(S).previewEffect(key,form);});
    el('select').onchange=()=>{selected=work.effects.findIndex(e=>e.key===el('select').value);index=0;time=0;playing=false;render();};
    el('form').onchange=()=>{form=el('form').value;render();};
    el('point').onchange=()=>{point=Number(el('point').value);render();};
    el('guides').onchange=()=>{showPoints=el('guides').checked;};
    el('name').oninput=()=>{effect().name=el('name').value;status();};
    el('duration').oninput=()=>{if(el('duration').value&&el('duration').checkValidity()){effect().duration_ms=Number(el('duration').value);el('scrub').max=el('duration').value;time=Math.min(time,effect().duration_ms);updateTimeline();status();}};
    el('point-name').oninput=()=>{work.hardpoints[point].name=el('point-name').value;status();};
    ['x','y'].forEach((axis,i)=>{el(`point-${axis}`).oninput=()=>{const f=el(`point-${axis}`);if(f.value&&f.checkValidity()){work.hardpoints[point].positions[form][i]=Number(f.value);status();}};});
    on('place',()=>{placing=!placing;showPoints=true;render();});
    on('add-point',()=>{let n=1;while(work.hardpoints.some(p=>p.key===`point-${n}`))n++;work.hardpoints.push({key:`point-${n}`,name:`Point ${n}`,positions:Object.fromEntries(forms().map(f=>[f,[0,-10]]))});point=work.hardpoints.length-1;render();});
    on('copy',()=>{let n=1;while(work.effects.some(e=>e.key===`effect-${n}`))n++;const e=clone(effect());e.key=`effect-${n}`;e.name=`${e.name.slice(0,42)} copy`;work.effects.push(e);selected=work.effects.length-1;render();});
    on('add-track',()=>{kind='tracks';effect().tracks.push({name:'Sprite track',layer:'front-pet',anchor:work.hardpoints[0].key,start_ms:0,duration_ms:effect().duration_ms,x:0,y:0,end_x:0,end_y:-60,scale:3,easing:'smooth',loop:true,frames:[{asset:(art().find(a=>a.kind==='effects')||art()[0]).key,duration_ms:150}]});index=effect().tracks.length-1;render();});
    on('add-cue',()=>{kind='cues';const start=Math.max(0,...effect().cues.map(c=>c.start_ms+c.duration_ms));if(start>=30000){S.status('No room for another cue. Shorten or remove a cue first.','warn');return;}const duration=Math.min(500,30000-start);effect().duration_ms=Math.max(effect().duration_ms,start+duration);effect().cues.push({name:'Creature cue',pose:'happy',start_ms:start,duration_ms:duration});index=effect().cues.length-1;render();});
    on('duplicate',()=>{if(effect()[kind].length>=16)return;const copy=clone(item());if(kind==='cues'){copy.start_ms=Math.max(0,...effect().cues.map(c=>c.start_ms+c.duration_ms));if(copy.start_ms+copy.duration_ms>30000){S.status('No room for the duplicated cue.','warn');return;}effect().duration_ms=Math.max(effect().duration_ms,copy.start_ms+copy.duration_ms);}effect()[kind].splice(index+1,0,copy);index++;render();});
    on('remove',()=>{effect()[kind].splice(index,1);index=Math.max(0,index-1);render();});
    for(const [id,delta] of [['earlier',-1],['later',1]])on(id,()=>{const at=index+Number(delta),list=effect().tracks;if(at<0||at>=list.length)return;[list[at],list[index]]=[list[index],list[at]];index=at;render();});
    root.querySelectorAll('[data-fx]').forEach(n=>{const f=/** @type {HTMLInputElement} */(n);f.oninput=()=>{if(f.value&&f.checkValidity()){item()[f.dataset.fx]=Number(f.value);status();updateTimeline();}};});
    root.querySelectorAll('[data-text]').forEach(n=>{const f=/** @type {HTMLInputElement} */(n);f.oninput=()=>{item()[f.dataset.text]=f.value;status();updateTimeline();};});
    if(el('loop'))el('loop').onchange=()=>{item().loop=el('loop').checked;status();};
    root.querySelectorAll('[data-frame-asset]').forEach(n=>{const f=/** @type {HTMLSelectElement} */(n);f.onchange=()=>{item().frames[Number(f.dataset.frameAsset)].asset=f.value;status();};});
    root.querySelectorAll('[data-frame-hold]').forEach(n=>{const f=/** @type {HTMLInputElement} */(n);f.oninput=()=>{if(f.value&&f.checkValidity()){item().frames[Number(f.dataset.frameHold)].duration_ms=Number(f.value);status();}};});
    root.querySelectorAll('[data-paint]').forEach(n=>n.addEventListener('click',()=>{state.key=item().frames[Number(n.getAttribute('data-paint'))].asset;shell.activateMode('paint');}));
    root.querySelectorAll('[data-remove-frame]').forEach(n=>n.addEventListener('click',()=>{item().frames.splice(Number(n.getAttribute('data-remove-frame')),1);render();}));
    on('add-frame',()=>{item().frames.push(clone(item().frames.at(-1)));render();});
    on('play',()=>{if(time>=effect().duration_ms)time=0;playing=!playing;el('play').textContent=playing?'Pause':'Play';last=0;});
    on('restart',()=>{time=0;playing=true;last=0;el('play').textContent='Pause';});
    const seek=t=>{playing=false;time=Math.max(0,Math.min(effect().duration_ms,t));el('play').textContent='Play';};
    on('back',()=>seek(time-50));on('next',()=>seek(time+50));el('scrub').oninput=()=>seek(Number(el('scrub').value));
    el('canvas').onpointerdown=e=>{if(!editable||loading||!placing)return;const r=el('canvas').getBoundingClientRect(),scale=S.cr.scaleFor?.(form)||6;
      work.hardpoints[point].positions[form]=[Math.round(((e.clientX-r.left)*466/r.width-233)/scale),Math.round(((e.clientY-r.top)*466/r.height-256)/scale)].map(v=>Math.max(-64,Math.min(64,v)));
      playing=false;render();};
    root.querySelectorAll('[data-trigger]').forEach(n=>{const f=/** @type {HTMLSelectElement} */(n);f.onchange=()=>{work.triggers??={};work.triggers[f.dataset.trigger]=f.value||null;status();};});
    bindSelect();
  }
  function draw(now) {
    if(state.view!=='effects'||!work)return;
    if(playing&&last){time=Math.min(effect().duration_ms,time+Math.min(100,now-last));if(time>=effect().duration_ms){playing=false;el('play').textContent='Play';}}
    last=now;const sample=M.sample(effect(),Math.floor(time));
    const canvas=/** @type {HTMLCanvasElement} */(root.querySelector('#fx-canvas')),ctx=canvas.getContext('2d');
    ctx.imageSmoothingEnabled=false;ctx.fillStyle='#30312b';ctx.fillRect(0,0,466,466);
    const scale=S.cr.scaleFor?.(form)||6;
    const pose=(sample.cue?.pose||'idle').replaceAll('_','-');
    const clip=S.cr.resolvedClip(`${form}.${pose}`);
    const cf=clip&&M.frameAt(clip.frames.map((asset,i)=>({asset,duration_ms:clip.durations_ms[i]})),sample.cue?time-sample.cue.start_ms:time,clip.loop);
    const a=D.assets.find(a=>a.key===cf?.asset)||D.assets.find(a=>a.form===form);
    const p=a&&decoded[a.key]?.after;
    const sprite=(key,x,y,s)=>{const p=decoded[key]?.after;if(!p)return;
      let sx=0,sy=0,n=0;for(let y=0;y<p.h;y++)for(let x=0;x<p.w;x++)if(p.data[(y*p.w+x)*4+3]){sx+=2*x+1;sy+=2*y+1;n++;}
      if(!n)return;const q=v=>Math.floor((v*128+Math.floor(n/2))/n);
      ctx.drawImage(p.img,x-Math.floor((q(sx)*s+128)/256),y-Math.floor((q(sy)*s+128)/256),p.w*s,p.h*s);};
    const layer=name=>{for(const s of sample.sprites.filter(s=>s.layer===name)){const pt=work.hardpoints.find(p=>p.key===s.anchor)?.positions[form];sprite(s.asset,s.x+(pt?233+pt[0]*scale:0),s.y+(pt?256+pt[1]*scale:0),s.scale);}};
    layer('behind-pet');
    if(p)S.cr.drawSprite(ctx,a.key,scale,233,256);
    layer('front-pet');layer('foreground');
    if(showPoints)work.hardpoints.forEach((p,i)=>{const [x,y]=p.positions[form],px=233+x*scale,py=256+y*scale;ctx.strokeStyle=i===point?'#ffffff':'#e8b87a';ctx.lineWidth=2;ctx.beginPath();ctx.moveTo(px-6,py);ctx.lineTo(px+6,py);ctx.moveTo(px,py-6);ctx.lineTo(px,py+6);ctx.stroke();ctx.fillStyle=ctx.strokeStyle;ctx.font='12px sans-serif';if(i===point)ctx.fillText(p.name,px+8,py);});
    el('time').textContent=`${Math.floor(time)} / ${effect().duration_ms} ms`;el('scrub').value=String(Math.floor(time));
    root.querySelectorAll('.fx-playhead').forEach(n=>{/** @type {HTMLElement} */(n).style.left=`${time/effect().duration_ms*100}%`;});
    raf=requestAnimationFrame(draw);
  }
  const reload=S.reload;
  S.reload=async()=>{await reload();await loadingTask;if(work&&!dirty())await load();};
  const boot=S.boot;S.boot=async()=>{await boot();await load();const d=window.JelliDrafts?.restore('effects','effects',base);
    if(work&&d?.data?.version===1&&d.data.effects?.length){work=clone(d.data);base=d.base;checkpoint=clone(work);shell.notify('Restored unsaved effects and hardpoints.',{tone:'warn'});}
    window.JelliDrafts?.track('effects','effects',{dirty,base:()=>base,data:()=>work});if(state.view==='effects')render();};
  const previous=renderView,keys=S.keydown;
  renderView=()=>{cancelAnimationFrame(raf);if(state.view!=='effects'){playing=false;return previous();}shell.showOnly('effects');render();};
  const key=e=>{if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='s'){e.preventDefault();e.stopPropagation();save();return;}
    if(!e.target.closest('input,textarea,select')&&(e.metaKey||e.ctrlKey)&&['z','y'].includes(e.key.toLowerCase())){e.preventDefault();e.stopPropagation();history(e.key.toLowerCase()==='z'&&!e.shiftKey);}};
  root.addEventListener('keydown',key);
  S.keydown=e=>{if(state.view!=='effects')return keys?.(e);key(e);return true;};
  shell.registerShortcuts('Effects',[{keys:['Mod+S'],description:'Save effects and hardpoints'},{keys:['Mod+Z'],description:'Undo effect edit outside fields'}]);
})();

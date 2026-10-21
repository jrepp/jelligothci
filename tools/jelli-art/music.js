/* Four-channel ProTracker workspace. Canonical files are ordinary M.K. MODs. */
'use strict';
(() => {
  const S=window.Studio,H=window.JelliShell;if(!S)return;
  const clone=x=>JSON.parse(JSON.stringify(x)),esc=x=>String(x).replace(/[&<>"']/g,c=>`&#${c.charCodeAt(0)};`);
  const periods=[856,808,762,720,678,640,604,570,538,508,480,453,428,404,381,360,339,320,302,285,269,254,240,226,214,202,190,180,170,160,151,143,135,127,120,113];
  const names=['C-','C#','D-','D#','E-','F-','F#','G-','G#','A-','A#','B-'];
  const note=p=>p?`${names[periods.indexOf(p)%12]}${Math.floor(periods.indexOf(p)/12)+1}`:'---';
  const hex=(n,size=2)=>n.toString(16).toUpperCase().padStart(size,'0');
  const blank=()=>({sample:0,period:0,effect:0,param:0});
  let work,loaded,base,catalog=[],song='morning-tea',editable=false,busy=false,pattern=0,row=0,channel=0,instrument=1,octave=2,step=1;
  let undo=[],clipboard=null,context=null,sources=[],gains=[],timer=0,started=0,generation=0,paused=false,shown=false;
  let mixerOpen=innerWidth>700;
  const levels=[64,64,64,64],muted=[false,false,false,false];
  const dirty=()=>!!work&&JSON.stringify(work)!==JSON.stringify(loaded);
  const cell=()=>work.patterns[pattern][row][channel];
  document.getElementById('views').insertAdjacentHTML('beforeend','<button data-view="music" aria-pressed="false">Music</button>');
  document.querySelector('main').insertAdjacentHTML('beforeend','<section id="music" class="hidden" aria-label="Music tracker"></section>');H.registerViewRoot('music');
  const root=document.getElementById('music'),f=id=>/** @type {HTMLInputElement} */(root.querySelector(`#mu-${id}`));
  H.registerDirty('music',{label:'Tracker module',mode:'music',check:dirty});const oldDirty=S.clipsDirty;S.clipsDirty=()=>oldDirty?.()||dirty();
  document.head.insertAdjacentHTML('beforeend',`<style>
  html[data-mode=music] .layout{grid-template-columns:minmax(0,1fr)}html[data-mode=music] .layout>aside{display:none}html[data-mode=music] main{grid-column:1}
  #music{padding:12px;container:music/inline-size}#music h2{margin:0 0 10px}.mu-controls{display:flex;flex-wrap:wrap;gap:10px;align-items:end;margin:10px 0}
  #music label{display:grid;gap:4px;min-width:0}#music button,#music select,#music input{min-height:40px;max-width:100%;box-sizing:border-box}#music input[type=number]{width:80px}
  #music .mu-check{display:flex;align-items:center;gap:5px}#music fieldset{min-width:0;border:1px solid var(--line);border-radius:8px;margin:12px 0;padding:10px}
  .mu-layout{display:grid;grid-template-columns:minmax(0,1fr) 290px;gap:16px}.mu-layout>*{min-width:0}.mu-scroll{max-height:580px;overflow:auto;border:1px solid var(--line);border-radius:6px}
  #mu-grid{border-collapse:collapse;width:100%;table-layout:fixed;font:12px ui-monospace,monospace}#mu-grid th{position:sticky;top:0;background:var(--panel,var(--bg));z-index:1;padding:6px 2px}
  #mu-grid th:first-child{width:30px}#mu-grid td{padding:0}#mu-grid td button{font:inherit;white-space:nowrap;padding:5px 1px;width:100%;min-height:32px;border-radius:0;border:0;background:transparent}
  #mu-grid tr:nth-child(4n+1){background:var(--line)}#mu-grid button[aria-pressed=true]{outline:2px solid var(--accent);outline-offset:-2px;background:var(--bg)}
  .mu-mix{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.mu-mix input[type=range]{width:100%;min-width:0}#mu-wave{width:100%;height:80px;background:var(--bg);border:1px solid var(--line)}
  #mu-time{width:min(100%,460px)}.mu-help{color:var(--muted);font-size:13px}#mu-sample{width:100%}
  @container music (max-width:850px){.mu-layout{grid-template-columns:1fr}.mu-scroll{max-height:420px}}
  @container music (max-width:390px){#mu-grid{font-size:10px}.mu-mix{grid-template-columns:repeat(2,minmax(0,1fr))}}
  </style>`);
  function stop(){generation++;for(const source of sources){source.onended=null;source.stop();source.disconnect();}sources=[];for(const gain of gains)gain.disconnect();gains=[];clearInterval(timer);paused=false;if(f('playing'))f('playing').textContent='Stopped';if(f('pause'))f('pause').textContent='Pause';if(f('time'))f('time').value='0';}
  function status(){H.sync();if(!work)return;f('status').textContent=busy?'Working…':dirty()?'Unsaved changes':base===null?'New module — save to create':'Saved';f('save').disabled=busy||!editable||(!dirty()&&base!==null);f('play').disabled=busy;f('reload').disabled=busy;f('undo').disabled=!undo.length||!editable;}
  function remember(){undo.push(clone(work));if(undo.length>20)undo.shift();stop();}
  function changed(focus=false){render();if(focus)/** @type {HTMLElement} */(root.querySelector(`[data-row="${row}"][data-channel="${channel}"]`))?.focus();}
  async function load(next=song,discard=false){
    if(busy)return false;
    if(discard&&dirty()&&!await H.confirm('Discard unsaved tracker edits?',{confirmLabel:'Discard and load'}))return false;
    stop();busy=true;const before=JSON.stringify(work);
    try{const r=await S.api('GET',`/api/music?song=${encodeURIComponent(next)}`);if(before!==JSON.stringify(work))throw Error('Module changed while loading; your edits are preserved.');work=clone(r.data);loaded=clone(work);base=r.base;editable=r.editable;catalog=r.songs;song=next;pattern=row=channel=0;undo=[];if(discard)window.JelliDrafts?.drop('music','module');return true;}
    catch(e){S.status(e.message,'bad',true);return false;}finally{busy=false;if(state.view==='music')render();}
  }
  async function save(){
    if(busy||!editable)return;const invalid=/** @type {HTMLInputElement} */(root.querySelector('input:invalid'));if(invalid){invalid.reportValidity();invalid.focus();return;}
    const sent=clone(work),sentBase=base,sentSong=song;busy=true;status();
    try{const r=await S.saveGuarded(current=>S.api('POST','/api/music',{song:sentSong,data:sent,base:current?current.music:sentBase,artist:state.artist}),'Music');if(!r)return;loaded=sent;base=r.base;if(!dirty())window.JelliDrafts?.drop('music','module');S.status(r.git_error?`Saved; commit pending: ${r.git_error}`:'MOD saved. Rebuild to hear it in the game.',r.git_error?'warn':'');}
    catch(e){S.status(e.message,'bad',true);}finally{busy=false;status();}
  }
  async function play(){
    if(busy||!work)return;stop();const token=generation;busy=true;status();f('playing').textContent='Preparing four channels…';
    try{context??=new AudioContext();await context.resume();const result=await S.api('POST','/api/music/preview',{data:clone(work)});if(token!==generation)return;
      const buffers=await Promise.all(result.stems.map(s=>context.decodeAudioData(Uint8Array.from(atob(s),c=>c.charCodeAt(0)).buffer)));if(token!==generation)return;
      started=context.currentTime+.03;
      buffers.forEach((buffer,i)=>{const source=context.createBufferSource(),gain=context.createGain();source.buffer=buffer;source.connect(gain);gain.gain.value=muted[i]?0:levels[i]/64;gain.connect(context.destination);sources.push(source);gains.push(gain);source.start(started);});
      sources[0].onended=()=>{clearInterval(timer);f('playing').textContent='Finished';f('time').value='30';};
      timer=setInterval(()=>{const elapsed=Math.max(0,Math.min(30,context.currentTime-started));f('time').value=String(elapsed);f('playing').textContent=`${paused?'Paused':'Playing'} ${Math.floor(elapsed)} / 30 seconds`;},250);f('playing').textContent='Playing 0 / 30 seconds';
    }catch(e){if(token===generation){stop();S.status(`Preview could not play: ${e.message}`,'bad',true);}}finally{busy=false;if(state.view==='music')status();}
  }
  async function pause(){if(!sources.length)return play();paused=!paused;if(paused)await context.suspend();else await context.resume();f('pause').textContent=paused?'Resume':'Pause';}
  function options(values,value){return values.map(([v,n])=>`<option value="${v}" ${v===value?'selected':''}>${esc(n)}</option>`).join('');}
  function redrawWave(){const canvas=/** @type {HTMLCanvasElement} */(root.querySelector('#mu-wave')),ctx=canvas.getContext('2d'),pcm=work.samples[instrument-1].pcm;ctx.clearRect(0,0,canvas.width,canvas.height);ctx.strokeStyle='#9b8cdb';ctx.beginPath();for(let x=0;x<canvas.width;x++){const v=pcm[Math.floor(x*pcm.length/canvas.width)]||0,y=40-v/128*36;if(x)ctx.lineTo(x,y);else ctx.moveTo(x,y);}ctx.stroke();}
  function render(){
    if(!work){root.innerHTML='<h2>Music tracker</h2><p>Loading records…</p>';return;}
    const c=cell(),sample=work.samples[instrument-1],sampleNames=work.samples.map((s,i)=>[i+1,`${hex(i+1)} ${s.name||'(empty)'}`]);
    root.innerHTML=`<h2>Music tracker</h2><p>Four PCM channels · standard ProTracker MOD · 30-second record preview</p>
    <div class="mu-controls"><label>Record<select id="mu-song">${options(catalog.map(s=>[s[0],`${s[1]} · ${s[2]}`]),song)}</select></label><label>Module title<input id="mu-title" maxlength="20" pattern="[ -~]*" value="${esc(work.title)}" ${editable?'':'disabled'}></label><button id="mu-save">Save MOD</button><button id="mu-reload">Reload saved</button><button id="mu-undo">Undo</button><span id="mu-status" role="status"></span></div>
    <div class="mu-controls"><button id="mu-play">Play (F5)</button><button id="mu-pause">Pause</button><button id="mu-stop">Stop (F8)</button><button id="mu-export">Export MOD</button><label>Import MOD<input id="mu-import" type="file" accept=".mod" ${editable?'':'disabled'}></label><span id="mu-playing" role="status">Stopped</span></div>
    <label>Preview progress<progress id="mu-time" max="30" value="0"></progress></label>
    <details id="mu-mixer" ${mixerOpen?'open':''}><summary>Four-channel mixer (preview only)</summary><fieldset><legend class="sr-only">Mixer channels</legend><div class="mu-mix">${levels.map((v,i)=>`<div><label>Channel ${i+1} gain<input type="range" min="0" max="64" value="${v}" data-level="${i}"></label><label class="mu-check"><input type="checkbox" data-mute="${i}" ${muted[i]?'checked':''}>Mute ${i+1}</label></div>`).join('')}</div></fieldset></details>
    <div class="mu-layout"><div><div class="mu-controls"><label>Pattern<select id="mu-pattern">${options(work.patterns.map((_,i)=>[i,hex(i)]),pattern)}</select></label><label>Octave<input id="mu-octave" type="number" min="1" max="3" value="${octave}"></label><label>Row step<input id="mu-step" type="number" min="0" max="16" value="${step}"></label><button id="mu-copy">Copy pattern</button><button id="mu-paste" ${editable?'':'disabled'}>Paste pattern</button><button id="mu-new" ${editable?'':'disabled'}>New pattern</button></div>
    <label>Order list (decimal pattern numbers)<input id="mu-orders" value="${work.orders.join(', ')}" ${editable?'':'disabled'}></label>
    <p class="mu-help">Arrow keys move. Z–M and Q–U enter notes; the upper keyboard row adds an octave. Delete clears a cell. Space pauses/resumes; F5 plays; F8 stops. Ctrl/Cmd+Shift+C/V copies/pastes a pattern.</p>
    <div class="mu-scroll"><table id="mu-grid" aria-label="Four-channel pattern"><thead><tr><th scope="col">Row</th>${[1,2,3,4].map(i=>`<th scope="col">Channel ${i}</th>`).join('')}</tr></thead><tbody>${work.patterns[pattern].map((r,y)=>`<tr><th scope="row">${hex(y)}</th>${r.map((v,x)=>`<td><button data-row="${y}" data-channel="${x}" tabindex="${y===row&&x===channel?0:-1}" aria-pressed="${y===row&&x===channel}" aria-label="${note(v.period)} ${hex(v.sample)} ${hex(v.effect,1)}${hex(v.param)}, row ${y}, channel ${x+1}">${note(v.period)} ${hex(v.sample)} ${hex(v.effect,1)}${hex(v.param)}</button></td>`).join('')}</tr>`).join('')}</tbody></table></div>
    <fieldset><legend>Selected cell: row ${row}, channel ${channel+1}</legend><div class="mu-controls"><label>Note<select id="mu-note" ${editable?'':'disabled'}>${options([[0,'No note'],...periods.map(p=>[p,note(p)])],c.period)}</select></label><label>Sample<select id="mu-cell-sample" ${editable?'':'disabled'}>${options([[0,'Keep current'],...sampleNames],c.sample)}</select></label><label>Effect<select id="mu-effect" ${editable?'':'disabled'}>${options([[0,'None'],[12,'C — volume'],[15,'F — speed/tempo']],c.effect)}</select></label><label>Value (decimal)<input id="mu-param" type="number" min="0" max="255" value="${c.param}" ${editable?'':'disabled'}></label><button id="mu-clear" ${editable?'':'disabled'}>Clear cell</button></div><p class="mu-help">C: volume 0–64. F: speed 1–31 ticks/row, or tempo 32–255 BPM. Other effects and finetuning are rejected on import.</p></fieldset></div>
    <aside aria-label="Sample manager"><fieldset><legend>Sample manager</legend><label>Instrument<select id="mu-sample">${options(sampleNames,instrument)}</select></label><canvas id="mu-wave" width="280" height="80" aria-label="Sample waveform"></canvas><p>${sample.pcm.length} bytes · ${work.samples.reduce((n,s)=>n+s.pcm.length,0)} / 32768 total</p><label>Sample name<input id="mu-sample-name" maxlength="22" pattern="[ -~]*" value="${esc(sample.name)}" ${editable?'':'disabled'}></label><div class="mu-controls"><label>Volume<input id="mu-volume" type="number" min="0" max="64" value="${sample.volume}" ${editable?'':'disabled'}></label><label>Loop start<input id="mu-loop-start" type="number" min="0" max="${sample.pcm.length}" step="2" value="${sample.loop_start}" ${editable?'':'disabled'}></label><label>Loop length<input id="mu-loop-length" type="number" min="0" max="${sample.pcm.length}" step="2" value="${sample.loop_length}" ${editable?'':'disabled'}></label></div><button id="mu-sample-play">Audition sample</button><label>Replace sample (8-bit mono WAV / signed PCM)<input id="mu-upload" type="file" accept=".wav,.pcm,.raw" ${editable?'':'disabled'}></label><p class="mu-help">At most 8192 bytes each. Loop positions use bytes; lengths are even. Zero disables looping. MOD stores no sample rate: imported data plays at tracker pitch.</p></fieldset></aside></div>`;
    bind();redrawWave();status();
  }
  function edit(fn){if(!editable)return;remember();fn();changed();}
  function bind(){
    f('mixer').ontoggle=()=>{mixerOpen=/** @type {HTMLDetailsElement} */(root.querySelector('#mu-mixer')).open;};
    f('song').onchange=async()=>{const next=f('song').value;if(!await load(next,true))f('song').value=song;};f('reload').onclick=()=>load(song,true);f('save').onclick=save;f('play').onclick=play;f('pause').onclick=pause;f('stop').onclick=stop;
    f('title').oninput=()=>{work.title=f('title').value;status();};f('undo').onclick=()=>{if(!undo.length)return;stop();work=undo.pop();pattern=Math.min(pattern,work.patterns.length-1);changed();};
    f('pattern').onchange=()=>{stop();pattern=Number(f('pattern').value);changed();};f('octave').onchange=()=>{octave=Math.max(1,Math.min(3,Number(f('octave').value)));};f('step').onchange=()=>{step=Math.max(0,Math.min(16,Number(f('step').value)));};
    f('copy').onclick=()=>{clipboard=clone(work.patterns[pattern]);H.announce('Pattern copied');};f('paste').onclick=()=>{if(clipboard)edit(()=>{work.patterns[pattern]=clone(clipboard);});};
    f('new').onclick=()=>{if(work.patterns.length>=16||work.orders.length>=16){S.status('At most 16 patterns and 16 order entries.','warn');return;}edit(()=>{work.patterns.push(Array.from({length:64},()=>Array.from({length:4},blank)));pattern=work.patterns.length-1;work.orders.push(pattern);});};
    f('orders').onchange=()=>{const parts=f('orders').value.split(',').map(v=>v.trim());const values=parts.map(Number);if(parts.some(v=>!/^\d+$/.test(v))||values.length>16||values.some(v=>v>=work.patterns.length)||Math.max(...values)!==work.patterns.length-1){S.status('Use 1–16 pattern numbers; include the highest pattern.','bad');f('orders').value=work.orders.join(', ');return;}edit(()=>{work.orders=values;});};
    root.querySelectorAll('[data-row]').forEach(n=>{n.addEventListener('click',()=>{row=Number(n.getAttribute('data-row'));channel=Number(n.getAttribute('data-channel'));changed(true);});n.addEventListener('keydown',trackerKey);});
    f('note').onchange=()=>edit(()=>{cell().period=Number(f('note').value);if(cell().period&&!cell().sample)cell().sample=instrument;});f('cell-sample').onchange=()=>edit(()=>{cell().sample=Number(f('cell-sample').value);});f('effect').onchange=()=>edit(()=>{cell().effect=Number(f('effect').value);cell().param=cell().effect===15?6:0;});f('param').onchange=()=>edit(()=>{cell().param=Number(f('param').value);});f('clear').onclick=()=>edit(()=>{work.patterns[pattern][row][channel]=blank();});
    f('sample').onchange=()=>{stop();instrument=Number(f('sample').value);changed();};for(const [id,key] of [['sample-name','name'],['volume','volume'],['loop-start','loop_start'],['loop-length','loop_length']])f(id).onchange=()=>edit(()=>{work.samples[instrument-1][key]=key==='name'?f(id).value:Number(f(id).value);});
    root.querySelectorAll('[data-level]').forEach(n=>{const el=/** @type {HTMLInputElement} */(n);el.oninput=()=>{const i=Number(el.dataset.level);levels[i]=Number(el.value);if(gains[i])gains[i].gain.value=muted[i]?0:levels[i]/64;};});root.querySelectorAll('[data-mute]').forEach(n=>{const el=/** @type {HTMLInputElement} */(n);el.onchange=()=>{const i=Number(el.dataset.mute);muted[i]=el.checked;if(gains[i])gains[i].gain.value=muted[i]?0:levels[i]/64;};});
    f('import').onchange=()=>importFile('module');f('upload').onchange=()=>importFile('sample');f('export').onclick=exportFile;f('sample-play').onclick=audition;
  }
  function trackerKey(e){
    if(e.ctrlKey||e.metaKey||e.altKey)return;let handled=true;
    if(e.key==='ArrowUp')row=(row+63)%64;else if(e.key==='ArrowDown')row=(row+1)%64;else if(e.key==='ArrowLeft')channel=(channel+3)%4;else if(e.key==='ArrowRight')channel=(channel+1)%4;else if(e.key==='PageUp')row=Math.max(0,row-16);else if(e.key==='PageDown')row=Math.min(63,row+16);else if(e.key==='Home')row=0;else if(e.key==='End')row=63;
    else if(e.key==='Delete'||e.key==='Backspace'){if(editable){remember();work.patterns[pattern][row][channel]=blank();}}
    else {const lower='zsxdcvgbhnjm',upper='q2w3er5t6y7u';const key=e.key.toLowerCase(),a=lower.indexOf(key),b=upper.indexOf(key),n=a>=0?(octave-1)*12+a:b>=0?octave*12+b:-1;if(n>=0&&n<36&&editable){remember();cell().period=periods[n];cell().sample=instrument;row=(row+step)%64;}else handled=false;}
    if(handled){e.preventDefault();e.stopPropagation();changed(true);}
  }
  async function fileBase64(file){if(file.size>131072)throw Error('File exceeds 128 KiB');const bytes=new Uint8Array(await file.arrayBuffer());let s='';for(const b of bytes)s+=String.fromCharCode(b);return btoa(s);}
  async function importFile(kind){
    const file=f(kind==='module'?'import':'upload').files?.[0];if(!file||!editable)return;
    const target=work,targetSong=song,targetInstrument=instrument,before=JSON.stringify(work);
    const unchanged=()=>{if(work!==target||song!==targetSong||JSON.stringify(work)!==before)throw Error('Module changed during import; retry to keep your edits.');};
    try{const encoded=await fileBase64(file);if(kind==='module'){const r=await S.api('POST','/api/music/import',{file:encoded});unchanged();if(dirty()&&!await H.confirm('Replace unsaved tracker edits with this MOD?',{confirmLabel:'Import MOD'}))return;edit(()=>{work=r.data;pattern=row=channel=0;});}
      else {const r=await S.api('POST','/api/music/sample',{file:encoded,format:/\.wav$/i.test(file.name)?'wav':'pcm'});unchanged();const total=work.samples.reduce((n,s,i)=>n+(i===targetInstrument-1?0:s.pcm.length),0)+r.pcm.length;if(total>32768)throw Error('Combined samples exceed 32768 bytes');edit(()=>{instrument=targetInstrument;work.samples[targetInstrument-1]={name:file.name.replace(/[^ -~]/g,'').slice(0,22),volume:64,loop_start:0,loop_length:0,pcm:r.pcm};});H.announce('Sample imported. MOD playback uses tracker pitch.');}}
    catch(e){S.status(e.message,'bad',true);}
  }
  async function exportFile(){try{const r=await S.api('POST','/api/music/export',{data:work}),bytes=Uint8Array.from(atob(r.file),c=>c.charCodeAt(0)),url=URL.createObjectURL(new Blob([bytes],{type:'audio/mod'})),a=document.createElement('a');a.href=url;a.download=`${song}.mod`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}catch(e){S.status(e.message,'bad',true);}}
  async function audition(){stop();const sample=work.samples[instrument-1];if(!sample.pcm.length)return;try{context??=new AudioContext();await context.resume();const buffer=context.createBuffer(1,sample.pcm.length,22050);buffer.getChannelData(0).set(sample.pcm.map(n=>n/128));const source=context.createBufferSource(),gain=context.createGain();source.buffer=buffer;source.playbackRate.value=8287/22050;source.loop=sample.loop_length>0;source.loopStart=sample.loop_start/22050;source.loopEnd=(sample.loop_start+sample.loop_length)/22050;gain.gain.value=sample.volume/64*.3;source.connect(gain);gain.connect(context.destination);sources=[source];gains=[gain];source.start();source.stop(context.currentTime+2);f('playing').textContent='Auditioning sample';source.onended=()=>{f('playing').textContent='Stopped';sources=[];};}catch(e){S.status(e.message,'bad',true);}}
  const boot=S.boot;S.boot=async()=>{await boot();await load();const d=window.JelliDrafts?.restore('music','module',base);if(d?.data?.doc?.patterns&&catalog.some(s=>s[0]===d.data.song)){if(d.data.song!==song)await load(d.data.song);work=clone(d.data.doc);base=d.base;H.notify('Restored unsaved tracker module.',{tone:'warn'});}window.JelliDrafts?.track('music','module',{dirty,base:()=>base,data:()=>({song,doc:work})});if(state.view==='music')render();};
  const previous=renderView;renderView=()=>{if(state.view!=='music'){shown=false;stop();return previous();}H.showOnly('music');render();if(!shown){shown=true;requestAnimationFrame(()=>document.getElementById('tab-music')?.scrollIntoView({block:'nearest',inline:'nearest'}));}};document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();});
  const keys=S.keydown;S.keydown=e=>{if(state.view!=='music')return keys?.(e);const typing=e.target.closest('input,textarea,select');if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='s'){e.preventDefault();save();return true;}if(typing)return true;if((e.metaKey||e.ctrlKey)&&e.shiftKey&&['c','v'].includes(e.key.toLowerCase())){e.preventDefault();f(e.key.toLowerCase()==='c'?'copy':'paste').click();}else if((e.metaKey||e.ctrlKey)&&e.key.toLowerCase()==='z'){e.preventDefault();f('undo').click();}else if(e.key==='F5'){e.preventDefault();play();}else if(e.key==='F8'||e.key==='Escape'){e.preventDefault();stop();}else if(e.code==='Space'){e.preventDefault();pause();}return true;};
  H.registerShortcuts('Music',[{keys:['F5'],description:'Play module from beginning'},{keys:['Space'],description:'Pause or resume'},{keys:['F8','Escape'],description:'Stop preview'},{keys:['Mod+S'],description:'Save MOD'},{keys:['Mod+Shift+C','Mod+Shift+V'],description:'Copy or paste pattern'},{keys:['Mod+Z'],description:'Undo tracker edit'}]);
})();

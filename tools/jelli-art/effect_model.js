/* Deterministic authoring preview; integer timing mirrors core/effect.c. */
'use strict';
(() => {
  const layers = ['behind-pet','front-pet','foreground'];
  const poses = ['idle','idle-alt','curious','content','eating','happy','asleep','unwell'];
  function frameAt(frames, time, loop) {
    const total=frames.reduce((sum,f)=>sum+f.duration_ms,0);
    if(!total)return frames[0];
    if(loop)time%=total;
    for(const f of frames){if(time<f.duration_ms)return f;time-=f.duration_ms;}
    return frames.at(-1);
  }
  function sample(effect,time) {
    if(time<0||time>=effect.duration_ms)return {sprites:[],cue:null};
    const cue=effect.cues.find(c=>time>=c.start_ms&&time<c.start_ms+c.duration_ms)||null;
    const sprites=effect.tracks.filter(t=>time>=t.start_ms&&time<t.start_ms+t.duration_ms).map(t=>{
      const local=time-t.start_ms;
      let p=Math.floor(local*1024/t.duration_ms);
      if(t.easing==='smooth')p=Math.trunc(Math.trunc(p*p/1024)*(3072-2*p)/1024);
      return {...t,asset:frameAt(t.frames,local,t.loop).asset,
        x:t.x+Math.trunc((t.end_x-t.x)*p/1024),y:t.y+Math.trunc((t.end_y-t.y)*p/1024)};
    });
    return {sprites,cue};
  }
  const api={layers,poses,frameAt,sample};
  if(typeof module!=='undefined')module.exports=api;
  else /** @type {any} */(window).JelliEffects=api;
})();

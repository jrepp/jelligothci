/* Authoring-only, binary-alpha layers. The game still reads a flattened PNG. */
'use strict';
(() => {
  const encode = data => { let s = ''; for (const b of data) s += String.fromCharCode(b); return btoa(s); };
  const decode = s => Uint8ClampedArray.from(atob(s), c => c.charCodeAt(0));
  const create = p => ({version: 1, layers: [{name: 'Artwork', visible: true, locked: false, data: p.data.slice()}], active: 0});
  const pack = doc => ({version: 1, layers: doc.layers.map(l => ({...l, data: encode(l.data)})), active: doc.active});
  const unpack = raw => ({...raw, layers: raw.layers.map(l => ({...l, data: decode(l.data)}))});
  const snapshot = doc => new TextEncoder().encode(JSON.stringify(pack(doc)));
  const restore = bytes => unpack(JSON.parse(new TextDecoder().decode(bytes)));
  const composite = (doc, p) => {
    const data = new Uint8ClampedArray(p.w * p.h * 4);
    for (const l of doc.layers) if (l.visible) for (let i = 0; i < data.length; i += 4) if (l.data[i + 3]) data.set(l.data.subarray(i, i + 4), i);
    return {...p, data};
  };
  const api = {encode, decode, create, pack, unpack, snapshot, restore, composite, limit: 8};
  if (typeof module !== 'undefined') module.exports = api;
  else window.JelliLayers = api;
})();

/* Jelli Art: pixel tool logic for Paint mode. Pure functions with no DOM, so
 * test_paint_tools.js can check them under node. A buffer is {w, h, data} with
 * data an RGBA byte array (4 bytes per pixel, binary alpha); a rect is {x, y, w, h}.
 * Colours are lowercase '#rrggbb' strings, and null means transparent. */
'use strict';
(root => {
  /* ---------- rasterisers: each returns [[x, y], ...] with no duplicates ---------- */

  /* Bresenham: 8-connected, one pixel per step on the major axis, so no doubled corners.
   * Always traced from the leftmost (then topmost) end, so A→B and B→A give the same pixels. */
  function line(x0, y0, x1, y1) {
    if (x1 < x0 || (x1 === x0 && y1 < y0)) return trace(x1, y1, x0, y0).reverse();
    return trace(x0, y0, x1, y1);
  }
  function trace(x0, y0, x1, y1) {
    const pts = [], dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    for (;;) {
      pts.push([x0, y0]);
      if (x0 === x1 && y0 === y1) return pts;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  const box = (x0, y0, x1, y1) => ({x0: Math.min(x0, x1), y0: Math.min(y0, y1), x1: Math.max(x0, x1), y1: Math.max(y0, y1)});
  function rect(ax, ay, bx, by, filled = false) {
    const {x0, y0, x1, y1} = box(ax, ay, bx, by), pts = [];
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++)
      if (filled || y === y0 || y === y1 || x === x0 || x === x1) pts.push([x, y]);
    return pts;
  }
  /* The ellipse inscribed in a pixel box (Zingl's plotEllipseRect), symmetric for odd and even sizes. */
  function ellipse(ax, ay, bx, by, filled = false) {
    let {x0, y0, x1, y1} = box(ax, ay, bx, by);
    const seen = new Set(), out = [], add = (x, y) => { const k = x + ',' + y; if (!seen.has(k)) { seen.add(k); out.push([x, y]); } };
    let a = x1 - x0, b = y1 - y0, b1 = b & 1;
    let dx = 4 * (1 - a) * b * b, dy = 4 * (b1 + 1) * a * a, err = dx + dy + b1 * a * a;
    y0 += (b + 1) >> 1; y1 = y0 - b1;
    const aa = 8 * a * a, bb = 8 * b * b;
    do {
      add(x1, y0); add(x0, y0); add(x0, y1); add(x1, y1);
      const e2 = 2 * err;
      if (e2 <= dy) { y0++; y1--; err += dy += aa; }
      if (e2 >= dx || 2 * err > dy) { x0++; x1--; err += dx += bb; }
    } while (x0 <= x1);
    while (y0 - y1 <= b) { add(x0 - 1, y0); add(x1 + 1, y0++); add(x0 - 1, y1); add(x1 + 1, y1--); }
    if (!filled) return out;
    const rows = new Map();
    for (const [x, y] of out) { const r = rows.get(y); rows.set(y, r ? [Math.min(r[0], x), Math.max(r[1], x)] : [x, x]); }
    const pts = [];
    for (const [y, [lo, hi]] of [...rows].sort((p, q) => p[0] - q[0])) for (let x = lo; x <= hi; x++) pts.push([x, y]);
    return pts;
  }
  /* Freehand clean-up: drop the corner pixel of every L, so strokes stay one pixel wide. */
  function pixelPerfect(pts) {
    const out = [];
    for (const p of pts) {
      const last = out[out.length - 1];
      if (last && last[0] === p[0] && last[1] === p[1]) continue;
      out.push(p);
      if (out.length >= 3) {
        const [a, c] = [out[out.length - 3], p], mid = out[out.length - 2];
        const ortho = (u, v) => Math.abs(u[0] - v[0]) + Math.abs(u[1] - v[1]) === 1;
        if (ortho(a, mid) && ortho(mid, c) && Math.abs(a[0] - c[0]) === 1 && Math.abs(a[1] - c[1]) === 1) out.splice(out.length - 2, 1);
      }
    }
    return out;
  }

  /* ---------- buffers ---------- */
  const blank = (w, h) => ({w, h, data: new Uint8ClampedArray(w * h * 4)});
  const copy = b => ({w: b.w, h: b.h, data: new Uint8ClampedArray(b.data)});
  const inside = (b, x, y) => x >= 0 && y >= 0 && x < b.w && y < b.h;
  function get(b, x, y) {
    if (!inside(b, x, y)) return null;
    const i = (y * b.w + x) * 4, d = b.data;
    return d[i + 3] ? '#' + [d[i], d[i + 1], d[i + 2]].map(v => v.toString(16).padStart(2, '0')).join('') : null;
  }
  function set(b, x, y, hex) {
    if (!inside(b, x, y)) return;
    const i = (y * b.w + x) * 4, d = b.data;
    if (!hex) { d[i] = d[i + 1] = d[i + 2] = d[i + 3] = 0; return; }
    d[i] = parseInt(hex.slice(1, 3), 16); d[i + 1] = parseInt(hex.slice(3, 5), 16); d[i + 2] = parseInt(hex.slice(5, 7), 16); d[i + 3] = 255;
  }
  const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
  /* A rect clipped to the buffer, or null when nothing of it is inside. */
  function clip(b, r) {
    const x0 = Math.max(0, r.x), y0 = Math.max(0, r.y), x1 = Math.min(b.w, r.x + r.w), y1 = Math.min(b.h, r.y + r.h);
    return x1 > x0 && y1 > y0 ? {x: x0, y: y0, w: x1 - x0, h: y1 - y0} : null;
  }
  const whole = b => ({x: 0, y: 0, w: b.w, h: b.h});
  const rectFrom = (ax, ay, bx, by) => { const {x0, y0, x1, y1} = box(ax, ay, bx, by); return {x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1}; };
  function extract(b, r) {
    const out = blank(r.w, r.h);
    for (let y = 0; y < r.h; y++) for (let x = 0; x < r.w; x++) set(out, x, y, get(b, r.x + x, r.y + y));
    return out;
  }
  function clear(b, r) { for (let y = r.y; y < r.y + r.h; y++) for (let x = r.x; x < r.x + r.w; x++) set(b, x, y, null); }
  /* Paste the region's opaque pixels at (x, y); transparent ones leave what is there. Clips at the edges. */
  function blit(b, region, x, y) {
    for (let sy = 0; sy < region.h; sy++) for (let sx = 0; sx < region.w; sx++) {
      const hex = get(region, sx, sy); if (hex) set(b, x + sx, y + sy, hex);
    }
  }
  function mapped(region, w, h, from) {
    const out = blank(w, h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const [sx, sy] = from(x, y); set(out, x, y, get(region, sx, sy)); }
    return out;
  }
  const flipH = r => mapped(r, r.w, r.h, (x, y) => [r.w - 1 - x, y]);
  const flipV = r => mapped(r, r.w, r.h, (x, y) => [x, r.h - 1 - y]);
  /* Quarter turn; clockwise unless cw is false. Width and height swap. */
  const rotate = (r, cw = true) => mapped(r, r.h, r.w, cw ? (x, y) => [y, r.h - 1 - x] : (x, y) => [r.w - 1 - y, x]);
  /* Move the pixels inside rect r by (dx, dy). With wrap they come round the other side; without, they fall off. */
  function shift(b, r, dx, dy, wrap = false) {
    const src = extract(b, r); clear(b, r);
    for (let y = 0; y < r.h; y++) for (let x = 0; x < r.w; x++) {
      let tx = x + dx, ty = y + dy;
      if (wrap) { tx = ((tx % r.w) + r.w) % r.w; ty = ((ty % r.h) + r.h) % r.h; } else if (tx < 0 || ty < 0 || tx >= r.w || ty >= r.h) continue;
      set(b, r.x + tx, r.y + ty, get(src, x, y));
    }
  }
  /* Recolour every `from` pixel inside r; returns how many changed. */
  function replace(b, r, from, to) {
    let n = 0;
    for (let y = r.y; y < r.y + r.h; y++) for (let x = r.x; x < r.x + r.w; x++) if (get(b, x, y) === from && from !== to) { set(b, x, y, to); n++; }
    return n;
  }
  /* 4-connected flood fill, kept inside r (the whole buffer by default). */
  function flood(b, x, y, hex, r = whole(b)) {
    const target = get(b, x, y), inR = (px, py) => px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
    if (target === hex || !inR(x, y)) return 0;
    const stack = [[x, y]], seen = new Uint8Array(b.w * b.h); let n = 0;
    while (stack.length) {
      const [cx, cy] = stack.pop();
      if (!inside(b, cx, cy) || !inR(cx, cy) || seen[cy * b.w + cx] || get(b, cx, cy) !== target) continue;
      seen[cy * b.w + cx] = 1; set(b, cx, cy, hex); n++;
      stack.push([cx + 1, cy], [cx - 1, cy], [cx, cy + 1], [cx, cy - 1]);
    }
    return n;
  }
  /* Colours in r (or the whole buffer), most used first. */
  function colours(b, r = whole(b)) {
    const n = new Map();
    for (let y = r.y; y < r.y + r.h; y++) for (let x = r.x; x < r.x + r.w; x++) { const hex = get(b, x, y); if (hex) n.set(hex, (n.get(hex) || 0) + 1); }
    return [...n].sort((p, q) => q[1] - p[1]);
  }
  /* The rect around every opaque pixel, or null for an empty buffer. Its last row is the ground contact. */
  function bounds(b) {
    let x0 = b.w, y0 = b.h, x1 = -1, y1 = -1;
    for (let y = 0; y < b.h; y++) for (let x = 0; x < b.w; x++) if (b.data[(y * b.w + x) * 4 + 3]) { x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); }
    return x1 < 0 ? null : {x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1};
  }
  const luma = hex => parseInt(hex.slice(1, 3), 16) * 299 + parseInt(hex.slice(3, 5), 16) * 587 + parseInt(hex.slice(5, 7), 16) * 114;
  /* The eye row of a face: the top of the eye, found from face ink in the middle half of the width.
   * Face ink is the darkest colour (the outline ink) with every neighbour opaque, so the outline never
   * counts. An eye is face ink touching a white catchlight; its row is the top of that 8-connected
   * cluster of face ink and white, wherever the catchlight sits. Without one, the topmost face ink counts. */
  function eyeRow(b) {
    const ink = colours(b).map(([h]) => h).reduce((d, h) => (d === null || luma(h) < luma(d) ? h : d), null);
    if (!ink) return null;
    const near = (x, y) => { const out = []; for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) if (dx || dy) out.push([x + dx, y + dy]); return out; };
    const face = (x, y) => get(b, x, y) === ink && near(x, y).every(([nx, ny]) => get(b, nx, ny));
    let first = null;
    for (let y = 0; y < b.h; y++) for (let x = b.w >> 2; x < b.w - (b.w >> 2); x++) {
      if (!face(x, y)) continue;
      if (first === null) first = y;
      if (!near(x, y).some(([nx, ny]) => get(b, nx, ny) === '#ffffff')) continue;
      let top = y; const seen = new Set([x + ',' + y]), stack = [[x, y]];
      while (stack.length) {
        const [cx, cy] = stack.pop(); top = Math.min(top, cy);
        for (const [nx, ny] of near(cx, cy)) {
          const k = nx + ',' + ny;
          if (!seen.has(k) && (get(b, nx, ny) === '#ffffff' || face(nx, ny))) { seen.add(k); stack.push([nx, ny]); }
        }
      }
      return top;
    }
    return first;
  }
  /* Colours of a buffer that the palette lacks. */
  const offPalette = (b, palette) => { const pal = new Set(palette.map(h => h.toLowerCase())); return colours(b).map(([h]) => h).filter(h => !pal.has(h)); };

  /* What picking `hex` does. The Pick colour tool hands over to the pencil (or the eraser on a transparent
   * pixel); Alt-click with any other tool only takes the colour and keeps that tool. Returns {tool, color},
   * with color undefined when it stays the same, or {off: true} for a colour the palette lacks. */
  function pickOutcome(hex, palette, tool) {
    const held = tool !== 'picker';
    if (!hex) return {tool: held ? tool : 'eraser'};
    if (!palette.includes(hex)) return {off: true};
    return {tool: held ? tool : 'pencil', color: hex};
  }

  /* ---------- history: a bounded list of states with a cursor ---------- */
  /* Each entry is {label, data}; entries after `at` are redo steps until the next record. */
  function history(data, label = 'Opened', limit = 100) { return {limit, at: 0, entries: [{label, data: data.slice()}]}; }
  /* Record the buffer after an action; returns false (and records nothing) when it did not change. */
  function record(h, label, data) {
    if (same(h.entries[h.at].data, data)) return false;
    h.entries.splice(h.at + 1); h.entries.push({label, data: data.slice()});
    while (h.entries.length > h.limit) h.entries.shift();
    h.at = h.entries.length - 1;
    return true;
  }
  /* Jump to entry i; returns its data (a copy) or null when out of range. */
  function jump(h, i) { if (i < 0 || i >= h.entries.length) return null; h.at = i; return h.entries[i].data.slice(); }
  const current = h => h.entries[h.at].data;

  const api = {line, rect, ellipse, pixelPerfect, blank, copy, get, set, clip, whole, rectFrom, extract, clear, blit,
    flipH, flipV, rotate, shift, replace, flood, colours, offPalette, pickOutcome, bounds, eyeRow, history, record, jump, current};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.JelliPaint = api;
})(typeof window !== 'undefined' ? window : globalThis);

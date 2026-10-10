// Checks paint_tools.js, the pure logic behind Jelli Art's Paint tools.
//   node tools/jelli-art/test_paint_tools.js
// Exits non-zero on the first failing group; prints one line per group.
'use strict';
const assert = require('assert');
const P = require(require('path').join(__dirname, 'paint_tools.js'));

const key = pts => pts.map(([x, y]) => `${x},${y}`).sort().join(' ');
const unique = pts => new Set(pts.map(p => p.join(','))).size === pts.length;
const groups = [];
const test = (name, fn) => groups.push([name, fn]);
/* A buffer from rows of characters: '.' transparent, a letter a palette index into COLS. */
const COLS = {a: '#291b35', b: '#85e4b6', c: '#fa8c99', d: '#ffffff'};
function buf(rows) {
  const b = P.blank(rows[0].length, rows.length);
  rows.forEach((row, y) => [...row].forEach((ch, x) => P.set(b, x, y, COLS[ch] || null)));
  return b;
}
const rows = b => Array.from({length: b.h}, (_, y) => Array.from({length: b.w}, (_, x) => {
  const hex = P.get(b, x, y); return hex ? Object.keys(COLS).find(k => COLS[k] === hex) : '.';
}).join(''));

test('line is 8-connected Bresenham with one pixel per major step', () => {
  for (const [x0, y0, x1, y1] of [[0, 0, 7, 3], [5, 5, 0, 0], [2, 9, 2, 1], [0, 0, 6, 0], [3, 1, -4, 6], [4, 4, 4, 4], [0, 0, 5, 5]]) {
    const pts = P.line(x0, y0, x1, y1);
    assert.deepStrictEqual(pts[0], [x0, y0]); assert.deepStrictEqual(pts[pts.length - 1], [x1, y1]);
    assert.strictEqual(pts.length, Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0)) + 1);
    for (let i = 1; i < pts.length; i++) assert.strictEqual(Math.max(Math.abs(pts[i][0] - pts[i - 1][0]), Math.abs(pts[i][1] - pts[i - 1][1])), 1);
    assert.ok(unique(pts));
  }
  assert.strictEqual(key(P.line(0, 0, 4, 2)), key(P.line(4, 2, 0, 0)), 'a line is the same pixels both ways');
});

test('rectangles outline and fill the box from any corner', () => {
  assert.strictEqual(P.rect(0, 0, 3, 2).length, 10);
  assert.strictEqual(P.rect(3, 2, 0, 0, true).length, 12);
  assert.strictEqual(key(P.rect(3, 2, 0, 0)), key(P.rect(0, 0, 3, 2)));
  assert.deepStrictEqual(P.rect(2, 2, 2, 2), [[2, 2]]);
  assert.ok(unique(P.rect(1, 1, 1, 5)));
});

test('ellipses stay inside their box, touch every side and are symmetric', () => {
  for (let w = 1; w <= 14; w++) for (let h = 1; h <= 14; h++) {
    for (const filled of [false, true]) {
      const pts = P.ellipse(3, 2, 3 + w - 1, 2 + h - 1, filled), s = new Set(pts.map(p => p.join(',')));
      assert.ok(unique(pts), `${w}x${h} duplicates`);
      assert.ok(pts.every(([x, y]) => x >= 3 && y >= 2 && x < 3 + w && y < 2 + h), `${w}x${h} leaves its box`);
      for (const [x, y] of pts) {
        assert.ok(s.has(`${3 + w - 1 - (x - 3)},${y}`), `${w}x${h} not mirrored left/right at ${x},${y}`);
        assert.ok(s.has(`${x},${2 + h - 1 - (y - 2)}`), `${w}x${h} not mirrored top/bottom at ${x},${y}`);
      }
      const xs = pts.map(p => p[0]), ys = pts.map(p => p[1]);
      assert.deepStrictEqual([Math.min(...xs), Math.max(...xs), Math.min(...ys), Math.max(...ys)], [3, 3 + w - 1, 2, 2 + h - 1], `${w}x${h} misses a side`);
    }
    const outline = P.ellipse(0, 0, w - 1, h - 1), fill = new Set(P.ellipse(0, 0, w - 1, h - 1, true).map(p => p.join(',')));
    assert.ok(outline.every(p => fill.has(p.join(','))), `${w}x${h} filled ellipse lacks its outline`);
  }
  assert.strictEqual(key(P.ellipse(6, 5, 0, 0)), key(P.ellipse(0, 0, 6, 5)));
  assert.deepStrictEqual(P.ellipse(4, 4, 4, 4), [[4, 4]]);
});

test('pixel-perfect strokes drop L corners only', () => {
  assert.deepStrictEqual(P.pixelPerfect([[0, 0], [1, 0], [1, 1], [2, 1]]), [[0, 0], [1, 1], [2, 1]]);
  assert.deepStrictEqual(P.pixelPerfect([[0, 0], [1, 0], [2, 0]]), [[0, 0], [1, 0], [2, 0]]);
  assert.deepStrictEqual(P.pixelPerfect([[0, 0], [0, 0], [1, 1]]), [[0, 0], [1, 1]]);
});

test('flips and quarter turns', () => {
  const b = buf(['ab.', 'c..']);
  assert.deepStrictEqual(rows(P.flipH(b)), ['.ba', '..c']);
  assert.deepStrictEqual(rows(P.flipV(b)), ['c..', 'ab.']);
  assert.deepStrictEqual(rows(P.rotate(b)), ['ca', '.b', '..']);
  assert.deepStrictEqual(rows(P.rotate(b, false)), ['..', 'b.', 'ac']);
  let r = b; for (let i = 0; i < 4; i++) r = P.rotate(r);
  assert.deepStrictEqual(rows(r), rows(b));
  assert.deepStrictEqual(rows(P.rotate(P.rotate(b), false)), rows(b));
  assert.deepStrictEqual(rows(P.flipH(P.flipH(b))), rows(b));
});

test('extract, clear and blit move a selection without painting transparency', () => {
  const b = buf(['ab..', 'cd..', '..bb']);
  const r = P.extract(b, {x: 0, y: 0, w: 2, h: 2});
  assert.deepStrictEqual(rows(r), ['ab', 'cd']);
  P.clear(b, {x: 0, y: 0, w: 2, h: 2});
  P.blit(b, buf(['a.', '.a']), 2, 1);
  assert.deepStrictEqual(rows(b), ['....', '..a.', '..ba']);
  P.blit(b, buf(['cc']), 3, 0);  // clipped at the right edge
  assert.deepStrictEqual(rows(b)[0], '...c');
  assert.deepStrictEqual(P.clip(b, {x: -2, y: 1, w: 4, h: 9}), {x: 0, y: 1, w: 2, h: 2});
  assert.strictEqual(P.clip(b, {x: 9, y: 0, w: 2, h: 2}), null);
  assert.deepStrictEqual(P.rectFrom(3, 2, 1, 0), {x: 1, y: 0, w: 3, h: 3});
});

test('shift wraps or drops pixels within the rect only', () => {
  const b = buf(['ab.', '...', 'c.d']);
  const w = P.copy(b); P.shift(w, P.whole(w), 1, 0, true);
  assert.deepStrictEqual(rows(w), ['.ab', '...', 'dc.']);
  const n = P.copy(b); P.shift(n, P.whole(n), 0, -1);
  assert.deepStrictEqual(rows(n), ['...', 'c.d', '...']);
  const s = P.copy(b); P.shift(s, {x: 0, y: 0, w: 2, h: 1}, -1, 0, true);
  assert.deepStrictEqual(rows(s), ['ba.', '...', 'c.d']);
});

test('replace and flood stay inside their rect', () => {
  const b = buf(['aab', 'aab', 'bbb']);
  assert.strictEqual(P.replace(b, {x: 0, y: 0, w: 1, h: 2}, COLS.a, COLS.c), 2);
  assert.deepStrictEqual(rows(b), ['cab', 'cab', 'bbb']);
  assert.strictEqual(P.flood(b, 2, 2, COLS.d, {x: 1, y: 1, w: 2, h: 2}), 3);
  assert.deepStrictEqual(rows(b), ['cab', 'cad', 'bdd']);
  assert.strictEqual(P.flood(b, 0, 0, COLS.c), 0, 'filling a colour with itself changes nothing');
  assert.strictEqual(P.flood(b, 0, 0, null), 2);
});

test('colours and off-palette checks', () => {
  const b = buf(['aab', 'c..']);
  assert.deepStrictEqual(P.colours(b), [[COLS.a, 2], [COLS.b, 1], [COLS.c, 1]]);
  assert.deepStrictEqual(P.offPalette(b, [COLS.a.toUpperCase(), COLS.b]), [COLS.c]);
});

test('history is bounded, skips no-ops and truncates redo on a new action', () => {
  const data = new Uint8ClampedArray(4), h = P.history(data, 'Opened', 5);
  assert.strictEqual(P.record(h, 'nothing', data), false);
  for (let i = 1; i <= 7; i++) { data[0] = i; assert.ok(P.record(h, `step ${i}`, data)); }
  assert.strictEqual(h.entries.length, 5); assert.strictEqual(h.entries[0].label, 'step 3'); assert.strictEqual(h.at, 4);
  assert.strictEqual(P.jump(h, 1)[0], 4); assert.strictEqual(P.current(h)[0], 4);
  assert.strictEqual(P.jump(h, 9), null);
  data[0] = 42; P.record(h, 'branch', data);
  assert.deepStrictEqual(h.entries.map(e => e.label), ['step 3', 'step 4', 'branch']);
  data[0] = 0; assert.strictEqual(P.current(h)[0], 42, 'history keeps copies, not the live buffer');
});

test('Alt-pick keeps the tool; the Pick colour tool hands over to pencil or eraser', () => {
  const pal = ['#291b35', '#85e4b6'];
  assert.deepStrictEqual(P.pickOutcome('#85e4b6', pal, 'fill'), {tool: 'fill', color: '#85e4b6'});
  assert.deepStrictEqual(P.pickOutcome('#85e4b6', pal, 'eraser'), {tool: 'eraser', color: '#85e4b6'});
  assert.deepStrictEqual(P.pickOutcome(null, pal, 'line'), {tool: 'line'});
  assert.deepStrictEqual(P.pickOutcome('#85e4b6', pal, 'picker'), {tool: 'pencil', color: '#85e4b6'});
  assert.deepStrictEqual(P.pickOutcome(null, pal, 'picker'), {tool: 'eraser'});
  assert.deepStrictEqual(P.pickOutcome('#123456', pal, 'pencil'), {off: true});
});

let failed = 0;
for (const [name, fn] of groups) {
  try { fn(); console.log(`ok   ${name}`); } catch (err) { failed++; console.log(`FAIL ${name}\n     ${err.message}`); }
}
process.exit(failed ? 1 : 0);

/* Jelli Art: style-guide lint verdicts in the page (window.JelliLint), the same rules as
 * tools/assets/lint_rules.py; test_server.py checks both agree on every asset. Limits and waivers
 * come from assets/slice/source/lint.json (payload "lint"). measure() mirrors compare_slice.py measure(),
 * including the own-palette outline ink, so the page and the server see the same metrics. A metrics object is
 * {colors: {hex: count}, specks: [[x, y]], open_edges: [[x, y]]}. */
'use strict';
(root => {
  const RULES = ['specks', 'open_edges', 'colours'];
  /* Kinds whose silhouettes need a closed outline, and kinds with no speck check (compare_slice.py OUTLINED). */
  const OUTLINED = new Set(['creatures', 'icons', 'menus', 'meters', 'health', 'effects', 'prizes', 'props']);
  const UNMEASURED = new Set(['font', 'backgrounds']);
  const SHARED_INK = '#291b35', WHITE = '#ffffff';
  const luma = hex => [1, 3, 5].reduce((n, i, k) => n + parseInt(hex.slice(i, i + 2), 16) * [299, 587, 114][k], 0);
  /* The outline colour: shared ink, or the darkest colour of a sprite's own palette (null for the shared one). */
  const outlineInk = palette => palette ? palette.map(h => h.toLowerCase()).reduce((d, h) => luma(h) < luma(d) ? h : d) : SHARED_INK;
  /* Specks (no same-colour 8-neighbour; ink and white exempt), open edges (non-ink pixel with a transparent
   * 4-neighbour) and colour use, for an RGBA buffer {w, h, data}: compare_slice.py measure(). */
  function measure(p, kind, ink = SHARED_INK) {
    const at = (x, y) => {
      if (x < 0 || y < 0 || x >= p.w || y >= p.h) return null;
      const i = (y * p.w + x) * 4, d = p.data;
      return d[i + 3] ? '#' + [d[i], d[i + 1], d[i + 2]].map(v => v.toString(16).padStart(2, '0')).join('') : null;
    };
    const colors = {}, specks = [], open = [], measured = !UNMEASURED.has(kind), outlined = OUTLINED.has(kind);
    for (let y = 0; y < p.h; y++) for (let x = 0; x < p.w; x++) {
      const hex = at(x, y); if (!hex) continue;
      colors[hex] = (colors[hex] || 0) + 1;
      const n4 = [at(x + 1, y), at(x - 1, y), at(x, y + 1), at(x, y - 1)];
      const n8 = [...n4, at(x + 1, y + 1), at(x - 1, y - 1), at(x + 1, y - 1), at(x - 1, y + 1)];
      if (measured && hex !== ink && hex !== WHITE && !n8.includes(hex)) specks.push([x, y]);
      if (outlined && hex !== ink && n4.includes(null)) open.push([x, y]);
    }
    return {colors, opaque: Object.values(colors).reduce((n, v) => n + v, 0), specks, open_edges: open};
  }
  const NAMES = {specks: 'specks', open_edges: 'open edges', colours: 'colours'};
  function colourLimit(config, kind) {
    const kinds = config?.max_colours_by_kind || {};
    return kind in kinds ? kinds[kind] : config?.max_colours ?? null;
  }
  /* {fails: [rule], waived: {rule: reason}, colours, max_colours}. */
  function verdict(key, kind, metrics, config) {
    const limit = colourLimit(config, kind), colours = Object.keys(metrics.colors).length;
    const bad = {specks: metrics.specks.length > 0, open_edges: metrics.open_edges.length > 0, colours: limit !== null && colours > limit};
    const failing = RULES.filter(r => bad[r]), waiver = config?.waivers?.[key], waived = {};
    for (const r of failing) if (waiver?.rules.includes(r)) waived[r] = waiver.reason;
    return {fails: failing.filter(r => !(r in waived)), waived, colours, max_colours: limit};
  }
  const api = {RULES, NAMES, OUTLINED, colourLimit, verdict, measure, outlineInk};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.JelliLint = api;
})(typeof window !== 'undefined' ? window : globalThis);

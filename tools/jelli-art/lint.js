/* Jelli Art: style-guide lint verdicts in the page (window.JelliLint), the same rules as
 * tools/assets/lint_rules.py; test_server.py checks both agree on every asset. Limits and waivers
 * come from assets/slice/source/lint.json (payload "lint"). A metrics object is
 * {colors: {hex: count}, specks: [[x, y]], open_edges: [[x, y]]}. */
'use strict';
(root => {
  const RULES = ['specks', 'open_edges', 'colours'];
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
  const api = {RULES, NAMES, colourLimit, verdict};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.JelliLint = api;
})(typeof window !== 'undefined' ? window : globalThis);

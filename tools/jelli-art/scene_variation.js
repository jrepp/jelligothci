/* Seeded initial variation. Keep integer sampling in sync with scene_data.py. */
'use strict';
function resolveSceneLayer(scene, layer) {
  const result = {...layer}, variation = layer.random;
  if (!variation) return result;
  let state = (((scene.seed || 0) << 16) | variation.seed) >>> 0;
  const sample = () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state >>> 8;
  };
  for (const [field, limit] of [['x',256], ['y',256], ['travel',100]]) {
    const spread = variation[field];
    const value = layer[field] + sample() % (2 * spread + 1) - spread;
    result[field] = Math.max(field === 'travel' ? 0 : -Number(limit), Math.min(Number(limit), value));
  }
  result.visible = layer.visible && sample() % 100 < variation.chance;
  return result;
}

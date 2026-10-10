/* Jelli Art: the stimulus simulator, a line-for-line mirror of core/behavior.c
 * (roll, reaction_fits, react, deliver, enter_state's duration draw) and the look
 * placement of core/pet_actor.c and core/pet_behavior_draw.c. Pure functions with
 * no DOM, so tools/jelli-art/test_creatures.py can check them under node. Night is
 * an input here; the game derives it from the pet clock and content night bounds. */
'use strict';
(root => {
  const ANY = 255, TICKS_PER_SECOND = 10, CHANCE_SALT = 0x5bd1e995;
  const EFFECT_TARGET_PX = 40, PROP_SCALE = 3, MESS_SCALE = 2, FLOOR_Y = 256, PANEL = 466;

  /* core/behavior.c roll(): stateless draw from pet ID, tick count and salt. ticks is a BigInt. */
  function roll(id, ticks, salt) {
    const t = BigInt.asUintN(64, BigInt(ticks));
    const lo = Number(t & 0xffffffffn) >>> 0, hi = Number(t >> 32n) >>> 0;
    let x = (Math.imul(id >>> 0, 0x9e3779b1) ^ lo ^ Math.imul(hi, 0x85ebca6b) ^ Math.imul(salt >>> 0, 0xc2b2ae35)) >>> 0;
    x = (x ^ (x >>> 16)) >>> 0;
    x = Math.imul(x, 0x7feb352d) >>> 0;
    x = (x ^ (x >>> 15)) >>> 0;
    x = Math.imul(x, 0x846ca68b) >>> 0;
    return (x ^ (x >>> 16)) >>> 0;
  }

  /* A reaction's value filter, encoded as cmake/JelliBehaviors.cmake jelli_behavior_value() does. */
  function reactionValue(r, vocab) {
    const n = vocab.numbers, code = n.activity_code;
    if (r.on === 'need_low' && r.need) return n.needs[r.need];
    if (r.on === 'location' && r.location) return vocab.lists.locations.indexOf(r.location);
    if (/^activity_/.test(r.on)) {
      if (r.moment) return code.moment + vocab.moments.indexOf(r.moment);
      if (r.health) return code.health + n.health[r.health];
      if (r.activity) return code.care + n.care[r.activity];
    }
    if (/^present_/.test(r.on) && r.prize !== undefined && r.prize !== null) return r.prize;
    return ANY;
  }

  /* The table row the generator emits for one reaction. */
  function encode(r, doc, vocab) {
    const when = r.when || {};
    return {on: vocab.numbers.stimuli[r.on], value: reactionValue(r, vocab),
      state: doc.states.findIndex(s => s.name === r.state), weight: r.weight ?? 1, chance_pct: r.chance_pct ?? 100,
      location: when.location ? vocab.lists.locations.indexOf(when.location) : ANY,
      night: when.night === undefined ? 'always' : when.night ? 'yes' : 'no',
      mood_min: when.mood_min ?? 0, mood_max: when.mood_max ?? 100, bond_min: when.bond_min ?? 0};
  }

  /* reaction_fits(): returns null when it fits, else the first reason it does not. */
  function misfit(e, pet, kind, value, stateCount) {
    if (e.on !== kind) return 'different stimulus';
    if (e.value !== ANY && e.value !== value) return 'different value';
    if (e.state < 0 || e.state >= stateCount) return 'unknown state';
    if (pet.cooldown >= 0 && pet.cooldown === e.state) return 'state cooling down';
    if (e.location !== ANY && e.location !== pet.location) return 'other location';
    if ((e.night === 'yes' && !pet.night) || (e.night === 'no' && pet.night)) return e.night === 'yes' ? 'needs night' : 'needs day';
    if (pet.mood < e.mood_min || pet.mood > e.mood_max) return `mood outside ${e.mood_min}–${e.mood_max}`;
    if (pet.bond < e.bond_min) return `bond below ${e.bond_min}`;
    return null;
  }

  /* enter_state(): the duration in seconds from the third draw. */
  function duration(state, draw) {
    const low = state.duration_s[0] * TICKS_PER_SECOND, high = state.duration_s[1] * TICKS_PER_SECOND;
    const span = high - low + 1, seconds = Math.floor((low + (draw >>> 0) % span + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND);
    return seconds ? Math.min(seconds, 65535) : 1;
  }

  /* react(): weighted pick among fitting reactions, then that reaction's chance roll. */
  function react(doc, vocab, reactions, pet, kind, value) {
    const rows = reactions.map(r => ({reaction: r, encoded: encode(r, doc, vocab)}));
    const out = {rows, total: 0, pick: null, chosen: -1, chanceRoll: null, entered: null, gate: null};
    if (!reactions.length) { out.gate = 'This form has no repertoire, so it never enters behaviour states.'; return out; }
    if (pet.asleep) { out.gate = 'Asleep: reactions only run while awake.'; return out; }
    if (pet.busy) { out.gate = 'Busy with an activity: reactions only run while idle.'; return out; }
    for (const row of rows) {
      row.reason = misfit(row.encoded, pet, kind, value, doc.states.length);
      if (!row.reason) { row.from = out.total; out.total += row.encoded.weight; row.to = out.total; }
    }
    if (!out.total) return out;
    const salt = ((kind << 8) | value) >>> 0;
    out.salt = salt;
    out.pick = roll(pet.id, pet.ticks, salt) % out.total;
    let pick = out.pick;
    for (let i = 0; i < rows.length; i++) {
      const row = rows[i];
      if (row.reason) continue;
      if (pick >= row.encoded.weight) { pick -= row.encoded.weight; continue; }
      out.chosen = i;
      out.chanceRoll = roll(pet.id, pet.ticks, (salt ^ CHANCE_SALT) >>> 0) % 100;
      if (out.chanceRoll < row.encoded.chance_pct) {
        const state = doc.states[row.encoded.state];
        out.entered = {index: row.encoded.state, name: state.name, seconds: duration(state, roll(pet.id, pet.ticks, (salt + 1) >>> 0))};
      }
      return out;
    }
    return out;
  }

  /* deliver(): a stimulus in the running state's ends_on ends it (starting its cooldown);
   * a running state is otherwise kept, never replaced; with no state, the repertoire reacts. */
  function deliver(doc, vocab, reactions, pet, stimulus, value) {
    const kind = vocab.numbers.stimuli[stimulus], v = Math.min(value >>> 0, 255);
    const running = pet.behavior >= 0 ? doc.states[pet.behavior] : null;
    const out = {kind, value: v, ended: null, kept: null, result: null};
    let next = pet;
    if (running && (running.ends_on || []).includes(stimulus)) {
      out.ended = running.name;
      next = {...pet, behavior: -1, cooldown: running.cooldown_s > 0 ? pet.behavior : -1};
    } else if (running) {
      out.kept = running.name;
      return out;
    }
    out.result = react(doc, vocab, reactions, next, kind, v);
    return out;
  }

  /* The value a stimulus of this kind carries in the game, from the simulator's pickers. */
  function stimulusValue(stimulus, pick, vocab) {
    const n = vocab.numbers, code = n.activity_code;
    if (stimulus === 'need_low') return n.needs[pick.need] ?? 0;
    if (stimulus === 'location') return Math.max(0, vocab.lists.locations.indexOf(pick.location));
    if (/^activity_/.test(stimulus)) {
      if (pick.activityKind === 'moment') return code.moment + Math.max(0, vocab.moments.indexOf(pick.moment));
      if (pick.activityKind === 'health') return code.health + (n.health[pick.health] ?? 0);
      return code.care + (n.care[pick.care] ?? 0);
    }
    if (stimulus === 'present_given' && pick.plainGift) return ANY;
    if (/^present_/.test(stimulus)) return pick.prize | 0;
    if (stimulus === 'touched' || stimulus === 'woke') return pick.level | 0;
    return 0;
  }

  /* jelli_pet_actor_layout(): top-left and on-panel bounds of a frame at scale. */
  function actorLayout(anchorQ8, bounds, scale) {
    const x = 233 - Math.floor((anchorQ8[0] * scale + 128) / 256), y = FLOOR_Y - Math.floor((anchorQ8[1] * scale + 128) / 256);
    const [l, t, r, b] = bounds;
    return {x, y, bounds: {x: x + l * scale, y: y + t * scale, width: (r - l) * scale, height: (b - t) * scale}};
  }
  /* jelli_pet_draw_behavior() and draw_mess(): centres of the prop, effect and mess sprites. */
  function lookPlacement(box, effectWidth, clipFrame) {
    const cx = box.x + Math.floor(box.width / 2);
    return {prop: {x: cx, y: box.y + Math.floor(box.height * 2 / 3), scale: PROP_SCALE},
      effect: {x: cx + Math.trunc(box.width / 3), y: box.y - 22 - ((clipFrame & 1) ? 4 : 0),
        scale: Math.max(1, effectWidth ? Math.floor(EFFECT_TARGET_PX / effectWidth) : 1)}};
  }
  function messPlacement(box, messHeight) {
    let x = box.x + box.width + 40;
    if (x > PANEL - 90) x = box.x - 40;
    return {x, y: FLOOR_Y - Math.trunc(messHeight * MESS_SCALE / 2), scale: MESS_SCALE};
  }
  /* pet_render.c: the mess frame shown at time ms (1-based in the game; 0-based here). */
  const messFrame = (timeMs, frameMs, count) => frameMs && count ? Math.floor(timeMs / frameMs) % count : 0;

  const api = {ANY, roll, reactionValue, encode, misfit, duration, react, deliver, stimulusValue,
    actorLayout, lookPlacement, messPlacement, messFrame};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.JelliSim = api;
})(typeof window !== 'undefined' ? window : globalThis);

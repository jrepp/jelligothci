// Replays behavior_engine_cases.json (outcomes recorded from core/behavior.c) through
// simulator.js and checks roll() against reference values. Run by test_creatures.py:
//   node replay_engine_cases.js <simulator.js> <behavior_engine_cases.json>
'use strict';
const fs = require('fs');
const [simPath, fixturePath] = process.argv.slice(2);
const Sim = require(require('path').resolve(simPath));
const {doc, vocab, cases} = JSON.parse(fs.readFileSync(fixturePath, 'utf8'));

// roll(id, ticks, salt) from core/behavior.c, compiled and run on the host.
const ROLLS = [[1, '0', 0, 1834104592], [1, '12345', 2052, 2631490642], [7, '2592017', 1540481425, 1891082126],
  [7, '20015998343868', 511, 2251205299], [4000000000, '0', 2052, 2216911832], [4000000000, '20015998343868', 511, 138252643]];
const failures = [];
for (const [id, ticks, salt, want] of ROLLS) {
  const got = Sim.roll(id, BigInt(ticks), salt);
  if (got !== want) failures.push(`roll(${id}, ${ticks}, ${salt}) = ${got}, want ${want}`);
}

const reactions = doc.repertoires.find(r => r.forms.includes('AXOLOTL')).reactions;
for (const c of cases) {
  let pet = {id: c.id, ticks: BigInt(c.ticks), location: c.location, night: !!c.night, mood: c.mood, bond: c.bond,
    asleep: !!c.asleep, busy: !!c.busy, behavior: c.behavior, cooldown: c.cooldown};
  // jelli_behavior_step queues idle after the stimulus when no state is running.
  const queue = [[c.stimulus, c.value]];
  if (pet.behavior < 0 && !pet.asleep && !pet.busy) queue.push(['idle', 0]);
  let left = pet.behavior >= 0 ? 999 : 0;
  for (const [stimulus, value] of queue) {
    const o = Sim.deliver(doc, vocab, reactions, pet, stimulus, value);
    if (o.ended) { pet = {...pet, behavior: -1, cooldown: doc.states[pet.behavior].cooldown_s > 0 ? pet.behavior : -1}; left = 0; }
    if (o.result?.entered) { pet = {...pet, behavior: o.result.entered.index}; left = o.result.entered.seconds; }
  }
  if (pet.behavior !== c.after || left !== c.left)
    failures.push(`${JSON.stringify(c)} -> state ${pet.behavior}, ${left} s`);
}
if (failures.length) {
  console.error(failures.slice(0, 10).join('\n'));
  process.exit(1);
}
console.log(`${cases.length} engine cases and ${ROLLS.length} rolls match`);

---
title: Drive creature species, animation clips, and behaviour profiles from content data
status: Proposed
created: 2026-10-10T06:03:25Z
deciders: Jacob Repp
tags: [architecture, assets, content, creatures, rendering]
id: adr-012
project_id: jelligotchi
doc_uuid: 6b55f07f-7b11-4c04-b5c6-94c6b54a0243
---

# Context

The slice had one creature family, Mint growing into Lilac. Its frames were
fixed 32x32 sprites in the shared 16-colour palette. `core/pet_actor.c` chose
one hard-coded asset ID per state (base `1000`/`1006` plus offsets),
`core/pet_timing.c` held the idle schedule, and the 6x actor scale was repeated
in five files. Every pet entry in `content/pets.json` used the same evolution set.

The user supplied a hand-drawn axolotl: 14 frames, 39x27 art pixels (sleep 42x20),
9 colours not in the shared palette, and multi-frame loops (idle bob, blink, chew,
sleep breath, sad) that do not map one-to-one onto the jelly poses. The user chose:

- a new species alongside Mint and Lilac, assigned to the BUBBLE collection entry;
- larger frames that keep the art exact, rather than shrinking it to 32x32;
- the art's own palette, rather than remapping it to the shared one;
- the proposed pose mapping, which became the clip data;
- "data driven techniques as much as possible", including for rendering and
  creature behaviour in general, with authoring and preview in Jelli Art.

RFC-004 already proposes per-entry evolution sets and per-form profile references.

# Decision

Separate *what a state means* (code) from *how a creature presents it* (data):

1. **Species are evolution sets.** In `content/pets.json`, forms carry an `art`
   name, and `evolution_sets` lists up to 4 sets of 1–2 forms. The second form
   grows after the set's `growth_ticks`; single-form sets declare 0. Each form
   belongs to at most one set. Form IDs stay stable bit positions in the saved
   reached-forms mask. `cmake/JelliCollection.cmake` validates the catalog and
   generates bounded tables. The core asks a pet's set for growth, validity, unlock
   and form switching; `JELLI_CMD_FORM` takes a position in that set.
2. **Saves follow the catalog.** On load, `jelli_collection_normalize` maps each
   pet onto its entry's set before validation. It remaps only known form IDs, so a
   corrupt form or mask is still rejected. Existing BUBBLE pets become axolotls
   and keep their care state. The save format is unchanged.
3. **Clips are runtime data.** `assets/slice/assets.json` lists eight runtime
   poses (`creature_poses`). Every creature form needs a `<art>.<pose>` clip of
   1–6 frames with millisecond holds; the clip either loops or holds its last
   frame. `tools/assets/creature_data.py` emits a clip table indexed by catalog
   form. The renderer restarts a clip when the pose, form or pet changes and picks
   the frame from injected time. Frame changes are part of the render key, so an
   unchanged frame produces no damage. UI animation scale does not apply.
4. **Behaviour and size are profiles.** `content/creatures.json` gives each form
   art a profile with actor, icon and portrait scales, and a named behaviour. A
   behaviour has ordered first-match `{when, pose}` rules over a fixed C condition
   list, an idle-beat schedule of up to 16 beats, and a quiet cycle. The generator
   rejects unknown conditions or poses, duplicate rules, actors that would overlap
   the heading (more than 300x176 on-panel pixels), and icons wider than a grid
   cell. The `jelly` behaviour reproduces the former tables exactly; a test
   compares them beat for beat.

   *Amended 2026-10-10 (PR #37):* a rule may also name a state pose from
   `assets.json` `state_poses` (RFC-005), such as the axolotl's `touch_happy` →
   `hug`. A form without a clip for that state pose plays its fallback base
   pose; fallbacks are one level deep, because every fallback is a base pose.
   `tools/assets/creature_data.py` validates rule poses against the base and
   state pose list and resolves the fallback in the clip table. Jelli Art's
   Behaviour & size editor offers and simulates the same list, and
   `jelli_creature_clip` returns no clip for a pose index outside the table.
5. **Art size and palette are per asset.** Creature frames may be 32x32 or 48x48.
   An asset may name a palette in `manifest.palettes` (at most 16 colours), and
   binary alpha is unchanged. `tools/assets/import_creature.py` converts upscaled
   source art from a committed spec. IDs are `first_id` plus list position, so new
   frames are appended. The planned pack ceiling rises from 164,864 to
   237,568 bytes, and the SDL live-reload bank grows to 131,072 pixels and
   16,384 mask bytes. *Amended 2026-10-10 (owner-approved):* the desktop
   live-reload staging buffer is 294,912 bytes (288 KiB) and is the pack
   ceiling the build checks; the SDL decoder bounds pixels by the bank's real
   capacity instead of a stale 98,304-pixel limit. The subsequent main/device-power
   integration uses 320 KiB staging, 147,456 pixels and 18,432 mask bytes to retain
   both location art and baby/hug frames; see the measured budget in
   [memo-055](../memos/memo-055-main-rebase-and-device-firmware-deployment.md).
6. **Jelli Art authors the data.** Its Creature view edits clips with an animated
   preview at game placement. Profile and behaviour editing follows the same
   validate-then-commit path.

# Consequences

## Positive

- A new species needs only art, a manifest import, catalog entries, clips and a
  profile, with no new C constants. The axolotl itself added no species-specific
  code.
- Designers can retime or re-order animation and behaviour in Jelli Art, and the
  build validates every reference.
- Mint and Lilac keep the same assets, poses, schedule and scale.

## Negative

- Pose conditions remain a fixed C list. A truly new condition still needs code,
  plus a matching entry in the generator.
- A multi-frame clip redraws the full panel on each frame change, as idle phase
  changes already did. On ESP32 this costs one full frame every 250–900 ms while
  such a clip plays. It is not measured on hardware yet.
- The art pack is larger: with PR #37's baby axolotl, hug and surprised frames,
  the raw payload (RGB565, masks, font) is 267,304 bytes. It uses 125,248 of
  131,072 live pixels and 15,656 of 16,384 mask bytes, leaving room for two more
  48x48 frames. The firmware app is 1,778,608 bytes, leaving 58% of its 4 MiB
  partition free (measured 2026-10-10 at 41350aa); static D/IRAM is unchanged.
- `make run-live` reloads frame pixels but not clip or profile edits; those need
  a rebuild.

## Neutral

- Reassigning a collection entry to a different set is a catalog edit. Saves
  follow it on the next load.

# Alternatives Considered

## Shrink the axolotl to 32x32 in the shared palette

This would have needed no pipeline change, but it would destroy hand-placed
detail. The user rejected it.

## Hard-code an axolotl branch in `pet_actor.c`

This is the smallest code diff, but every later species would add another
branch. It contradicts the user's direction.

## Express pose rules as a general expression language

This is more flexible, but it is unbounded to validate and to evaluate on the
device. A fixed condition list ordered by data covers today's behaviour.

# References

- [RFC-004: Pet collection and evolution data](../rfcs/rfc-004-pet-collection-and-evolution-data.md)
- [RFC-001: Virtual pet systems architecture](../rfcs/rfc-001-virtual-pet-systems-architecture.md)
- [ADR-011: Jelli Art component and container](./adr-011-jelli-art-component-and-container.md)

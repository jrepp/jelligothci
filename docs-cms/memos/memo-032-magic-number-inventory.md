---
title: Magic-number inventory for importing more creatures
author: Jacob Repp
created: 2026-10-10T06:32:34Z
tags: [content, creatures, data-driven, memo, technical]
id: memo-032
project_id: jelligotchi
doc_uuid: 50acbef8-6ffb-44b5-bd95-6a8f455dd61d
---

# Overview

The user noted "a lot of magic numbers that we probably need to scrub as we are
going to be importing more creatures and needing things to be easier to edit
reliably". On 2026-10-10 a read-only sweep of `core/` and `include/jelli/` sorted
each literal into one of four buckets: asset IDs in code, creature and behaviour
tuning, layout geometry, and enum-like numbers. This memo is the checklist that
[RFC-005](../rfcs/rfc-005-stimulus-driven-creature-behaviour.md) works through.
Check off a line only when a test shows the behaviour is unchanged.

# Done (commits on `feat/axolotl-species`)

- [x] Creature frames, poses, scales and idle schedules come from data (ADR-012).
- [x] Moments come from `content/activities.json`, replacing the `choice` 0..3
  branches, the action-offset arithmetic and the suggestion hours.
- [x] `JelliHealthActivity` enum and one routine table replace `8001u + activity`,
  `page - JELLI_UI_BRUSH` and the 11 copies of the routine range check.
- [x] The action→command and action→icon tables use designated initialisers sized
  by `JELLI_UI_ACTION_COUNT`.
- [x] UI input codes start at `JELLI_INPUT_FIRST` (64). Codes 28–31 used to
  collide with WATER, EXERCISE and the two VOLUME actions in event logs.
- [x] The debug client reads the device's `page_id` and no longer keeps its own
  copy of the page list. The debug pages table is keyed by page enum.
- [x] The asset builder checks manifest/disk parity. It no longer needs
  hand-edited per-kind counts or an exact pixel total.
- [x] Tests strip legacy save tails through named sizes (`tests/save_layout.h`).
- [x] `JelliReaction` names the touch levels and wake moods. `JELLI_WAKE_HAPPY` and
  `JELLI_POSE_CURIOUS` replace `2u` in the render key's wake phase.
- [x] Health activities in health-ready checks and the render key use enum names.
- [x] The art builder checks the real live-reload pack, pixel and mask capacities. It
  no longer uses a hand-raised ceiling. Tests derive creature counts from disk.
- [x] `behavior_flags` names its bits (`JELLI_PET_FLAG_MESS`, `JELLI_PET_FLAG_WAS_ASLEEP`).

# Remaining, in priority order

1. **Overloaded phase.** The render key's `phase` still carries the idle pose, the
   wake-surprise flag and the exercise bob. Split it into separate fields.
2. **Species tuning into behaviour data.** Touch load 220/600/900, decay
   100/×10, gains 15/−20/3, `reaction_ticks` 30 (`personality.c`, `wake.c`,
   `game.c:68`), and the favourite moments currently chosen by **instance-ID
   parity** (`personality.c:28-47`).
3. **Newborn defaults.** They are duplicated in `game.c:42-52` and
   `collection.c:117-128` (needs 500/700/700/500/500, bond 100, hydration 700,
   bedtime 22, sleep 288000). Move them to `pets.json`.
4. **Asset IDs.** Generate `jelli_asset_ids.h` from `assets.json` keys. Replace
   `11000u + prize`, `9001u + style`, `10001u/10002u` backgrounds (in two places),
   the stat-tile icons (MOOD and SOCIAL both 7005), and **scale inferred from the
   ID range** (`icon >= 6000u ? 3u : 6u` in `pet_layout.c` and `pet_menu.c`).
5. **Needs and activity tuning.** Name the recovery floor `400u` (more than 10
   sites), plus 100/200/350/700/800/150. Move them, the decay rates
   (`game_time.c:64-74`) and the activity durations/effects
   (`game_commands.c:77-134`, `game_time.c:169-211`) into `needs.json` and
   `activities.json`.
6. **Enums.** Add Location (0/1), Prize indices and Stat indices. Name the
   `JELLI_SNAP_*` event flag bits.
7. **One day schedule.** Moment windows (5/11/15/19 h) now live in
   `activities.json`. Preference windows (660/900/1140 min) and night
   (1200/360) still disagree with them.
8. **Layout header.** Round-panel mask (`465`, `466*466`, 7 or more sites),
   `233` centre, grid, tile and button rects.
9. **Capacity before growth.** Widen these before going past 8 forms or 9
   entries: `reached_forms` (u8), `[9]` collection arrays, `JELLI_PRIZE_COUNT`
   and `jelli_foods[9]`. Each needs a save migration.

# Notes

- The save is about 3,605 of 4,096 bytes with nine pets. Version 10 added 5 bytes
  per pet; RFC-005's behaviour fields will add about 7 more.
- `jelli_creature_profile()` falls back to form 0 for an unknown form. The
  generator rejects catalogs whose forms lack profiles, so this only guards
  corrupt input.
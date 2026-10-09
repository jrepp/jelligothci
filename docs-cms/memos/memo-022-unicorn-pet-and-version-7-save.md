---
title: Unicorn pet, four-pet roster, and version 7 save migration
author: Claude
created: 2026-10-09T00:56:49Z
tags: [memo, save, ui]
id: memo-022
project_id: jelligotchi
doc_uuid: 5deec73b-a920-46e8-94c2-bb094d31ee87
---

# Overview

The user asked to "add the unicorn. Make the unicorn the default." This memo
records the roster, frame, and save changes, and the later switch to the unicorn's
own art. It follows the same steps as the ninja
([memo-020](memo-020-ninja-pet-and-three-pet-roster.md)).

# Context

Before this change a game held three pets: two Mints (form 0, which grow into
Lilac, form 1) and Nirmal the ninja (form 2). Nirmal was the default for new
players. Valid rosters needed at least three pets, and the save codec was at
version 6.

# Details

## Roster

- `JELLI_FORM_UNICORN` (3) is a fourth form, and `JELLI_FORM_COUNT` is now 4.
  Evolution still applies only to form 0, so the unicorn never evolves.
- New games start with four pets in roster order: Mint (ID 1), Mint (ID 2),
  Nirmal (ID 3), and the unicorn (ID 4, `JELLI_UNICORN_PET_ID`). All four start
  with the same stats.
- `jelli_game_new`, which `jelli_pet_init` uses, makes the unicorn active
  (slot 3). It is now the default pet for new players. `jelli_game_init` still
  builds the roster with slot 0 active.
- `JELLI_ROSTER_MIN` is 4. SWITCH PET still moves to the next slot and wraps
  (1 → 2 → 3 → 4 → 1). From the new-player default it goes to the first Mint.
- The home heading reads UNICORN for form 3. At 7 glyphs and 3x it is 168 px
  wide, centred at x = 233. A test checks that every glyph and shadow pixel of
  each name lies on the round panel. Another checks that the UNICORN heading never
  paints inside the battery indicator's rectangle.

## Save codec version 7

- `JELLI_SAVE_VERSION` is 7. The byte layout is the same as version 6, and the SDL
  storage gate uses the same constant.
- `jelli_game_add_ninja` has become `jelli_game_add_species(game, form)`. It
  appends a fresh ninja or unicorn only when all of these hold:
  - the roster is valid as stored, with two or more pets;
  - it has no pet of that form;
  - there is room.
  The new pet takes the species' stable ID (3 for the ninja, 4 for the unicorn).
  If that ID is taken, it uses the next unused ID above it, within a bounded loop.
  Mint, Lilac, and unknown forms are refused. A refusal leaves the game
  byte-identical.
- Decoding a v1–v4 save appends the ninja and then the unicorn. Decoding a v5–v6
  save appends only the unicorn. Each is added only if absent and only if there
  is room. Decoding does not change the active pet, so existing saves keep the pet
  they had. After migration the full validity check runs on the candidate. A
  failed decode leaves the output unchanged.
- A v7 record is never migrated. A v7 roster with fewer than four pets is
  rejected. A v5 or v6 three-pet record that already holds a unicorn is also
  rejected, because no pet is appended and it stays at three.
- A full eight-pet legacy roster loads without the missing species. A seven-pet
  v1–v4 roster gains only the ninja.

## Frames

The unicorn's own frames are IDs 1041–1048, `creatures.unicorn-<pose>`, in pose
order: idle-a, idle-b, eating, happy, asleep, unwell, curious, content. They are
high-resolution art (192x192 at density 6, from `unicorn-sheet.jpg`; see the slice
guide), and clips 5022–5028 mirror the ninja's. Until that art landed, the pose row
borrowed Lilac's frames (1007–1012, 1023, 1024). The integration step then:

1. Replaced the fourth row of `pose_ids` in `core/pet_actor.c` with
   `{1041u, 1042u, 1043u, 1044u, 1045u, 1046u, 1047u, 1048u}` and removed the TODO.
2. Made the same change in `tests/test_pet_roster_ui.c`, which now also checks that
   every ninja and unicorn frame is a density-6 creature whose cached bottom bound is
   its real lowest covered row.
3. Made `expected_id` in `tests/test_pet_ninja.c` return `1041u + pose` for form 3.
4. Reran `pet_roster_ui`, `pet_ninja`, `pet_battery_layout`, `pet_catch_props`, and
   `pet_ninja_screen`; all pass. The battery-layout check covers every form, pose and
   caption, including soft edges, and still sees eight distinct frames per form. The
   unicorn's frames stay clear of the indicator; the closest frame of any pet, happy
   Lilac, ends four pixels right of its rectangle.
5. Removed the playing guide's note that the unicorn is drawn with Lilac's frames.

## Tests

- New `tests/test_roster_unicorn.c` covers:
  - the new-player default and next-slot bounds;
  - two offline days without evolution;
  - SWITCH 4 → 1 → 2 → 3 → 4;
  - the species helper's ID choice and refusals;
  - v5/v6 three-pet saves keeping each active slot and gaining one unicorn;
  - a stable v7 fixed point;
  - v7 short rosters being rejected with the output untouched.
- `tests/test_roster_migration.c` covers v1–v7, every count from 2 to 8, every
  active slot, and every ninja and unicorn slot (or none), checked against an
  independent oracle. That is 10,731 cases. It also has a 3,000-mutant fuzz pass
  over v1–v6.
- `tests/test_save_storage.c` sends v5/v6 three-pet files through SDL storage.
  `tests/test_pet_session.py` drives the real binary with v4–v7 saves.
- One pinned seed in `tests/test_pet_catch_props.c` changed. With the unicorn as
  the starting pet, the walk from 0x9e3779b9 reached only two catch-rect partial
  redraws, below the five each pinned seed must show. 0xb7e15162 reaches 25. The
  full-redraw oracle passed on every frame for both seeds.

# Verification

These results are from HEAD plus only these changes, built in the scratchpad.
They exclude the parallel high-resolution asset work.

- Desktop: 51 of 51 tests pass. Core-only: 23 of 23 pass. With address and
  undefined-behaviour sanitizers: 51 of 51 pass.
- The previous (v6) desktop binary, given a v7 save, printed "Cannot load saves
  (3). Files preserved". Both slot files stayed byte-identical.
- A v6 save written by the previous binary, with Nirmal active, loaded in the new
  binary as v7. The roster became `[(1, Mint), (2, Mint), (3, ninja), (4, unicorn)]`
  and Nirmal stayed active.
- ESP32 was not built or flashed. Its loader marks an undecodable checkpoint
  protected and disables writes, as in memo-020. That was read in source, not
  exercised.

# Remaining uncertainty

- The unicorn's high-resolution frames have been checked only in pixel tests and
  headless renders, not on the AMOLED panel.
- Nothing has been flashed. The UNICORN heading's fit was checked only in pixel
  tests and headless renders.
- Downgrading to an older build with a v7 save preserves the save, but that build
  cannot load it.

# References

- [memo-020: Ninja pet and three-pet roster](memo-020-ninja-pet-and-three-pet-roster.md)
- [memo-021: Settings sound level and version 6 save](memo-021-settings-sound-level.md)
- [Playing guide](../../docs/playing.md)

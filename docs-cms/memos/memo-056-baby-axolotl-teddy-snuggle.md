---
title: Baby axolotl teddy snuggle, crying and sleep frames
author: Jacob Repp
created: 2026-10-11T02:51:24Z
tags: [activities, art, memo, prizes, save]
id: memo-056
project_id: jelligotchi
doc_uuid: 6c41ce14-4945-4cce-9c8c-f10341ba3170
---

# Overview

The owner delivered new baby axolotl (BABY AXO, form 3) art on 2026-10-10:
two frames hugging a teddy bear, two crying frames, two sleeping frames and a
standalone teddy bear. They asked for a special baby activity, "snuggling a
teddy bear", that rewards the teddy as a special item another pet can be given.
This memo records what was built and which numbers are unapproved defaults.

# Art

All seven PNGs are 3294×3546 exports on the 72-pixel grid used by
`assets/slice/source/axolotl-baby-import.json`; every bounding box lands on a
cell edge. They import with `tools/assets/import_creature.py`:

| Asset IDs | Keys | Source |
| --- | --- | --- |
| 1205–1206 | `creatures.axolotl-baby-snuggle-1/2` | Hug1 Bear, Hug2 Bear |
| 1207–1208 | `creatures.axolotl-baby-cry-1/2` | Axolotl Baby Idle 1 Cry, Cry 2 |
| 1209–1210 | `creatures.axolotl-baby-asleep-1/2` | Axolotl Baby Sleep1, Sleep2 |
| 11010 | `prizes.teddy-bear` | teddy bear 1 |

The snuggle frames are wider than the first four baby frames, and the importer
centres frames on their union. The new optional spec `anchor` positions frames
from the original four frames' source box instead, so their PNGs are unchanged
(verified by re-import). The teddy's four browns join the shared `axolotl`
palette, which now has 15 of 16 colours. The prize uses a `teddy` palette and
`source_cell` 36, which doubles the 9×11 bear to 18×22 in its 32×32 cell.

Clips: `axolotl-baby.asleep` now loops the two sleep frames (900 ms each), and
`axolotl-baby.unwell` loops the crying frames (600 ms each), matching the adult
axolotl's holds. A new `snuggle` state pose (fallback `happy`) has the clip
`axolotl-baby.snuggle` (5210), looping both snuggle frames at 700 ms.

# Activity, present and gifting

- **SNUGGLE** (moment 23, `content/activities.json`) is limited to form 3 and
  runs at home. Two new optional moment fields drive it: `pose` names the
  creature pose shown while it runs, and `prize` names the present offered on
  completion. The CMake generator resolves both from `assets/slice/assets.json`
  and rejects unknown names; Jelli Art validates through the same generator.
- **TEDDY BEAR** is the tenth present (`JELLI_PRIZE_COUNT` 10). It reuses the
  existing present system without new rules. The pet catches it, the gallery
  holds it, and **GIVE** hands it to any pet other than the one that earned it.
  Giving it removes it and grants social and bond, like any other present, and
  offers the friendship bow. Only the day's rewarded SNUGGLE can offer it
  again (see below).
- **One rewarded snuggle per pet per day.** A moment with `"daily_reward": true`
  pays costs and gives rewards and its prize only on its first run each local
  day. The reward is claimed when the costs are paid (the moment's
  `completed_moments` bit), so interruption, offline completion and save/reload
  cannot pay twice. Later runs that day play the animation for free: no cost
  check, no costs, no rewards and no teddy. `JELLI_PET_FLAG_FREE_PLAY` in the
  saved `behavior_flags` marks such a run, so a reload mid-run stays unpaid.
  Because the claim sets the completion bit at acceptance, a recipe that
  `requires` a daily-reward moment unlocks once that moment is accepted.
- **Presents gallery** lays out ten slots in rows of 3, 4 and 3; the wider
  middle row fits the round panel (checked in a rendered preview).
- **Save version 14.** The original blocks keep their nine prize slots, so
  older saves load unchanged. The teddy's origin pet is appended after the
  per-pet tail. Per-pet prize progress is still saved for the nine original
  slots only; the teddy needs none.

# Owner decisions (2026-10-10)

The owner reviewed PR #44 and chose:

- **One rewarded snuggle per pet per in-game day.** Further snuggles replay the
  animation without costs, rewards or another teddy, and the limit persists
  across save/reload. This closes the snuggle, give, snuggle farming loop.
- **Keep the SNUGGLE rewards and energy cost** as initial playtest tuning:
  amusement 120, social 250, bond 3; energy 30 (scaled by the baby's 100% energy
  profile); duration 8 s; jitter 10%; home only; available 0:00–24:00, not in
  the hourly random rotation.
- **Keep the 18×22 teddy** inside its 32×32 cell, smaller than the other
  presents (about 24–30 px); no redraw before landing.

# Unapproved defaults

- Teddy offer: the day's rewarded SNUGGLE offers it when it is not already held
  or pending. No random odds.
- Clip holds: snuggle 700/700 ms, crying 600/600 ms, sleep 900/900 ms.

# Budget

The pack now holds 121 assets: 156,480 of 196,608 live pixels and 19,560 of
24,576 mask bytes. The SDL live pack is 336,592 bytes of its 448 KiB staging
buffer (ADR-012). These are desktop authoring limits; firmware memory was not
measured.

# Verification

Compiled and tested on desktop (macOS): `make test`, `make core-test`,
`make sanitize`, and `make hooks-check`. Jelli Art's Python suites, JSDoc type
check, paint-tools JS test and Playwright UI tests pass. New tests cover these
cases:

- the SNUGGLE completion offer and the teddy gift (`tests/test_pet_rewards.c`);
- the daily limit: after gifting the teddy away, and across save/reload between
  and during runs, a same-day snuggle pays nothing, rewards nothing and offers
  no teddy. A free replay needs no energy, and the next day is rewarded again.
  Disabling the limit makes this test fail;
- the teddy's v14 save round trip;
- snuggle, crying and sleep frame selection (`tests/test_pet_creature.c`);
- prize award bounds (`tests/test_prizes.c`);
- generator rejection of unknown poses and prizes and of a non-boolean
  `daily_reward`.

`make esp-build` compiles the firmware: the app is 1,846,096 bytes, leaving 56%
of its 4 MiB partition free. The SNUGGLE scene and the ten-slot gallery were
inspected in `jelli_game_preview` renders. **Not verified on hardware:** the
physical display, touch and gameplay remain unverified on the device.

# References

- [ADR-012: data-driven creature species](../adr/adr-012-data-driven-creature-species-and-presentation.md)
- [memo-018: sleep habits and collectible presents](memo-018-sleep-habits-and-collectible-presents.md)
- [memo-042: additional location backgrounds](memo-042-additional-location-backgrounds.md)
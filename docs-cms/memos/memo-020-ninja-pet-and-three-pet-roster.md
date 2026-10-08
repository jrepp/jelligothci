---
title: Ninja pet, three-pet roster, and version 5 save migration
author: Claude
created: 2026-10-08T19:06:47Z
tags: [assets, memo, save, ui]
id: memo-020
project_id: jelligotchi
doc_uuid: 4da0277c-5c31-496b-868b-2a445dc232d9
---

# Overview

The user asked for "an animated character that looks like a little ninja" with
its animation states, added now. The ninja is a third pet with eight poses. This
memo records where the art came from and how it was processed, the roster and
save changes, the raised art budget, and what has not been verified.

# Context

Before this change a game held two pets: Mint (form 0), which grows into Lilac
(form 1). SWITCH PET toggled between the two, frame selection hard-coded the
asset IDs for two forms, and validity rejected any form above 1. The asset
builder's 160 KiB planned-pack ceiling had 256 bytes spare, while eight 32x32
frames need 17,408 bytes.

# Details

## Art provenance

- Generator: Gemini image generation (`gemini-3-pro-image-preview`, aspect 1:1,
  no style preset). The exact prompt is
  [ninja-imagegen-prompt.txt](../../assets/slice/source/ninja-imagegen-prompt.txt).
  No reference images or external character designs were supplied.
- Output: one 1024x1024 JPEG, kept unchanged as
  `assets/slice/source/ninja-atlas.jpg` (SHA-256
  `9db0500464b6bde9acfea21b62541714afbc38b660544ff5f31b4709aba3f929`). It is a 4x4 grid of 256px cells on flat magenta. The first
  eight cells hold the poses in order: idle A, idle B, eating, happy, asleep,
  unwell, curious, content. Rows 3 and 4 are empty.
- Extraction (`assets/slice/source/ninja_art.py`, run by `authoring.py`):
  background is red and blue above 190 with green below 170. One logical pixel
  is 8.0 source pixels for the whole sheet. Per cell, the sampling grid's phase
  is searched within ±4 pixels, picking the phase with the least disagreement
  inside blocks. Each logical pixel takes the majority class, mapped to the
  nearest shared-palette colour. Frames are cropped to their opaque bounds and
  placed in 32x32, centred at x=16 with the feet on y=30 (pivot [16, 30]).
- Paintover (scripted, deterministic): gold (#f5c764) stays only in the eating
  pose and becomes cream elsewhere. In the open-eyed poses (idle A/B, eating,
  curious), each small dark blob fully enclosed by cream or pink face pixels
  becomes a crisp 2x2 deep-plum (#291b35) eye.
- Reproducibility: Pillow 12.0.0 is pinned and bundles its JPEG decoder.
  `authoring.py` regenerates the eight PNGs byte-identically, and they match the
  prototype extractor's frames byte for byte.

Runtime frames are `assets/slice/creatures/ninja-<pose>.png`, IDs 1031–1038 in
pose order, with clips 5015–5021 that mirror the Mint and Lilac clips.

## Roster and frames

- `JELLI_FORM_NINJA` (2) is a third form. New games start with three pets:
  Mint ID 1, Mint ID 2, and the ninja ID 3. All three start with the same stats.
- Evolution still only applies to form 0, so the ninja never evolves.
- Validity now requires at least three pets (`JELLI_ROSTER_MIN`) and a form below
  `JELLI_FORM_COUNT`.
- SWITCH PET activates `jelli_game_next_slot`, which is the next roster slot,
  wrapping (1 → 2 → 3 → 1). The render key's and debug state's `stored_id`,
  `stored_form`, and `stored_asleep` describe that next pet. The existing
  NOT READY rules are unchanged: no switching while busy or while the sleep
  journal is active, and commands are BUSY while resuming. Slot IDs are unchanged.
- Frame selection uses one table of eight pose IDs per form, with the same state
  rules as before. An unknown form falls back to Mint's row.
- The home heading reads NINJA for form 2.
- Tunables: no production form profiles are registered, so all three forms
  inherit the same defaults. The debug tunable scope already resolves any pet ID.
- Sound cues, gifting, and the collection are not per-form. Gifting already
  works for any pet other than the prize's origin.

## Save codec

`JELLI_SAVE_VERSION` is now 5, and the SDL storage gate shares it. The version 4
byte layout is unchanged. When a version 1–4 save decodes, `jelli_game_add_ninja`
appends a fresh ninja if the stored roster is valid as a two-pet roster, has no
form-2 pet, and has room. It uses ID 3, or the next unused ID if 3 is taken. The
full validity check still runs afterwards. A corrupt legacy roster, such as a
prize origin pointing at the missing ID 3, is rejected rather than repaired.
Version 5 saves round-trip unchanged and never gain a second ninja.

Older builds reject version 5 cleanly. Their decoder only accepts versions
1..4. SDL storage classifies a higher version as incompatible and preserves the
file. ESP32 marks the checkpoint as protected and disables writes. These paths
were read in source, not exercised with an older binary.

## Budget

| Measure | Before | After |
| --- | --- | --- |
| PNG inventory | 86 | 94 (24 creature frames) |
| Raw pixels + masks | 151,296 B | 168,704 B |
| Planned pack (with 12,288 B allowances) | 163,584 B | 180,992 B |
| Ceiling | 163,840 B (160 KiB) | 196,608 B (192 KiB) |
| Spare | 256 B | 15,616 B |
| 1.75 app image (1 MB partition) | 0xd9f00, 15% free | 0xde620 (910,880 B), 13% free |
| 1.75C app image | 0xd7530, 16% free | 0xdbc50 (900,176 B), 14% free |

The ceiling was raised because the user asked for the ninja. Both images grew
by 18,208 bytes.

# Verification

- `make test`: 40 of 40 pass, including the new `roster` and `pet_ninja` tests.
  `make core-test`: 20 of 20 pass. `make sanitize`: 40 of 40 pass.
- New tests cover:
  - a table of frame IDs for forms 0–2 and unknown forms against every
    state-rule branch;
  - three-pet init and identical starting stats;
  - the ninja never evolving, and forms 3, 4, 127, and 255 being rejected;
  - SWITCH PET cycling through the game API and through taps, with the render
    key's stored pet, and NOT READY for busy, sleep journal, and resuming;
  - v1–v4 two-pet saves gaining exactly one fresh ninja and not duplicating it
    on re-save and reload;
  - a full legacy roster loading without a ninja, and an invalid legacy roster
    being rejected;
  - the v5 round trip, a v4 file that already holds a ninja, a two-pet v5
    record, and a v6 record;
  - the ninja home screen in five poses through real commands.
- Updated tests reflect three pets, v5, and 24 creature frames: game, save
  settings, save fuzz (the version sweep accepts 4 and 5 for the shared layout),
  and sprite geometry.
- The asset builder validates 94 assets (palette, binary alpha, bounds, clips).
- Both firmware images build.
- Headless SDL snapshots were reviewed. The home screen shows the ninja idle,
  eating, happy, unwell, asleep, curious, and content. Settings shows the switch
  target from each pet (`build/ninja-*.png` with `.png.json` state).

# Remaining uncertainty

- Nothing has been flashed. Appearance, readability, and touch on the AMOLED are
  unverified. The ninja's frames are taller than the jellies' (up to 29 rows), so
  at 6x its head reaches about y=82–94, just below the location line.
- At night, the background crescent, painted at a fixed spot above the jellies'
  heads, sits partly behind the awake ninja's hood. It is visible but cosmetic.
  Moving it would change the scene for every pet and is left for design review.
- The SWITCH PET ring button keeps its generic icon. The next pet appears in the
  render key and debug state, not as a drawn preview.
- The generated sheet is a review candidate, not human-approved art. JPEG spill
  was cleaned by rule, not by hand.
- Downgrading to an older build with a v5 save preserves the save, but that
  build cannot load it.

# References

- [Asset inventory and ninja section](../../assets/slice/README.md#ninja-pet)
- [Playing guide](../../docs/playing.md)
- [memo-019: Battery indicator and earlier budget](memo-019-battery-status-indicator.md)

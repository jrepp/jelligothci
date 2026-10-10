---
title: Pixel art polish pass and before/after review tooling
author: Jacob Repp
created: 2026-10-10T00:31:22Z
tags: [art, assets, preview, validation]
id: memo-021
project_id: jelligotchi
doc_uuid: ad682508-9936-49e0-91c0-cec122e1606c
---

# Overview

The user asked for a cleanup of the slice PNGs so they look clean, friendly, and
consistent on the device. They also asked for a preview workflow with before/after
comparison and deep zoom, plus authoring guidelines for consistent future art.
This memo records what was measured, changed, and verified.

# Findings

Measured on the 79 slice assets at HEAD (b5f1997):

- **Creatures and props were the main source of mess.** They came from a
  downscaled generator atlas (memo-007) and had 8–19 isolated single-colour
  pixels each. They also had broken outlines, eyes and cheeks that drifted
  between frames, and blotchy highlights.
- **Code-authored icons were clean but inconsistent.** Some had 2px ink outlines.
  Effects, several health icons, and the close, moments, and tea icons had none,
  which left 1,469 silhouette pixels without an outline. Shading styles also
  differed between families.
- Three keepsakes (butterfly, friendship bow, rainbow seed) and the rainbow effect
  did not read as their subject at 2×.
- `menus.moments` and `menus.tea` are pixel-identical. That is left as is because
  it is a content decision, not cleanup.

# Changes

- `assets/slice/source/polish.py` with `pixel_kit.py`, `creature_art.py`,
  `prop_art.py`, and `keepsake_art.py` redraws the 16 creature frames, 4 props,
  and 4 keepsakes/effects from shared shapes. All frames use one face kit and one
  ramp-based shading rule.
- `icon_polish.py` re-inks the remaining icon families: one closed 1px outline,
  despeckled fills, and a 1px bottom shadow tone on 32px icons. The pass is
  idempotent; a second run produced zero changed pixels.
- `menus.exercise` and `menus.water` were added by concurrent work during this
  pass. They are excluded (`SKIP`) and not modified.
- `tools/assets/compare_slice.py` and `compare.html` add the before/after review
  page. `docs/pixel-art-guide.md` records the style rules and review checklist.
- Manifest changes are limited to recomputed `bounds`. IDs, pivots, clips, and the
  palette are unchanged.

Result against HEAD, with ink and white exempt as deliberate single pixels:
specks went from 230 to 52. The remainder are cream glints on legacy icons, quilt
stitches, and the two skipped icons. Open outline edges went from 1,413 to 56,
and all 56 belong to the two skipped icons.

# Jelli Art follow-up

The user then asked for a dynamic, Python-backed version with painting and an
editable palette, aimed at non-technical artists.

- `tools/jelli-art/jelli_art.py` (`make jelli-art`) serves the same review page on
  127.0.0.1 with `studio.js` injected. It rebuilds data from disk and polls for
  file changes. Writes go only to PNGs named by manifest keys.
- Paint mode adds pencil, eraser, fill, and colour picker tools, mirror symmetry,
  undo/redo, live speck and open-edge metrics, **Tidy outline** (the
  `icon_polish.reink` pass), revert, and "use before". Saving writes the PNG,
  recomputes that asset's bounds, and records the key in
  `source/hand-painted.json`; `polish.py` then skips it.
- The palette editor paints with the 16 slots or any custom colour. Editing a slot
  rewrites every non-background PNG that uses the old colour, then the manifest.
  `pixel_kit` now reads its palette from the manifest so recipes follow the edit.
- Verified with agent-browser against a scratch copy (`--assets`): a stroke, tidy,
  undo/redo, save (PNG, bounds, and hand-painted list), mirror, fill, palette
  slot edit (35 assets recoloured), and reload after an outside file change.
  agent-browser's `press` floods keydowns without `repeat`. Toggle keys now ignore
  auto-repeat, and single synthetic events confirmed the bindings.
- Not done: font and background editing, multi-user locking, and browsers other
  than Chrome.

# Verification

- `tools/assets/build_slice.py` validated all 81 assets.
- `make test`: 38/38 passed, including the SDL smoke and live-asset tests.
- Headless SDL snapshots (`--demo --frames 200/650`) show both forms on the home
  and garden scenes with the new art.
- The compare page loaded in headless Chrome with no script errors. Side-by-side,
  diff, the issues overlay, the sheet, and the panel context were inspected.

# Remaining uncertainty

- The new art has not been checked on the physical board. AMOLED contrast for
  ink `#291b35` against true black, and the readability of the 1px rose and tan
  shadow tones at 2×, still need a look on the device.
- The art changes are not approved: they are a candidate for the user's review in
  the compare page. Its review notes export is the intended feedback path.
- Firmware was not rebuilt. The PNGs embed at build time, so `make esp-build`
  is needed before flashing.

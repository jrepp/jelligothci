---
title: Character palette-swap authoring and reusable generation
author: Codex
created: 2026-10-21T04:13:13Z
tags: [jelli-art, memo, tooling, validation]
id: memo-090
project_id: jelligotchi
doc_uuid: b114c6c5-6482-4cb3-8197-69ba7ab11e06
---

# Scope and contract

The owner requested palette swaps in character art creation, including blue
Jelli visitors for a later airplane scene. The `feat/character-palette-swaps`
increment adds reusable authoring support; it does not add a runtime recoloring
system or register another collectible pet for a background visitor.

The [helper](../../tools/jelli-art/palette_swap.py) applies explicit lowercase
RGB substitutions simultaneously. It preserves dimensions, alpha (including
partial alpha), and unmapped colors. Fully transparent pixels remain unchanged.
Its standalone command refuses existing outputs and source colors absent from
the image. Asset generators can use the same `remap_image` function while
retaining a source reference and map beside their recipe.

The [creator backend](../../tools/jelli-art/creation.py) accepts optional
`palette_map` in an existing pet request. It checks source colors against the
template palette, generates each saved frame once, remaps the new frame's inline
palette, preserves copied clip timing/order, and records `palette_source`
provenance. Same-size copies preserve the authored pivot; resized copies scale
it. Existing atomic validation, file reservations and stale-source checks still
guard publication. Template artwork and palettes stay intact.

The [New view](../../tools/jelli-art/creation.js) offers original-to-target color
controls after selecting a template, with a live first-frame palette preview.
The controls explicitly explain that the result is an independent copy; source
edits do not propagate automatically. Unsaved template paint, clip or size edits
hide the preview and prevent copying, with save/revert guidance that preserves
drafts. Canvas-size changes still use the existing
nearest-neighbor resampling. The preview shows template pixel geometry, not a
gameplay simulation or the eventual selected display scale.

Author instructions live in [the artwork guide](../../docs/artwork.md#create-a-palette-variant).
The helper ships through the existing studio directory COPY, so it needs no new
shared-container dependency exception.

# Verification and remaining boundaries

Local macOS validation uses the real catalog generators through
`./scripts/uv run --python 3.12 tools/jelli-art/test_creation.py`. Regressions
cover simultaneous replacement, transparent/partial alpha, palettes, source
immutability, pivots, clip order/timing, rejected invalid maps, and CLI overwrite
protection. Browser coverage exercises the actual New controls, preview and
saved palette metadata through the existing creation test suite.

Review found that the initial preview read Paint's mutable buffer while the
backend copies saved PNGs. The dirty-template guard remedies this mismatch.
Its first regression also found that the page's canvas display style overrides
the native `hidden` attribute; the preview now uses the shared `hidden` class.
The browser test covers visible preview changes, dirty-source refusal, and
successful creation after returning to clean state.

One local creation-suite run observed `Failed to fetch` during teardown of the
existing intentional-network-abort recovery test, after its assertions passed.
An isolated rerun and a subsequent complete creation subset did not reproduce
that teardown error; its intermittent cause remains unestablished. This is
separate from the deterministic hidden-canvas regression above.

The JavaScript baseline check and static container-layout check pass. These
checks do not establish container startup or a green PR revision. CI still owns
the pinned renderer's complete UI suite. No firmware build, deployment or
physical display/touch validation belongs to this authoring-only increment.

Final local results on this branch:

- `./scripts/uv run --python 3.12 tools/jelli-art/test_creation.py`: 19 passed.
- `./scripts/uv run --python 3.12 tools/jelli-art/test_server.py`: 36 passed.
- `JELLI_UI_LOCAL=1 tools/jelli-art/ui_tests/run.sh -k 'test_creation' -q`:
  9 passed, 297 deselected, including the corrected dirty-template regression.
- `tools/jelli-art/typecheck/check.sh`: no errors beyond the existing baseline.
- `python3 scripts/check-studio-container.py`: valid without building an image.
- `make docs-check` and `make docs-fix`: valid; no repairs needed.

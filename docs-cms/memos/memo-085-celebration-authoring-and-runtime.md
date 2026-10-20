---
title: Celebration authoring and runtime implementation
author: Codex
created: 2026-10-20T20:00:00Z
tags: [animation, authoring, memo, validation]
id: memo-085
project_id: jelligotchi
doc_uuid: 99bab450-e0c1-4430-99b8-3ce2f3a714e9
---

# Delivered increment

[Effects content](../../content/effects.json) contains a starter celebration with
staggered sprite tracks and creature anticipation, happy and settle cues.
The [Effects workspace](../../tools/jelli-art/effects.js) edits names, timing,
frame holds, looping, linear/smooth endpoint motion, named layers and attachments.
It provides playback, scrubbing, time stepping, undo/redo, drafts, guarded saves,
shared Paint navigation and a saved-content engine preview.

Hardpoints are edited per artwork form with coordinate fields and an explicit
canvas-placement mode. New forms copy and scale their template's points through
the shared [creation transaction](../../tools/jelli-art/creation.py). New effect
sprites use the existing New/Paint workflow and registered palette.

[ADR-015](../adr/adr-015-authored-celebration-timelines.md) records the contract and
remaining policy boundaries. There are no automatic gameplay bindings, and no
new effect data is added to saves. Test a saved celebration with:

```sh
make build
build/desktop/jelligotchi --effect celebrate
```

# Runtime and resource evidence

The [sampler](../../core/effect.c) uses absolute elapsed time and bounded arrays.
The [pet adapter](../../core/pet_effect.c) restores ordinary presentation after
completion or cancellation. Sprite masks, night treatment, circular clipping and
region restoration use the existing canvas. Pose cue clips start at the authored
cue time, even when a rendered frame skips its start boundary.

A local arm64 desktop sizeof probe measured 1008 bytes per effect definition,
56 bytes per track, 200 bytes per sample and 4 bytes per hardpoint. Definitions
and point tables are immutable flash/rodata; the UI owns one sample plus playback
identity and clock fields. There is no added frame buffer. These are desktop
measurements, not a firmware stack high-water measurement. Limits live in the
[public header](../../include/jelli/effect.h) and
[validator](../../tools/assets/effect_data.py).

Active timeline ticks currently require full-surface redraws. This favors correct
layer restoration over dirty-region optimization; device frame cost is unmeasured.
Normal, power-validation and factory firmware compilation and profile-isolation checks pass. No physical display, touch or performance validation
has been performed for this increment.

# Validation and findings

Validation on the working tree of `feat/celebration-effects`, based on
`35f8765d27429235d9da7b8e76493af0069d585d`:

- `make test`: all 92 desktop/host checks pass, including incremental rebuilding
  of saved effect and hardpoint data through the actual engine preview.
- `make core-test`: all 41 portable checks pass.
- `make sanitize`: all 92 checks pass under address/undefined-behavior sanitizers.
- `make lint-c`: formatting, static analysis and source-size checks pass.
- Studio type checking passes with three previous callback-shape exceptions
  removed and no added exceptions. All 15 creation tests pass, including new
  effect artwork and copied/scaled hardpoints for both supported canvas sizes.
- The full local browser run passed 289 cases and failed the initial undo-selection
  case plus two macOS-sensitive Paint baselines. Undo selection was repaired.
  The pinned Playwright image then passed all 33 view/effect cases, including
  both Paint cases. A final pinned run passed all six Effects cases, adding the
  new-pet refresh path. No accessibility baseline exceptions were added.
- Documentation checks and repairs pass; repairs sorted the new documents' tags.
- A headless `--effect celebrate` render was visually inspected along with the
  browser preview and desktop workspace. Files are under ignored `build/effects`
  and `build/ui-tests/screenshots`.

The pause test initially compared the entire pre-pause UI with its paused state,
including the changed pause label; it now verifies repeated paused frames and
the frozen effect timestamp. Static analysis required an explicit comparison of
the string lookup result. SDL startup was split by responsibility to stay within
the complexity limit. Standalone creation tests exposed an import-path assumption;
the validator now uses the existing checkout-module loader. Browser testing
caught undo returning to the first track instead of retaining selection. The
repaired undo path and shared Paint navigation pass. Visual review moved preview
and timeline side by side and reduced hardpoint label overlap.

Tests cover interval boundaries, invalid references, overlapping cue rejection,
frame loops, large elapsed time, read-only/stale saves and runtime teardown.
Browser tests cover drafts, conflict cancellation, continued editing during saves,
hardpoints, scrubbing, history, shared Paint navigation and accessibility.
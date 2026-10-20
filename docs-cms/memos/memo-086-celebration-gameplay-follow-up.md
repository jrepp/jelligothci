---
title: Celebration gameplay bindings and physical validation preparation
author: Codex
created: 2026-10-20T22:00:00Z
tags: [animation, authoring, memo, validation]
id: memo-086
project_id: jelligotchi
doc_uuid: c94a296e-54ba-490d-b8b3-f205e5675cc7
---

# Follow-up increment

After publishing the initial celebration system at `534e98e`, the owner requested
continuing the gameplay and physical-validation follow-up. No gameplay-default
selection was supplied, so [shipped bindings](../../content/effects.json) remain
disabled. The Effects workspace now authors optional evolution and present-catch
bindings using the same guarded saves, drafts and undo flow as the timeline.
Missing bindings in older documents also mean disabled. The
[validator/compiler](../../tools/assets/effect_data.py) resolves effect references
at build time; rebuild to apply a saved binding to a host.

[Event delivery](../../core/pet_celebration.c) consumes the existing bounded event
ring, skips offline resume, and retains one pending celebration for the active
pet/form. Latest eligible event replaces pending work; current playback is not
interrupted. Menus defer playback so catching a present preserves the collection
view, then celebrates on returning home. Pause freezes the animation clock and
defers starts. Pending work expires after 30 animation-clock seconds; identity
changes or clock reversal discard it. Sequence rollover resets the cursor; event
ring overflow retains only existing available history. No simulation/save fields
or framebuffers are added. The caller-owned pending state has a compile-time
32-byte budget; two immutable binding bytes are generated for the current catalog.

Care/reward completion bindings remain outside this increment. They need a
separate event policy because the current completion counter also includes wake
rewards. See [ADR-015](../adr/adr-015-authored-celebration-timelines.md).

# Physical validation preparation

USB discovery through `./scripts/esp ports` on this date found only Bluetooth and
system debug-console ports, with no ESP32 board. Nothing was flashed. Physical
display, touch, frame cost and stack/heap evidence remain unverified.

The new [debug handler](../../core/debug_effect.c) supports `effect play KEY` and
`effect stop` on both game-profile hosts. The
[CLI](../../tools/debug/jelli_debug.py) exposes `effect celebrate` and `effect stop`.
Replay is session-only and does not synthesize a gameplay event or modify a save.
Capture, open menus, pause and resume reject playback; unknown keys preserve the
existing effect. Stop also clears pending playback.

When the intended Waveshare ESP32-S3-Touch-AMOLED-1.75 is connected:

1. Rediscover its port, identify the normal game image, flash using the existing
   deployment workflow, and check hashes, version, reset cause and app-ready log.
2. Return home, then run `./scripts/jelli-debug --port PORT effect celebrate`.
   Observe anticipation, happy and settling poses, sprite motion/occlusion around
   the pet, circular clipping and clean restoration after completion.
3. Replay, open navigation by touch during playback, and confirm cancellation
   leaves no sprite residue and touch remains responsive. Close the menu and
   replay again. Observe normal gameplay after the sequence.
4. Capture existing display diagnostics and a framebuffer screenshot for software
   evidence. Record physical observations separately; a screenshot or serial
   acknowledgment does not establish panel motion, touch or frame pacing.

# Verification

- `make test`: all 93 checks pass; `make core-test`: all 41 checks pass.
- Replay and stop succeed through the CLI against the actual SDL debug socket.
- `make lint-c` passes formatting, clang-tidy, Cppcheck and source-size gates.
- All five effect schema/persistence tests, 15 creation tests and 27 game-preview
  tests pass. TypeScript checking adds no baseline exceptions.
- The pinned browser image passes all seven Effects cases, including binding
  save/reload and undo/redo. An additional delayed-response test passes for the
  corrected in-flight effects reload path. The workspace screenshot was inspected.
- Normal, power-validation and factory firmware builds pass; all three firmware
  profile-isolation checks pass. These are build checks, not physical evidence.
- Documentation validation and repairs pass with no unresolved findings.

The end-to-end event test initially advanced more simulation time per call than
its bounded live-step budget admits; using ordinary 100 ms steps reaches the
expected evolution boundary. Clang-tidy also required multiplication in the
explicit clock width. An initial analysis run overlapped header formatting and
observed a partial file; the subsequent stable-tree run passes. Browser testing
caught an exact-label locator mismatch and an intermittent newly created pet
refresh failure. The test now uses the accessible combobox role, and reload waits
for an in-flight effects load before fetching fresh content. A delayed-response
regression verifies the latest saved content wins. Runtime tests cover disabled
bindings, exactly-once consumption, live evolution/catch events, menu/pause/active
playback deferral, expiry, pet change, backwards time, resume suppression,
sequence rollover, and debug replay rejection/preservation.

`make sanitize` passes all 93 checks under address/undefined-behavior sanitizers.
A final USB discovery still found no ESP32 board.

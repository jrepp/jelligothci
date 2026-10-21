---
title: Traveling activity, airplane cabin and ambient motion
author: Codex
created: 2026-10-20
tags: [activities, assets, memo, rendering, scenes, validation]
id: memo-091
project_id: jelligotchi
doc_uuid: 29937c6c-9810-46b7-9711-ed0eed7d8646
---

# Requested increment

After the gift/crafting ledger was written, the owner requested Traveling, an
airplane seat beside a porthole with drifting clouds, and occasional blue Jelli
bouncing through the aisle. This implements that presentation independently of
proposed crafting unlocks. Existing activity IDs remain stable and Traveling preserves the current location
by allowing every existing location without randomizing it.
The [activity catalog](../../content/activities.json) owns availability and duration:
the initial manually initiated 30-second presentation has no costs, rewards or
random selection. This duration is an implementation choice, not an approved
balancing requirement.

# Implementation

The [airplane scene](../../content/scenes.json) binds the appended Traveling ID.
The [pixel geometry recipe](../../assets/slice/source/airplane_art.py) draws the
cabin and seat, and creates the blue aisle visitor by remapping existing baby Jelli
art through the source recipe’s simultaneous RGB remapper. The visitor
is a background actor, not a new gameplay form. Source alpha and face geometry are
preserved before nearest-neighbor scaling. All layers stay behind the main pet and
HUD; the visitor crosses the lower aisle, with the HUD naturally occluding its
central passage. The seat aligns to the existing pet's ground anchor.

The [shared schema/compiler](../../tools/assets/scene_data.py) validates bounded
ambient speed, cycle duration and bounce alongside optional activity bindings.
The [portable compositor](../../core/scene.c) uses independent integer transforms;
clouds repeat horizontally while the visitor spends part of each cycle off screen.
There is no runtime RNG or spawn allocation. Animation advances with the injected
presentation clock, freezes during pause, resets on reversal and drops stall
backlog. The [binding](../../core/pet_scene.c) clears when the activity ends,
retains explicit diagnostic overrides, and publishes only complete resolved scenes.

SDL and the preview tool reuse their existing caller-owned scene caches. Firmware
binds one static cache after initialization; no cache lives on a task stack and no
steady-state heap allocation is introduced. The [development guide](../../docs/development.md#layered-background-presentation)
owns the reusable runtime contract. The studio Ambient motion inspector previews
and saves these fields, and scene duplication drops the unique activity binding.

# Resource evidence

Native macOS measurements from this increment, not device timing:

- `sizeof(JelliScene)` is 8,352 bytes; the existing compile-time 9 KiB ceiling passes.
  `sizeof(JelliPetUi)` is 8,656 bytes. Firmware cache allocation is static internal
  storage; firmware layout/build evidence is pending integration verification.
- The new artwork required 211,008 total live pixels and 26,376 mask bytes, exposing
  the prior desktop live-bank ceiling. The measured complete live pack is 450,624
  bytes. The [bank](../../ports/sdl/asset_reload.h), [exporter](../../tools/assets/build_slice.py)
  and [studio validation](../../tools/jelli-art/animation.py) now share increased,
  fixed limits. The staging ceiling also covers exporter metadata allowances.
- Native `sizeof(JelliAssetBank)` is 496,280 bytes and reload storage is 1,516,880
  bytes. This bounded desktop startup workspace grows by 204,800 bytes across its
  two banks and staging buffer. Firmware does not allocate these desktop banks.
- Quantized ambient motion marks the composite dirty, including motion that may
  currently be off screen; changed composition requests the existing full surface.
  This increment makes no physical frame-rate, power or transfer-cost claim.

# Findings and validation

The initial strict compile rejected a narrowing conversion after unary negation;
the bounded integer expression is now narrowed after negation. Static analysis
then caught an implicit `strcmp` truth test and extra UI struct padding; explicit
comparison and grouping flags with existing booleans resolved both.

The first host/sanitizer run exposed the artwork bank ceiling through studio
validation failures; raising the fixed desktop capacities consistently repaired
the cause. One browser test hardcoded the starter scene count; its history check
now derives the initial count so it verifies undo/redo independently of catalogs.
An activity-policy fixture assumed Snuggle was the final activity; it now selects
that known fixture by name before appending a new example.
The new activity integration test initially assigned a moment into an otherwise idle
pet, which correctly failed game validation. It now starts the real game command
and advances bounded ticks through completion, exercising the actual lifecycle.
An initial live-pack command used unsupported options forwarded to the SDL binary;
the corrected `--build-only --pack` invocation produced the measured pack above.

Validation:

- Desktop: all 93 test cases passed across the final full run and focused rerun
  of the corrected activity lifecycle fixture; core-only: all 41 passed.
- Static: `make lint-c` and all configured pre-commit hooks passed, including
  TypeScript with no new baseline entries and the container layout checker.
- Scene browser tests: all 14 passed, including zero-travel ambient motion, pause,
  motion saves, unique-binding duplication and keyboard/accessibility checks.
- Native render: `jelli_game_preview --moment 25 --minute 720 --frames 11 --step_ms 200`
  produced an inspected Traveling image and animation without a scene override.
  Ignored artifacts are `build/airplane-activity.png` and `.gif` in the implementation
  worktree. These show the seated pet, cloud window and blue aisle visitor.
- Documentation: `make docs-check` and `make docs-fix` passed; only tag ordering
  was repaired, reviewed and retained.

The complete sanitizer suite passed all 93 cases. The full local browser suite
passed 306 cases and failed one Paint creature desktop accessibility baseline:
macOS exposes an additional “Sprite details, scrolls sideways” group. The exact
failure reproduced on clean base revision `e88ae79` in an isolated detached
worktree. The pinned Linux ARM64 renderer passed all 14 scene tests and that Paint
case (15 total). A separate pinned Paint run with screenshot comparison enabled
also passed. No accessibility or screenshot baseline was changed. These local
results do not substitute for required checks on the eventual PR head.

Firmware compilation is assigned to the prepared integration checkout; no flashing
or physical display/touch testing is authorized or performed.
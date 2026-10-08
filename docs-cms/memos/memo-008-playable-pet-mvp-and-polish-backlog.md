---
title: Playable pet MVP and polish backlog
author: Jacob Repp
created: 2026-10-08T01:40:04Z
tags: [game, implementation, memo, validation]
id: memo-008
project_id: jelligotchi
doc_uuid: 49dea459-b78a-42ff-afc1-e1d55bebb250
---

# Overview

The user requested that the slice assets be committed, followed by a quick MVP
using inexpensive subagents and durable notes for polish and fixes. Asset commit
`fef3ad0` preserves the artwork, preview builder, and preceding RFC work. Three
GPT-6-luna agents implemented simulation, save/storage, and rendering/UI; the
primary agent integrated hosts, builds, tests, and this record.

This is a playable subset of RFC-001, which remains Draft. It does not implement
all proposed content, time, storage, input, or resource contracts. No firmware
was flashed during this increment. Prior shapes firmware measurements must not
be presented as measurements of this pet renderer.

# Implemented behavior

| Area | MVP behavior |
| --- | --- |
| World | Fixed capacity eight pets; two initialized pets with stable IDs; one active; stored pet clocks and needs freeze |
| Care | Five needs, feed/play/clean, recoverable illness, free BasicCare with a 30-second recovery floor; no death |
| Inventory | Food and gift stacks capped at 20; timed actions charge at completion; cancellation does not spend items |
| Gifts/rewards | One gift type adds bond; first useful feed creates a one-time, explicitly claimed three-food reward per pet; full inventory preserves the pending claim |
| Sleep | Manual rest/wake, low-energy naps, relative bedtime and eight-hour sleep window, wake override, awake dwell and hunger grace |
| Progression | Baby to grown after 60 active seconds for review; terminal grown form; two locations and collection switching |
| Timing | Injected time, 100 ms live ticks, at most eight ticks per call, two-second admitted backlog with discarded-time diagnostic |
| Offline | Six-hour cap, at most eight minute endpoint segments per call; only active pet progresses; commands locked until resume completes and desktop commits |
| Presentation | Shared round 466x466 UI, five menu pages, sprites/font, inventory and save state; full redraw only when visible state changes |
| Desktop storage | Versioned explicit little-endian codec, content version, CRC and commit marker; two alternating files, flush/sync and read-back verification; newest-valid fallback |
| Firmware | Same game/UI/assets compiled into ESP32; existing PSRAM buffers, display mutex, and touch queue retained; explicitly volatile and time-unavailable |

The care/evolution values are accelerated implementation fixtures, not approved
production tuning. Hunger is currently the only tracked neglect episode. The
reward ledger is two flags per pet, not the generalized event/reward ledger in
the RFC. Pets and locations are fixed fixtures; there is no creature creation,
slot reuse, branching evolution, persistent placed-object system, or runtime
content-pack loader.

Game definitions are compiled C. `tools/assets/embed_slice.py` validates the
29 tracked PNGs and emits immutable C pixel/mask/font arrays under `build/`.
This is asset embedding, not the proposed gameplay definition compiler. The
SDL-free core build needs no Python, Pillow, or SDL. Rendered macOS/Linux and
ESP32 builds use the repository-local pinned Python/Pillow path. Native Windows
rendered builds remain unsupported; portable core CI remains configured there.

# Save and time boundaries

`make run` selects `build/pet-save`; direct execution without `--save` is explicitly
unsaved. Desktop saves on explicit request, selected progression commands,
60-second intervals, and exit. It captures a wall/monotonic pair and drains the
bounded admitted backlog before encoding. IO completion never replaces that
snapshot's time anchor. Failed writes retain an independent reconciliation flag,
so a retry checks the disk sequence even if the UI changes Failed to Pending.

Resume uses a capped wall-clock difference; missing/backward clocks forgive the
gap. Startup renders bounded batches and commits the resumed state before live
commands. A second startup cannot repeatedly apply the same offline interval.
Corrupt or incompatible files are preserved; startup fails with recovery guidance.
A valid older slot can recover from a damaged newer slot. Concurrent processes
writing the same base path are not supported.

Desktop IO is synchronous outside the frame callback and can stall the host.
There is no asynchronous save worker, retained source-epoch/uncertainty model,
migration path, parent-directory durability guarantee, or ESP32 flash adapter.
Tests exercise damaged files and restart behavior, not physical power loss.
Pet sleep changes simulation state; it does not enter ESP32 hardware sleep.

# Validation and integration findings

Local macOS validation for this implementation:

- `make test`: 12 tests, including deterministic game, renderer, codec, storage,
  headless demo, and desktop restart tests.
- `make core-test`: six tests with SDL disabled.
- `make sanitize`: the same 12 desktop tests under ASan/UBSan.
- `make lint-c`: formatting, clang-tidy, Cppcheck, function/file/complexity limits.
- `make esp-build`: pinned ESP32-S3 toolchain compiles the shared implementation;
  image `0x9aa30` bytes, 40% of the 1 MiB application partition free.
- SDL window opened successfully. Deterministic baby/home and grown/garden
  screenshots were inspected after correcting prop/status overlap. These are
  desktop observations, not physical panel or touch verification.
- `make docs-check`, `make docs-fix`, and `make hooks-check` passed. The only
  documentation repair alphabetized this memo's tags; it was reviewed. Shell
  syntax and ShellCheck also passed for the modified C lint wrapper.

Measured caller-owned state is 1,064 bytes for `JelliGame` and 1,264 bytes for
`JelliPetEngine`. Static assertions cap the pet engine at 32 KiB and desktop save
workspace at 16 KiB. Existing two 466x466 RGB565 buffers total 868,624 bytes;
BSP/LVGL allocations are additional. These figures are not a complete heap,
stack, frame-time, or firmware budget measurement.

Integration exposed and resolved these issues:

1. IDF's early component discovery cannot run `file(GLOB CONFIGURE_DEPENDS)`.
   The component now registers dependencies and returns during
   `CMAKE_BUILD_EARLY_EXPANSION`; normal configuration generates artwork. The
   subsequent firmware build succeeded. Keep asset generation in normal CMake
   configuration when extending this component.
2. Per-frame integer need changes discarded fractions, and sleep reverses energy
   direction. Persisted per-need remainders now preserve fractional progress across
   live updates, sleep changes, and save/load; deterministic tests cover this.
3. UI labels/navigation, prop placement, and stale render invalidation needed
   integration fixes. Exact visible-state comparison now drives redraw decisions;
   hit testing uses the same button layout and respects the circular boundary.
4. Save-write branching exceeded the complexity limit. Slot validation was split
   into a helper without relaxing the gate. Read-back and sequence behavior remain
   covered by storage tests.

# Prioritized polish and fixes

| Priority | Work and owning area | Acceptance evidence |
| --- | --- | --- |
| P1 | ESP32 time retention and storage, `ports/esp32`: prove RTC/source continuity, add explicit uncertainty policy and flash save adapter | Battery/reset/clock-jump matrix; power-cut tests; no repeated offline award; visible recovery state |
| P1 | Desktop save worker and durability, `ports/sdl/session.c` and `storage.c`: immutable queued snapshots, acknowledgments, directory durability, failure injection | UI remains responsive during slow IO; uncertain completion/retry tests; validated old/new slot after each injected failure |
| P1 | Physical board review: font size, touch coordinates, latency, memory/stack and frame-time budgets | Photos/observations plus separate serial/performance logs on the actual SKU |
| P2 | Content definitions/compiler: bounded tables and branching predicates, stable content IDs, validated migrations | Invalid-reference/cycle/bounds fixtures; golden save/content compatibility cases |
| P2 | Reward/gift expansion, `core/game_commands.c`: repeatable earning, bond/discovery milestones, preferences, finite gift replenishment | Deterministic grants and claims; no double credit; full-stack and cancellation cases |
| P2 | Collection/object systems: creation, reusable slots with generations, placed objects and location capacity | Stable target/replay behavior; no stale command mutates a replacement pet |
| P2 | Input, `ports/*` and `core/pet_ui.c`: press/release, hold/drag, overflow cancellation, collection confirmation | Queue-overflow/release-loss tests and physical gesture trials |
| P2 | Readability and presentation, `core/pet_render.c`: larger bitmap labels, transient feedback, open-gift animation, better poses/layout and smaller damage regions | Native-size board review; screenshots for every page/state; measured frame cost |
| P2 | Care tuning and time policy: rates, evolution gates, neglect beyond hunger, long-session/offline comparisons, pause feedback | Playtest notes, seeded replay corpus, explicit live/offline endpoint expectations |
| P3 | Tooling: author-friendly definition files, asset ID code generation, UI capture gallery, native Windows rendered build | Repeatable clean-machine builds and reviewed generated artifacts |

Before extending the slice, read this table and the relevant RFC section. Avoid
interpreting the presence of fields such as random state or revision as evidence
that deterministic randomized content or replay logging is complete.

# References

- [RFC-001: Virtual pet systems](../rfcs/rfc-001-virtual-pet-systems-architecture.md)
- [Memo-007: Artwork and preview](memo-007-slice-artwork-and-preview.md)
- [Memo-004: Process learnings](memo-004-process-learnings-and-context-remediation.md)
- [Development and runtime commands](../../README.md)
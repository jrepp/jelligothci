---
title: Portable background scene composition and synthetic motion
author: Codex
created: 2026-10-20
tags: [memo, motion, rendering, scenes, validation]
id: memo-080
project_id: jelligotchi
doc_uuid: a269e9f5-8bbc-4c5d-bc26-aa8b1cec5f7f
---

# Scope and implementation

The owner continued the [authoring increment](memo-079-background-scene-authoring.md)
with sideways travel first. This increment implements the portable composition stage
of [RFC-011](../rfcs/rfc-011-layered-backgrounds-and-motion.md). The RFC remains Draft
for subsequent activity, orientation and forward-travel work.

[The compiler](../../tools/assets/scene_data.py) shares the studio's strict validator,
resolves references to stable asset IDs, emits immutable C definitions and reports
unique decoded image bytes per scene. CMake tracks scene JSON and compiler edits;
preview tests verify an incremental content edit reaches the renderer.

[The core](../../core/scene.c) uses caller-owned fixed storage, integer transforms,
wrapped fractional phases and binary-mask composition. All image views are checked
before modifying the cache. [Pet integration](../../core/pet_scene.c) publishes only
complete selections and retains the previous complete background on resolution
failure. It reacquires views each frame, keeps no borrowed image pointers, and handles
same-address asset reload through the existing UI invalidation contract. Changed
quantized output redraws the full surface; unchanged fractional motion adds no damage.
Actor/particle restoration, night shading, round clipping and strided surfaces use
the same composite. Clearing an override restores the normal location background.

The [SDL adapter](../../ports/sdl/scene_preview.c) injects tilt and signed travel,
tracks fractional elapsed distance, drops stall backlog and freezes motion when
paused. Its controls are in the [development guide](../../docs/development.md#layered-background-presentation).
Jelli Art opens saved scenes in **Test in game**, with explicit tilt and travel inputs
rendered by the same core. The browser authoring preview remains independently useful.

No gameplay activity chooses a scene automatically. No save format changes, sensor
acquisition, inferred walking distance or device calibration are introduced. Firmware
compiles the API but does not allocate/select a scene cache. Forward projection and
walking-animation synchronization are still pending.

# Resource evidence

Measurements below are from the 2026-10-20 local desktop build, not device timing.
The [public types](../../include/jelli/scene.h) and [composition loop](../../core/scene.c)
are authoritative for limits and ownership.

- Native `sizeof(JelliScene)` was 8,304 bytes, including an 8,192-byte RGB565 cache.
  A compile-time ceiling enforces the 9 KiB cache budget. SDL and the preview own
  static caches; the core does not allocate. Descriptor scratch is bounded by layer
  capacity and contains only borrowed pointers and metadata, not image buffers.
- Native pet UI state grew from 8,376 to 8,400 bytes. The firmware includes the added
  UI fields but has no selected scene cache. Native sizes do not prove ESP32 layout.
- The generated resource report measured 34,816 unique decoded image bytes for each
  starter scene. The six shared kit images total 52,224 bytes of pixels and masks,
  already embedded as constant assets; composition does not allocate decoded copies.
- A changed composite still requests the existing full surface: 434,312 RGB565 bytes
  at the current display dimensions. The device keeps its separate engine and LVGL
  PSRAM buffers and existing copy/mutex contract. These figures exclude BSP buffers,
  tasks, queues and driver allocations and do not represent total firmware RAM.
- An optimized native C harness forced 10,000 compositions of the maximum twelve
  fully covering, repeating 256-square layers. CPU clock time averaged 48.30 µs per
  composition with AppleClang `-O2`. This covers source composition only, excluding
  pet rendering and display transfer. Harness and executable are ignored artifacts
  under `build/scenes/bench.*`; this is historical desktop evidence, not an ESP budget.

# Findings and remedies

The first strict lint pass rejected struct-wide comparisons in failure-atomicity
tests because C structs contain padding. Tests now compare scalar fields and each
array explicitly. Cppcheck also could not establish initialization of image descriptors
when considering a zero-count definition; scratch descriptors now start zeroed before
resolution, and the core still rejects zero-count definitions. A maximum-capacity test also triggered the widening-conversion lint rule; its
image capacity now derives from the array size before assignment. No check was
suppressed.

Two browser invocations supplied nonexistent test paths and collected no cases. The
wrapper always includes the full suite directory, so explicit file arguments also
started an unintended full run. That run was stopped, and the final invocation uses
`-k` to select scene and view cases. Neither collection failures nor the interrupted
run are counted as passing suite evidence.

# Validation

| Layer | Evidence |
| --- | --- |
| Shared logic and integration | `make test core-test sanitize` passed all 89 desktop, 40 core-only and 89 sanitizer tests. The final maximum-capacity test was additionally rebuilt and rerun in all three configurations. |
| Renderer | Tests cover rounding, reverse/subpixel travel, phase wrap, saturated inputs, invalid definitions/views, retained caches, reloads, scene clearing, full/zero damage, partial restoration, particles, circular clipping and padded surfaces. A native street screenshot was visually inspected. |
| Preview service | All 26 tests passed after final argument handling, including actual C-rendered tilt/travel and scene-content incremental rebuilds. Flat previews omit scene-only arguments for older checkout compatibility. |
| CLI | Headless extreme tilt and reverse speed succeeded; unknown scenes, motion without a scene, excess speed and scenes in shapes mode were rejected. |
| Static | `make lint-c` passed format, Clang-Tidy, Cppcheck and size checks. TypeScript added no exceptions; six obsolete baseline entries were removed. |
| Documentation | `make docs-check` and `make docs-fix` passed with no repairs or unresolved findings. |
| Firmware | Initial and incremental `make esp-build` passed. The final application was 0x1d0230 bytes, with 55% of its app partition free. No flashing or hardware interaction was performed. |

The pinned Linux ARM64 browser scene/view run passed 34 cases and found one test
that reloaded an actively edited number before its change event committed the value.
The test now presses Tab before reloading, matching the existing form contract.
The focused rerun passed, completing all 35 selected cases across the two runs. Screenshot comparisons were
enabled; no screenshot or accessibility baselines changed in this runtime increment.

No physical display/touch or sensor evidence was collected. The desktop performance
measurement does not establish device latency, power, frame rate, stack headroom or
orientation behavior. Gameplay scene choice, activity distance, walking animation,
reduced motion policy and forward projection remain future increments.

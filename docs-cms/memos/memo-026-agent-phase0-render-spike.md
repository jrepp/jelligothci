---
title: Agent phase 0 render spike, the 384 px unicorn on the CO5300
author: Claude
created: 2026-10-09T17:09:55Z
tags: [agent, esp32, memo, performance, rendering]
id: memo-026
project_id: jelligotchi
doc_uuid: ecc6f74f-f5a5-4789-8415-de7977ead849
---

# Overview

The last measurement of [RFC-003](../rfcs/rfc-003-voice-agent-companion.md) phase 0:
how fast the agent can animate a large unicorn without LVGL, drawing into a PSRAM
frame and presenting changed rectangles to the panel. The answer shapes the agent's
animation design: small regions are cheap at any rate the panel can show, while
moving the whole 384 px body every frame is not, yet.

# Context

- Panel: CO5300, 466 x 466, 40 MHz QSPI, tearing effect at 59 Hz (memo-024).
- Art: the pet's eight unicorn poses at 32 x 32 logical, density 6 (192 x 192 stored,
  RGB565 plus 8-bit alpha, ADR-012), drawn at logical scale 12, so 384 x 384 on
  screen: each stored pixel fills a 2 x 2 block.
- Agent at 240 MHz, performance optimisation; earlier, the pet's LVGL canvas path
  managed about 7 full frames per second (memo-006).

# Details

## What was built

- `core/agent/scene.c`: draws a high-resolution sprite at a whole scale over black
  inside a clip rectangle, clearing the clip first and sampling each stored pixel
  once per output block. Drawing a frame as several clips equals drawing it whole;
  host tests check this against an independent per-pixel reference, including the
  "bob" update (redraw the union of the old and new rectangles), and a seeded sweep
  over random sprites, every scale, positions near `INT_MIN`/`INT_MAX`, clips whose
  fields overflow 32-bit sums, and padded strides.
- `tools/assets/embed_slice.py --keys`: embeds only assets whose key starts with a
  prefix, plus the font. The agent embeds the unicorn's poses (about 0.9 MB of
  rodata), so its image is 1.9 MB of the 4 MB `ota_0`.
- `ports/esp32-agent/main/screen.c`: presents any rectangle by copying its rows from
  the PSRAM frame into the 14,912-byte internal DMA stripe (swapping to big-endian)
  and sending it, widened to the CO5300's even window edges. The widening and the
  rows per send are portable (`core/agent/present.c`) and tested over every span of
  the 466-pixel axes; damage fields are summed in 64 bits, so a huge rectangle clips
  instead of wrapping. `render_bench.c` adds
  `render bench full|bob|patch SECONDS` (`scripts/jelli-agent render bench …`).

## Measurements (1.75C, 5 s per mode)

| Mode | Rectangle per frame | Frames/s | Render | Present |
| --- | --- | --- | --- | --- |
| full: whole screen | 466 x 466 (217,156 px) | 12.3 | 37.0 ms | 44.0 ms |
| bob: body moves ±6 px | about 384 x 385 (147,991 px) | 17.0 | 33.3 ms | 25.3 ms |
| patch: head region | 192 x 96 (18,432 px) | 142.8 | 3.6 ms | 3.3 ms |

- A second session, after the present arithmetic moved into `core/agent/present.c`,
  measured bob at 13.1 frames/s (present 42.5 ms) and patch at 115.9 (present 4.9 ms)
  with the same rectangles; full was unchanged at 12.3. A build with the original
  inline loop gave the same slower figures in that session, so the refactor is not
  the cause. Why partial windows presented faster in the first session is not
  explained; where the PSRAM frame lands and the cache state are candidates. The
  final build measured patch at 127 frames/s (present 4.1 ms). Treat the
  partial-window present figures as a range.
- Present runs at about 5.9 to 7 M pixels per second (about 11 to 14 MB/s against
  the QSPI's 20 MB/s): copying and DMA alternate, never overlap.
- Rendering the whole 384 px sprite costs about 33 ms, dominated by blending
  147,000 output pixels into PSRAM.
- A head-sized update, the work an eye blink, a mouth shape, or a small effect
  needs, takes 7 ms in total, far inside a 59 Hz panel frame.

## What it means for the agent's animation

RFC-003's plan holds: the body pose redraws only when the unicorn's state changes
(listening, thinking, success), and per-frame motion is limited to small regions:
eyes, mouth, sparkles, the bezel ring. Whole-body motion at about 17 frames per
second would look choppy. Known improvements, not yet built: render each body pose
once into a cached PSRAM image and move it with row copies instead of resampling;
overlap the stripe copy with DMA (two stripes); align presents to the 59 Hz
tearing-effect signal on GPIO13; keep the background black so blending stays
cheap.

## Review notes

The security review passed with low findings only, recorded here rather than fixed
(the project's rule for findings below medium): `jelli_agent_rect_union` adds
fields in 32 bits, so rectangles that were not clipped to a surface could wrap;
`jelli_agent_scene_draw` and `jelli_agent_sprite_rect` trust the surface's
`pixels` and `stride` the way their only caller provides them, without the
validity check `core/engine.c` applies; `screen.c` marks the DMA stripe unusable
after a timed-out transfer but not after a failed `draw_bitmap` call; and
`embed_slice.py --keys` writes its output before rejecting a filter that matched
nothing, and does not flag one prefix of several that matches nothing.

# Remaining uncertainty

- Tearing was not judged by eye, and presents were not synchronised to the TE
  signal.
- The art is the pet's 192 px unicorn scaled 2x; purpose-drawn 384 px poses (RFC-003)
  cost the same to blend but would need the `art` partition for size.
- Power draw at a steady 60 Hz patch rate is unmeasured.

# References

- [memo-024: two apps and hardware probes](memo-024-agent-phase0-two-apps-and-hardware-probes.md)
- [memo-025: audio spike](memo-025-agent-phase0-audio-spike.md)
- [memo-006: input animation and frame performance](memo-006-input-animation-and-frame-performance.md)
- [ADR-012: high-resolution asset density](../adr/adr-012-high-resolution-asset-density-and-flash-partition.md)

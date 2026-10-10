---
id: memo-031
title: ESP32 render acceleration review
author: Codex
created: 2026-10-10T05:49:38Z
tags: [embedded, investigation, performance, rendering]
project_id: jelligotchi
doc_uuid: 8b3ceb2c-e4f8-4f28-8c1b-c8a825597522
---

# Scope and evidence

The user requested an investigation of device APIs for faster blitting and
blending. This is a source review and proposal, not a measured speedup or an
approved renderer redesign. Reviewed the local ESP-IDF v5.5.5 sources, Waveshare
BSP 3.0.1, LVGL 9.3 integration, and application rendering paths on 2026-10-09.
Concurrent creature work was present in the checkout; no application source was
changed by this review. No firmware was built, flashed, or profiled on hardware.
Historical shapes timings in [memo-006](memo-006-input-animation-and-frame-performance.md)
are not measurements of the current pet renderer.

# Current path

1. The core draws native-endian RGB565 into an engine framebuffer in PSRAM.
2. `jelli_display_output_present` holds the BSP mutex and copies damaged rows to
   a separate PSRAM LVGL canvas. It then invalidates that area.
3. LVGL renders the canvas into partial draw buffers. The BSP requests two
   PSRAM buffers, each sized for 466 by 50 pixels.
4. The adapter swaps RGB565 bytes before invoking our draw callback.
5. `display_transfer.c` copies the packed strip into a reserved internal DMA
   buffer, then calls `esp_lcd_panel_draw_bitmap`. SPI DMA sends it to CO5300.
   The existing completion callback releases the flush and permits buffer reuse.

Each frame is 434,312 bytes. Two application frames use 868,624 bytes, excluding
128 bytes of guard storage. The two nominal LVGL strips add 93,200 bytes of
pixel storage; our internal DMA strip adds 46,600 bytes. These figures exclude
alignment, SDK descriptors, stacks, and other allocations.

The CO5300 QSPI configuration uses 40 MHz and four data lines. Its ideal payload
rate is 20 MB/s: a full frame takes at least 21.72 ms, or at most about 46 full
frames/s, before command gaps or software overhead. This is a calculated limit,
not observed throughput. The engine's 16 ms target cannot imply 60 full-screen
transfers/s on this configuration. Small damage rectangles remain essential.

# Hardware and API fit

| Mechanism | Fit for this board | Constraints |
| --- | --- | --- |
| ESP32-S3 SIMD instructions | Candidate for RGB565 fill, copy, tint, and blend spans | CPU acceleration, not a GPU; needs target-specific kernels, alignment handling, and scalar tails |
| `esp_async_memcpy_install_gdma_ahb` and `esp_async_memcpy` | Can offload eligible memory copies, including PSRAM on this target | No scaling, masking, RGB565 blend, byte swap, or rectangular-stride API; completion and cache rules apply |
| Existing `esp_lcd_panel_draw_bitmap` | Already uses the panel's SPI DMA transport | Buffers must remain valid until completion; transmission does not perform compositing |
| PPA / DMA2D | Not available on this ESP32-S3 | PPA fill, blend, and scaling APIs target hardware such as ESP32-P4; adapter support does not imply S3 support |

The pinned S3 `soc_caps.h` declares SIMD, AHB GDMA, and async memcpy support,
including AHB access to PSRAM. It does not declare PPA or DMA2D support.
Espressif provides LVGL SIMD sources in `esp-bsp`; the inspected upstream build
logic gates that integration to LVGL 9.1.x. This project uses LVGL 9.3 with
`esp_lvgl_adapter`, and generated configuration selects `LV_USE_DRAW_SW_ASM=0`.
Those kernels are a reference to evaluate, not a confirmed drop-in dependency.
Enabling acceleration inside LVGL would not accelerate our core's own loops.

# Ranked opportunities

## 1. Reduce full redraws and make drawing operate on spans

`pet_render.c` recognizes unchanged and tile-only frames, but most other render
key changes lead to full redraws. Track old/new actor bounds and other changed
regions where feasible, with full redraw fallback for global changes. Existing
particle restoration already demonstrates region drawing. Compare incremental
frames against full redraws to prove restoration and damage correctness.

`jelli_canvas_sprite` expands each opaque source pixel into a rectangle; the
main actor is drawn at scale six. Rectangles call `jelli_canvas_pixel` for every
destination pixel, repeating clipping and the round-screen distance test.
Clip once per scanline and process opaque runs. Fill repeated scaled pixels in
spans and reuse expanded rows within the clipped area. A table of two uint16_t
round-screen bounds per row would cost 1,864 bytes, if used. Preserve black
corners on initial rendering and the existing stride/damage contract.

Background drawing also does coordinate scaling, tint, and circle calculations
per pixel. Precompute bounded coordinate maps or hoist row-invariant work;
consider a cached background only after measuring its extra 434,312-byte cost.
These portable changes reduce work and create useful inputs for SIMD kernels.

## 2. Add optional S3 span kernels after a scalar reference exists

Candidate operations are RGB565 constant fill, opaque copy, constant-alpha
blend, and tint. Keep target code in the ESP32 port, injected through a small
optional renderer interface with a portable scalar fallback. This interface
would be a separate proposed architecture change, not a change approved here.
Avoid callbacks per pixel; dispatch per clipped span or useful block.

The particle blend uses channel-wise integer alpha from 0 through 16, with
truncation after division by 16. A generic LVGL 0-through-255 blend need not be
bit-exact. Preserve current rounding, binary asset masks, nearest-neighbor
scaling, and night/dim order. Fast-path alpha zero and fully opaque spans.

The framebuffer row stride is 932 bytes: it is not a multiple of 16. Aligning
the base alone does not align all rows. Kernels need safe prefix/body/tail
handling and must not read beyond a row, clipped span, or asset buffer. Changing
stride to padded rows would also require changing the ESP32 present contract
and LVGL canvas setup; it is not a local allocation tweak.

## 3. Reduce display copies before adding memory DMA

For full-width damage, engine-to-canvas rows are contiguous and can use one
bulk CPU copy. For narrow damage, keep stride-aware rows. Benchmark this small
change before introducing DMA setup and synchronization.

Potential later work: use startup-reserved internal LVGL draw buffers so the
panel can consume them directly, or fuse packing/byte swap into the existing
internal staging copy through an explicit adapter configuration. These require
BSP/adapter integration and ownership validation. Two 50-row internal buffers
would consume 93,200 bytes, versus our current 46,600-byte internal strip.
Do not remove the reserved strip and rely on implicit driver bounce allocation;
it exists to make panel transfer memory availability predictable.

The current adapter already swaps bytes: swapping again in our callback would
be wrong. Preserve the separate engine/canvas buffers and display mutex unless
an explicit replacement ownership design is approved and tested.

## 4. Treat async memcpy as a conditional experiment

The v5.5.5 implementation in `esp_hw_support/dma/async_memcpy_gdma.c` deletes
previous TX/RX descriptor lists and creates new lists on each submission.
`gdma_new_link_list` allocates heap memory; cache-edge handling can also need a
stash buffer. A startup transaction backlog does not eliminate that churn.
Thus this API is not a direct fit for the project's steady-state allocation
rule. A fixed-descriptor backend would require a separate SDK-coupled design
and a bounded startup resource budget.

The pinned implementation checks address/length alignment and handles source
cache writeback and destination cache edges. Use this actual implementation,
not older examples of deprecated alignment fields, to evaluate a prototype.
932-byte rows and arbitrary damage offsets make per-row DMA awkward. Benchmark
aligned large contiguous spans with CPU prefixes/tails if pursuing this path.

DMA must complete before `present` returns and before releasing the canvas to
LVGL. Likewise, copying into the staging strip must finish before SPI reads it,
and the strip cannot be reused until SPI completion. Waiting immediately may
free CPU time for other tasks without reducing frame latency. Small particle
spans are unlikely to justify submission overhead; measure a crossover size.

# Measurement plan

First add host-side timing around core render, display-lock wait, canvas copy,
LVGL drawing, byte swap/staging, and SPI completion. Use microsecond timing and
bounded counters/histograms; report p50/p95 and transferred bytes by scene.
The current `render` sample also includes other engine-loop work; `present`
includes lock wait and diagnostics, and does not measure subsequent panel
completion. Every five seconds it compares whole frames and checks the heap
under the display mutex. Account for that diagnostic cost separately.

Exercise idle, actor motion, ring transitions, night transitions, and dense
particles. Measure CPU memcpy versus each candidate with actual PSRAM/internal
memory combinations and small, medium, and full-frame regions. Include audio
and Wi-Fi load. The generated configuration currently selects 160 MHz CPU,
32 KB data cache, and 32-byte cache lines; compare 240 MHz and cache choices as
separate experiments with power/internal-memory costs, not assumed speedups.

For an implementation, require scalar-versus-accelerated pixel equivalence,
clipped/unaligned spans, all alpha endpoints, mask edges, padded host strides,
guard integrity, incremental-versus-full redraw checks, and the usual host,
sanitizer, lint, and ESP32 builds. Measure panel completion and physical output
separately from engine update rate. No speedup figure is established yet.

# References

- [Core canvas operations](../../core/pet_canvas.c)
- [Particle blending](../../core/pet_particles.c)
- [Background rendering](../../core/pet_atmosphere.c)
- [Damage selection](../../core/pet_render.c)
- [ESP32 presentation](../../ports/esp32/main/display_output.c)
- [DMA staging](../../ports/esp32/main/display_transfer.c)
- [Frame timing](../../ports/esp32/main/main.c)
- [Surface ownership](../../include/jelli/engine.h)
- [IDF 5.5.5 async copy](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/async_memcpy.html)
- [S3 technical reference manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [Espressif LVGL SIMD build integration](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/CMakeLists_v2.txt)
- [P4 PPA API](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32p4/api-reference/peripherals/ppa.html)
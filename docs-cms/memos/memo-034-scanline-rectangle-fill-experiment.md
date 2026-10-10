---
id: memo-034
title: Scanline rectangle fill experiment
author: Codex
created: 2026-10-10T07:19:38Z
tags: [embedded, performance, rendering, validation]
project_id: jelligotchi
doc_uuid: a0e80c9c-08e8-4868-967f-0cacbba6f860
---

# Outcome

The user requested another technique after the negligible bulk-copy result in
[memo-033](memo-033-full-width-display-copy-experiment.md). This experiment changes
only rectangle filling: clip each row to the round panel once, then write its
contiguous pixels. The measured gain supports keeping this change on the
isolated `perf/esp32-copy` branch. No merge or push was performed. The device now
runs the scanline version with the original production row-copy presentation
path. No SIMD, CPU clock, cache, DMA-copy, or buffer ownership changes were made.

# Implementation

`jelli_canvas_rect` first intersects the rectangle with its existing canvas clip.
For each remaining row, it advances the left edge and retreats the right edge
until both lie inside the round screen. A circle's intersection with a scanline
is contiguous, so interior pixels require no further circle or clipping tests.
Dim color is computed once per rectangle. Writes still honor the surface stride.
No tables, scratch buffers, heap allocations, or platform APIs were added to
core. Disk drawing and background rendering remain unchanged.

Text and scaled sprite pixels already call rectangle filling, so they benefit
without altering the asset format or call sites. Entirely off-circle spans can
still require walking the whole span; no claim is made that every input is faster.

# Benchmark method and correction

`display benchmark-rect` borrows only the LVGL canvas under the display mutex.
It leaves the engine frame intact, resets the canvas from that frame before each
measurement, and restores it before unlocking. Eight pairs are measured per
workload, alternating method order. Checksums of the whole canvas are compared
outside the timed sections. It uses existing PSRAM buffers, not new framebuffers.

The workloads are 1,024 central 6x6 blocks, one 466x466 fill, and 1,024 scattered
6x6 blocks crossing screen and round-mask edges. Five command invocations give
40 workload samples per method. Timings include function dispatch and fills but
exclude buffer reset, checksums, and panel transport. The display task is locked;
these are primitive timings, not whole-game latency or panel FPS.

The initial reference called the original pixel function from another translation
unit. That deprived it of the old renderer's inlining opportunity and overstated
the speedup. The benchmark was corrected to keep the original pixel arithmetic
local to its reference rectangle function, then rebuilt, reflashed, and rerun.
Only corrected results below support the decision. Earlier logs remain under
`build/rect-benchmarks.json` for audit; they must not be used as final evidence.

# Corrected device results

| Workload | Original mean ms | Scanline mean ms | Speedup | Time saved |
| --- | --- | --- | --- | --- |
| Central 6x6 blocks | 9.151 | 4.894 | 1.87x | 46.5% |
| Full-screen fill | 42.796 | 13.966 | 3.06x | 67.4% |
| Scattered edge blocks | 8.518 | 6.562 | 1.30x | 23.0% |

Every response reported matching framebuffer checksums. Subsequent diagnostics
reported equal engine/canvas buffers, intact guards, a valid heap, zero mismatches,
and zero transfer failures (74 transfers at the sample). Engine-task minimum
stack headroom was 996 bytes after the explicit diagnostic. No physical display
or touch observation was supplied, so that verification remains pending.

The board was rediscovered at `/dev/cu.usbmodem2101`, USB 303A:1001, serial
90:70:69:FE:21:DC. Both flashes completed with verified transfer hashes and an RTS
reset; USB debug responded afterward. The corrected application binary SHA256 is
`6bb253628474f09339f7bd1b61893a5e40b7d254990a2172aaaae0f22379d983`. Build version is 0.3.0.
Generated configuration uses performance optimization and a 160 MHz CPU.

Evidence in the isolated worktree: `build/rect-fair-benchmarks.json`,
`rect-fair-display-after.json`, `rect-fair-esp.log`, and `rect-fair-flash.log`.
The baseline copy experiment remains rejected; this result does not change it.

# Validation

- Desktop tests: 50/50 passed, including rectangle equivalence and SDL smoke/demo.
- Core-only tests: 24/24 passed.
- ASan/UBSan tests: 50/50 passed.
- The rectangle equivalence test passed again in desktop and sanitizer builds
  after correcting the reference inlining setup.
- Tests compare entire buffers against the original pixel predicate for central,
  full-screen, scattered, and clipped regions, with both dim states. They include
  empty rectangles, individual pixels, padded stride, and surrounding guards.
- Formatting, clang-tidy, Cppcheck, and size checks passed before calibration;
  Cppcheck and size checks were repeated after the reference-helper correction.
- ESP32 builds passed before both flashes. The SDL frame-650 demo snapshot was
  inspected for text, sprite, clipping, and round-panel rendering.

The benchmark is retained for repeatable measurements. Broader render-stage and
frame-latency measurements are needed to quantify the whole-game improvement.

# Follow-up device confirmation

On 2026-10-10 the user requested further device confirmation. Rediscovery found
the same USB device. No firmware change or reflash was needed. The running device
responded to the retained rectangle benchmark; firmware provenance remains the
verified flash recorded above, since the debug state version field identifies
the wire protocol rather than the application hash.

Captured and visually inspected device engine-framebuffer screenshots of home,
main menu, and care menu. Their PNG checksums, 466x466 geometry, and nonblank pixel
checks passed. Five subsequent menu/care/back/close cycles passed via USB, using
both label presses and coordinate taps derived from the device's own buttons.
Transitions settled and the device returned home with capture released. These
exercise UI hit testing, not the physical touch sensor. No care action or cheat
was applied; ordinary game time continued to advance.

Three more A/B runs (24 workload samples per method) reproduced the result:

- central blocks: 9.156 ms original, 4.895 ms scanline (1.87x).
- full fill: 42.793 ms original, 13.962 ms scanline (3.06x).
- edge blocks: 8.524 ms original, 6.567 ms scanline (1.30x).

All framebuffer checksums matched. Display diagnostics progressed from 1,644 to
2,620 transfers during the confirmation session. Both samples reported zero
mismatches and transfer failures, equal engine/canvas buffers, intact guards, and
a valid heap. The ending sample reported 996 bytes minimum engine-task stack
headroom. Screenshots read the engine framebuffer, not panel RAM, so they cannot
prove physical scanout. Physical display and touch confirmation was requested
from the user and had not arrived when this entry was written.

Evidence remains in ignored `build/confirm-*` files in the isolated worktree:
`home.png`, `menu.png`, `care.png`, `navigation.json`, `benchmarks.json`, and
`display-before.json` / `display-after.json` (each with the `confirm-` prefix).
Documentation check and repair were run after recording these findings.

# References

- [Rectangle fill](../../core/pet_canvas.c)
- [Original reference and workloads](../../tools/bench/rect_workload.h)
- [Exact buffer comparison](../../tests/test_canvas_rect.c)
- [ESP32 benchmark](../../ports/esp32/main/render_benchmark.c)
- [Diagnostic commands](../../docs/playing.md#debug-cli)

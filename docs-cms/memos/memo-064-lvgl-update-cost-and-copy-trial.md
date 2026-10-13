---
title: LVGL update cost and copy trial
author: Codex
created: 2026-10-11
tags: [embedded, performance, rendering, validation]
id: memo-064
project_id: jelligotchi
doc_uuid: 063c3d3d-182c-4ceb-9c74-b1a339aadefe
---

# Scope

The user requested continuation through hardware optimizations. Following the
[rectangle SIMD trial](memo-063-clipped-rectangle-simd-device-trial.md), this
increment decomposes the measured LVGL update interval and compares its built-in
copy function with libc on the real display pipeline. The candidate remains
power-profile-only; normal rendering and product policies are unchanged.

# Implementation and method

The [profiler](../../ports/esp32/main/pipeline_profile.c) uses the pinned LVGL
render, flush and wait events. Flush and wait intervals are disjoint in the
current partial-render path. Their totals plus the residual `lvgl_work` must
exactly equal the `lvgl` render-start to render-ready total. Work includes drawing,
dispatch, bookkeeping and interrupts; it is not pure CPU time. SPI completion
and visible panel scanout remain different boundaries, and final DMA completion
can occur after render-ready.

Power-only GNU linker wrappers in
[lvgl_copy_trial.c](../../ports/esp32/main/lvgl_copy_trial.c) time the RGB565 image
and solid-color drawing functions. Both are subsets of residual work, not extra
intervals to sum with it. A third wrapper selects the original `lv_memcpy` or
libc `memcpy` atomically. It intercepts external references; same-translation-unit
calls are not guaranteed to pass through GNU wrapping. The RGB565 object file
retains a relocation to `lv_memcpy`, and the final map includes the wrappers.
No managed dependency source is edited for this comparison.

Before accepting either mode, bounded internal-RAM checks compare both functions
with a byte predicate and sentinel guards, covering all source/destination offsets
modulo four and twelve lengths, including zero and tails. These 384 calls verify
bytes, guards and returned pointers. They are not an exhaustive PSRAM or panel
oracle. Existing canvas/transfer diagnostics remain separate evidence.

Timing arrays plus report scratch occupy 8,040 bytes, below the source's 8 KiB
assertion; copy qualification adds 640 bytes of static scratch. Event state and an
atomic mode flag add bounded scalar storage. There is no new framebuffer, task or
runtime allocation for these probes. Normal and factory profile isolation excludes
the copy trial and profiler hooks.

The [station](../../tools/debug/pipeline_experiment.py) records identity, state,
all command replies and raw serial. It rejects stale/missing timing data,
incomplete interval accounting and display health regressions. A finally block
stops capture and attempts restoration to the built-in copy path even after a
failed capture. Failed serial cleanup requires explicit normal-image recovery.
`--refresh-ms 500` requests a full canvas invalidation every half second while
normal animation continues; this is a mixed-update workload, not a deterministic
single-frame benchmark. `--memcpy` selects the copy mode. Both modes carry the
same profiling overhead. Percentiles use the last 128 samples per stage; totals
cover the entire ten-second capture. Stage percentiles must not be added.

# Device evidence

USB rediscovery found SKU31261 ESP32-S3 at `/dev/cu.usbmodem2101`, serial
90:70:69:FE:21:DC. This follows a normal-profile identity/display check at the start
of the investigation. Builds used the existing pinned IDF/LVGL configuration,
160 MHz CPU and PSRAM-backed display buffers. Evidence is retained under ignored
`build/lvgl-cost-001`, with source snapshots, binary/configuration hashes, build
and flash logs, raw serial, per-run summaries and `comparison.json`.

Initial natural and forced-full captures established exact interval accounting:
LVGL wait p95 was only 19–22 us, while full-update residual work p95 was about
52 ms. The first image/copy-wrapper build failed because the public LVGL header
only forward-declares the draw descriptor. Including the pinned private blend
header supplied its definition; the rebuilt power image passed. No failed image
was deployed. This is a version-sensitive test seam, not a portable engine API.

An initial copy pair without solid-fill timing showed little benefit. The final
instrumented image then ran six captures in built-in/libc/libc/built-in/built-in/libc
order, restoring built-in after every capture. All mode qualifications, interval
accounting and buffer/guard/heap checks passed, with zero mismatches or transfer
failures. Median p95 across the three captures per mode:

| Interval | Built-in us | libc us |
| --- | --- | --- |
| LVGL update | 82799 | 82374 |
| Residual work per update | 52704 | 52285 |
| Flush callback per strip | 3260 | 3258 |
| Wait per strip | 23 | 21 |
| RGB565 image draw per call | 2546 | 2524 |
| RGB565 solid fill per call | 1237 | 1249 |

The update p95 reduction is only 425 us, approximately 0.51%. Median built-in
full-run time shares were 65.51% residual work, 34.22% flush callbacks and 0.27%
waits. Within the total LVGL interval, image draws accounted for 24.83% and solid
fills 11.61%; these are already included in work. The remaining work is not yet
attributed to a specific operation. Median shares come from individual runs and
need not sum exactly after independent aggregation. Each capture's raw disjoint
totals do sum exactly.

# Decision and limits

Do not promote a global libc-copy replacement on this evidence. It provides a
small timing change, not a demonstrated material whole-scene or visible-response
improvement. Transfer waiting is not the dominant cost in this workload. Further
experiments should target the residual draw/dispatch work and memory traffic,
with matched controls and exact output checks before changing buffer ownership
or presentation. Solid fills consume measurable time, but this trial does not
establish that any particular fill is redundant or safe to remove.

Main-task minimum stack reserve reached 800 bytes during the final captures;
this remains a separate margin concern. No new physical display/touch observation
was obtained for this increment, and no external current meter was connected.
Existing diagnostics do not verify the LVGL draw-buffer output pixel-for-pixel
or prove physical panel correctness. No energy saving is claimed.

# Validation and restoration

Normal and power builds passed, as did normal/power/factory isolation checks.
Tracked C formatting, clang-tidy, Cppcheck and size checks passed. The new copy
trial received explicit formatting, Cppcheck and size checks; a const-pointer
style finding was corrected before the final measured image. Four station tests
passed, including rejection of inconsistent interval totals. No portable engine
code changed, so core/sanitizer suites were not rerun.

Normal firmware was restored with verified flash hashes. The restored board
reported profile `game`, 11 display checks, intact buffers/guards/heap, zero
mismatches/transfer failures and 1,292 bytes minimum main-task stack reserve.
Clock trust remains unknown. Serial is closed. This is runtime diagnostic
evidence, not a new physical display/touch acceptance.

Documentation check/repair passed for 85 records without repairs, and
`git diff --check` passed. Progress and completion updates were sent to the
user-requested ntfy topic. No commit, merge or push was made.

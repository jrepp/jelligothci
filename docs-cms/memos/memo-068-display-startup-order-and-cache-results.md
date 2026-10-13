---
title: Display startup ordering and cache performance results
author: Codex
created: 2026-10-11
tags: [embedded, performance, rendering, validation]
id: memo-068
project_id: jelligotchi
doc_uuid: 068d8bcb-6027-49bb-9cea-15645cd208a4
---

# Scope

The user requested root-causing the repeated baseline allocation failure in
[memo-067](memo-067-cache-configuration-trial.md) and continuing rendering
optimization. This increment fixes startup ordering, requalifies the baseline
on hardware and evaluates cache configurations. Cache improvements remain
experimental until their output and physical behavior are qualified.

# Root cause and remedy

The pinned Waveshare BSP calls `esp_lv_adapter_start` before returning its
display. That creates a worker which can immediately refresh the default
screen. The application previously installed its custom DMA staging callback
only after that return. The adapter's default draw path could consequently
pass a PSRAM draw buffer directly to the SPI panel driver. This BSP does not
enable direct PSRAM DMA. In the pinned IDF's `setup_dma_priv_buffer`, an external
pointer without that flag requires an additional internal DMA allocation.
That allocation failed in both rejected baseline boots, even though the
application had already reserved its own staging strip.

[Display transfer startup](../../ports/esp32/main/display_transfer.c) now
intercepts only the BSP's initial worker-start request during
`jelli_display_prepare`. It verifies that the request occurred, returns the
prepared display, and lets [app_main](../../ports/esp32/main/main.c) attach the
staging callback and construct/invalidate the canvas before explicitly starting
the worker after releasing the display mutex. Later start calls reach the real
adapter implementation. The flags belong exclusively to the startup main task;
they are not a cross-task synchronization mechanism. The factory profile does
not use this seam. Managed vendor source is unchanged.

This adds two static booleans, no new framebuffer, DMA buffer, task, queue or
steady-state allocation. The worker, buffer lifetime and transfer-completion
ownership remain as before. The existing startup transfer-ready wait still
requires a successful custom-path submission. The
[profile checker](../../tools/debug/check_firmware_profile.py) checks that the
startup wrapper is linked in game/power images and absent from factory.

Three consecutive baseline boots after the fix passed startup and capture
checks without SPI allocation failures. Logs place the explicit deferred-start
message before worker creation. Their instrumented full-update p95 values
were 84387, 84474 and 84227 us. Together with the two pre-fix failures and the
source path, this supports the startup-order diagnosis; it is not an exhaustive
proof against every possible future allocation failure.

# Experiment method

All images share the startup fix, original buffers, CPU clock, task settings,
copy path, overlay traversal and fine event probes. Each capture follows an
explicit board reset, verified ELF identity, successful PSRAM/panel/touch/app
startup, ten seconds of warmup and preflight display/memory checks. The existing
[pipeline station](../../tools/debug/pipeline_experiment.py) captures ten seconds
with full invalidation every 500 ms, then verifies timing accounting, buffer
equality, guards, heap integrity and mode restoration. Percentiles are computed
over the station's bounded sample window; per-stage percentiles are not additive.

An additional [instruction-cache plus line-length profile](../../ports/esp32/profiles/cache/icache-line64.defaults)
combines the independently promising changes without enlarging the data cache.
It reserves 16384 additional cache bytes relative to baseline, versus 49152 for
the all-settings candidate. The [matrix checker](../../tools/debug/check_cache_trial.py)
includes it and continues to reject unrelated configuration differences.

The capture helper uses the installed SDK monitor's reset implementation with
a directly owned serial connection and bounded reads, avoiding the previous
PTY helper's teardown problem. Serial closes in `finally`. The helper and raw
evidence are retained under ignored `build/cache-device-002`; it is not a new
production station interface.

# Results and recovery

The primary comparison uses three captures per configuration, with controls
interleaved and every capture passing the new network gate. All use the home
scene with the same pet, form and location. Medians are of per-capture p95
values, not pooled per-frame percentiles:

| Configuration | LVGL update p95 | Reduction | LVGL work p95 | Free internal 8-bit heap |
| --- | --- | --- | --- | --- |
| Baseline | 84.484 ms | Reference | 54.451 ms | 39379 bytes |
| Longer data-cache lines | 68.219 ms | 19.25% | 45.096 ms | 39367–39503 bytes |
| Larger instruction cache plus longer lines | 53.606 ms | 36.55% | 32.256 ms | 23055–23187 bytes |

The longer-line candidate's individual update p95 values were 68193, 68293 and
68219 us. The instruction-cache/line combination measured 53606, 53488 and
55146 us. The corresponding baseline controls measured 84379, 84484 and
84611 us. `comparison.json` records the exact run folders and checks common
scene, workload, mode restoration, network state and display health before
producing the comparison. No stage percentiles are summed.

Exploratory single captures measured 71165 us for instruction-cache capacity
alone and 66346 us for data-cache capacity alone. The latter retained only
7487 bytes of internal 8-bit heap, versus roughly 39 KiB in baseline, so its
small apparent advantage over longer lines does not justify that memory cost
on this evidence. These single captures are not repeated-treatment estimates.
The line-length-only setting adds no cache capacity and is the more conservative
candidate; the instruction-cache/line combination is the fastest qualified
configuration in this trial, at a cost of 16 KiB of heap capacity.

All primary runs reported equal engine/canvas buffers, intact guards/heap and
zero mismatches or transfer failures. Their smallest reported main-task stack
headroom was 432 bytes. This value comes from `check_buffers` called by the
engine's presentation path, not the LVGL worker's stack. These heavily
instrumented captures therefore warrant stack review before adding deeper
probes or promoting a configuration. Final LVGL draw-buffer/panel equivalence,
physical display/touch behavior, connected-network memory margin and a reduced-
instrumentation comparison remain pending. These results are full-update
latencies, not whole-game FPS or battery-life measurements.

The all-settings candidate was rejected despite a completed display capture:
Wi-Fi reported `malloc buffer fail`, allocated only four of ten expected RX
buffers, and returned network result 257 (`ESP_ERR_NO_MEM`). Its rendering
measurement therefore used a different resource state and is not accepted as
an optimization result. The raw capture is explicitly marked rejected and
retained for diagnosis. The baseline was restored and passed startup/capture
checks before proceeding.

This exposed a station acceptance gap: display integrity alone does not catch
another subsystem failing initialization. The pipeline station now requires an
available, non-pending network state with a successful result before and after
capture, and rejects network enable/connection/operation changes within the
window. Its regression test rejects the observed allocation error. The ad hoc
startup helper also checks Wi-Fi readiness and allocation errors. Successful
initialization with networking disabled does not establish margin for connected
traffic; that remains a separate qualification requirement.

# Validation and final state

All six cache configurations, normal, power and factory images built through
the pinned wrapper. Configuration comparisons and profile isolation passed.
The C formatting, Clang-Tidy, Cppcheck and size checks passed. The cache,
pipeline and factory capture suites passed 24 tests in total; the pipeline
suite includes the new failed-network regression. Shell syntax and whitespace
checks also passed. Core/game behavior was not changed.

The final normal image includes the startup ordering fix, with the original
cache configuration. Flash hashes and the running ELF identity (`ed3d082ff`
prefix) matched. Startup reported version 0.8.0, the deliberate USB reset,
successful PSRAM test, panel/touch initialization, deferred worker startup and
application readiness, without SPI or Wi-Fi allocation failures. Network
initialization reported success. Ten final display-health requests passed with
zero mismatches/transfer failures, intact equality/guards/heap and 756 bytes of
reported main-task stack headroom. The serial connection was closed.

Documentation validation and repair passed without repairs or unresolved
findings. Physical display/touch confirmation and current measurements are not
available. The optimization profiles remain opt-in experiments. Build
identities, source snapshots, startup/flash logs, raw replies, rejected evidence
and comparisons are under ignored `build/cache-device-002`.

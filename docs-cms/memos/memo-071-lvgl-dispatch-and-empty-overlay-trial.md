---
title: LVGL dispatch and empty overlay trial
author: Codex
created: 2026-10-11
tags: [embedded, performance, rendering, validation]
id: memo-071
project_id: jelligotchi
doc_uuid: 9cea4871-db3b-4926-b947-987ad0eeb9bd
---

# Scope

The user requested continued hardware work after the
[memory-placement trial](memo-065-back-buffer-memory-placement-trial.md).
This increment attributes the remaining LVGL work on the original buffer
configuration and tests skipping traversal of empty overlay layers. All new
hooks and the mode switch are power-profile-only. Production rendering, clocks,
task affinity and buffer ownership remain unchanged.

# Instrumentation and resource contract

The pinned configuration uses `LV_OS_NONE` and one synchronous software draw
unit. There is no separate LVGL draw-worker thread to tune. New wrappers in
[lvgl_dispatch_profile.c](../../ports/esp32/main/lvgl_dispatch_profile.c) measure:

- Task finalization, including synchronous dispatch and drawing.
- External dispatch calls, including cleanup and any work they perform.
- Draw-dispatch waits, distinct from panel flush waits.
- Outermost object cover/draw events, including their synchronous drawing work.

GNU wrapping does not intercept same-object dispatch calls made inside task
finalization; those calls are included in the finalization interval. A nesting
counter prevents counting a wrapped dispatch twice if reached through another
wrapped interval. A separate event nesting counter measures enclosing object
events. Both counters rely on the current synchronous LVGL task/display-lock
contract, enforced by compile-time assertions on the draw configuration.

Task/dispatch/draw-wait totals are non-overlapping subsets of `lvgl_work` for
this path. Object event time includes task/drawing time and must not be added
to it. Image and solid-fill intervals are nested inside this work as well.
The station rejects missing task/event instrumentation, subgroup totals larger
than their containing interval, stale identities and incomplete flush/work/wait
accounting. Wall times include interrupts and probe overhead, not just CPU
instructions; independent stage percentiles must not be added.

Timing arrays plus report scratch now occupy 10184 bytes, bounded by a 10 KiB
assertion. Compared with memo-065 this adds 2144 bytes for four stages, plus
small nesting/mode state. No framebuffer, task, queue or DMA ownership change
is introduced. Explicit mode changes use LVGL local-style allocation for at most
two properties; the new probes do not allocate in the frame loop. A linker undefined-symbol root ensures the
wrapper-only archive member is retained; the first build failed before that
root was added. A later guard build rejected a const pointer passed to LVGL's
non-const event-count API; its helper signature was corrected. Neither failed
image was flashed. The managed LVGL source remains untouched.

# Attribution captures

The initial dispatch-only capture measured approximately 42.84% of total LVGL
time in task finalization/drawing, 0.69% in external dispatch and 0.08% in draw
waits. Draw-wait p95 was 6 us. Scheduling a nonexistent worker or optimizing
that wait is not supported by these results.

Three further ten-second captures included object-event timing and requested a
full canvas invalidation every 500 ms while the normal pet animated. Median
full-run time shares were:

| Interval | Share of LVGL total |
| --- | --- |
| Residual work | 66.50% |
| Flush callbacks | 33.25% |
| Panel flush waits | 0.26% |
| Object cover/draw events, within work | 57.77% |
| Task finalization/drawing, nested in events | 41.55% |
| External dispatch | 0.55% |
| Draw-dispatch waits | 0.03% |

These are independently aggregated per-run shares, so rounded medians need not
sum exactly. Each capture's original disjoint totals account for elapsed time
exactly. Events numbered 26 per strip; there were two draw tasks per strip.
An additional natural-animation capture passed the same accounting checks.

The finer event hooks increased forced-full-update p95 from 82849 us in the
initial dispatch capture to a three-capture median of 84578 us, about 2.1%.
This is observable profiling perturbation, not an application regression or
optimization gain. The candidate comparison below therefore uses identical
instrumentation in both modes and records restored controls.

# Guarded empty-layer candidate and recovery

The first candidate used `LV_OBJ_FLAG_HIDDEN` on empty top/system root layers.
Its timing capture completed, but restoration crashed: pinned LVGL's
`lv_obj_remove_flag` calls `lv_obj_mark_layout_as_dirty` with the root's null
parent. The station rejected cleanup, attempted the remaining copy restoration,
and stopped the sequence. That capture is not an accepted performance result.
Normal firmware was restored and passed runtime buffer/guard/heap checks before
continuing. The reboot also logged an early SPI bounce-buffer `ESP_ERR_NO_MEM`
before application display callbacks were ready; later runtime health passed.
This startup allocation uncertainty remains separate from the diagnosed panic.

The replacement in
[lvgl_dispatch_profile.c](../../ports/esp32/main/lvgl_dispatch_profile.c) uses
`LV_STYLE_OPA_LAYERED=LV_OPA_TRANSP`, which makes pinned `lv_refr.c` return before
traversing the layer. `display profile overlays skip` requires zero children,
zero custom event callbacks, transparent backgrounds, no background image and
no border, shadow or outline. It saves each original local property value and
presence. `display profile overlays baseline` restores them and checks both.
No managed SDK source or hidden flags are changed. Changes require the display
mutex and inactive profiler; concurrent power trials/captures are excluded.

Three explicit skip/restore cycles passed before timing. Six ten-second captures
then used baseline/skip/skip/baseline/baseline/skip, with full invalidation every
500 ms, original draw buffers and built-in copy. All retained the same boot
identity, restored both modes, and passed buffer equality, guards and heap checks,
with zero mismatches or transfer failures.

| Median of three captures | Baseline | Skip empty overlays |
| --- | --- | --- |
| LVGL update p95 | 85331 us | 82352 us |
| Residual work p95 | 55174 us | 52275 us |
| Cover/draw events per strip | 26 | 14 |

The instrumented update reduction is 3.49%. Fewer events also execute fewer
profiling hooks, so this is not an established production gain. Task count stayed
two per strip. The observed stack low-water mark reached 864 bytes; further
nested probes need a stack review. No current measurement was made.

This is an entry-time guard, not a policy for future dialogs, cursors or widgets.
The candidate remains power-only. Promotion requires a comparison without the
fine event probes, final LVGL draw-buffer equivalence, physical display/touch
confirmation and an ownership policy if overlays acquire content.

The [station](../../tools/debug/pipeline_experiment.py) accepts
`--overlays baseline|skip`, defaulting to baseline. Cleanup attempts stop,
overlay restoration and built-in-copy restoration independently; any failure
rejects the capture and requires normal-image recovery. Tests cover lost replies,
rejected restoration and incomplete/nested timing accounting.

# Validation and artifacts

Normal and power firmware built successfully. C formatting, static analysis and
size checks passed, including the new wrapper source. The 24 station experiment
tests and normal/power/factory profile-isolation checks passed. Normal firmware
was restored after the final trial. Startup reported version 0.8.0, the expected
monitor-initiated USB reset, successful PSRAM test, panel creation, touch
registration and app readiness, with no SPI allocation error in this final boot.
The normal image passed ten display checks with zero mismatches/transfer failures,
intact guards/heap and 1292 bytes of reported stack headroom. Serial was closed. No new physical display/touch confirmation is available for this increment.

Ignored evidence is under `build/lvgl-dispatch-001`: `events-*` attribution,
`overlay-02-hidden` rejected capture, `overlay-crash-decoded.txt`, recovery logs,
`opacity-cleanup.json`, six `opacity-*-*/summary.json` captures and
`opacity-comparison.json`. Build/flash logs, image hashes and source snapshots
retain the corresponding configurations. These are dated measurements of this
board and scene, not universal latency guarantees.

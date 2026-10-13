---
title: SIMD fill and packed blend device benchmarks
author: Codex
created: 2026-10-11
tags: [embedded, performance, rendering, validation]
id: memo-062
project_id: jelligotchi
doc_uuid: 99d06031-f4fc-4ae4-b6eb-7d51afb15162
---

# Scope and implementation

The user requested device performance tests for SIMD graphics fill and blend
improvements while continuing the remaining hardware optimizations. This bounded
increment adds diagnostic candidates and a repeatable station benchmark. No
candidate replaces production renderer code. The existing
[scanline fill](../../core/pet_canvas.c) and
[particle alpha arithmetic](../../core/pet_particles.c) supply the scalar models.

[span_kernels.c](../../ports/esp32/main/span_kernels.c) compares scalar RGB565 fill
with an ESP32-S3 PIE store loop. A scalar prefix aligns the destination, the
assembly writes complete eight-pixel blocks, and a scalar tail stays inside the
span. The refined candidate retains scalar filling for short spans. The
[assembly](../../ports/esp32/main/span_fill_s3.S) touches only caller-clobbered q0
and changes no shift/accumulator state. The pinned SDK's Xtensa `tie-asm.h`
provides the target's custom-register context save/restore definitions.

The blend candidate packs blue, red and green into separated unsigned lanes,
performs the same integer alpha calculation and truncation, and unpacks RGB565.
It uses ordinary integer instructions, not PIE SIMD. Lane spacing prevents
cross-channel carry for the supported alpha contract. The refined candidate
also returns immediately for transparent spans and uses the fill path for opaque
spans. Current production effects use masked and sometimes varying source
pixels, so a constant-color span benchmark does not establish their total gain.

# Method and resource budget

The power-only [benchmark](../../ports/esp32/main/span_benchmark.c) borrows the
existing PSRAM canvas under the display mutex and restores it from the untouched
engine frame before unlocking. Its fixed internal scratch, timing arrays and
report total 2,876 bytes, bounded by a compile-time assertion. No new framebuffer,
queue or task is added. The SIMD wrapper has a small aligned stack pattern;
assembly and wrapper stack frames are visible in retained disassembly.

Correctness runs compare every pixel, including surrounding guards, against the
scalar calculation. They cover empty spans, vector boundaries/tails, all eight
16-bit alignment offsets, every supported integer alpha, selected foreground
colors and a deterministic varying background. Both internal RAM and PSRAM are
checked, including long spans. Clipping remains the caller's responsibility;
this does not validate a new clipped rectangle implementation.

Each timed workload uses eight A/B pairs with alternating order and one pair per
alignment offset. Each method repeats sixteen calls per pair. Buffer initialization
and correctness comparisons are outside timing. The station gathers five runs;
tables use median run totals divided by the calls per method. Timer quantization,
interrupts and cache state limit interpretation of sub-microsecond differences.
The reset/warm-repeat method is explicit: cached row timings are not streaming
framebuffer throughput. The target runs at the existing 160 MHz and performance
compiler optimization; no CPU/cache/PSRAM settings were changed.

Disassembly confirms a scalar store loop for the reference, inline per-component
blend arithmetic inside its loop, and PIE vector stores in the candidate. This
avoids the out-of-line scalar-reference bias previously found in
[memo-034](memo-034-scanline-rectangle-fill-experiment.md).

# Initial on-device findings

On the rediscovered SKU31261, USB serial 90:70:69:FE:21:DC, all five initial runs
passed exact pixel and guard checks. Representative PSRAM results:

| Kernel / pixels | Scalar us/call | Candidate us/call | Speedup |
| --- | --- | --- | --- |
| Fill / 6 | 0.281 | 0.578 | 0.49x |
| Fill / 36 | 0.672 | 0.867 | 0.77x |
| Fill / 466 | 6.109 | 1.156 | 5.28x |
| Fill / 23300 | 1443.125 | 1443.242 | 1.00x |
| Blend alpha 7 / 466 | 61.070 | 42.703 | 1.43x |
| Blend alpha 7 / 23300 | 3591.469 | 2679.383 | 1.34x |

Internal-RAM row fill measured 5.39x and row blending 1.43x. SIMD setup regressed
small fills, while the large PSRAM fill showed no gain, consistent with memory
throughput dominating that workload. Those findings motivated a scalar short-span
fallback and transparent/opaque blend shortcuts in the refined diagnostic.

Initial post-run display diagnostics reported intact buffers/guards/heap, zero
mismatches/transfer failures and 752 bytes of minimum main-task stack reserve.
Evidence is in ignored `build/graphics-spans-001/measured`, with source snapshots,
image/configuration hashes, flash/build logs and disassembly alongside it.

# Refined candidate results

Five further device runs passed exact pixel and guard checks after adding the
short-fill scalar fallback and blend alpha endpoint fast paths. PSRAM medians:

| Kernel / pixels | Scalar us/call | Refined us/call | Speedup |
| --- | --- | --- | --- |
| Fill / 6 | 0.305 | 0.328 | 0.93x |
| Fill / 36 | 0.664 | 0.695 | 0.96x |
| Fill / 466 | 6.102 | 1.227 | 4.97x |
| Fill / 23300 | 1443.258 | 1444.836 | 1.00x |
| Blend alpha 7 / 6 | 1.016 | 0.820 | 1.24x |
| Blend alpha 7 / 36 | 5.070 | 3.445 | 1.47x |
| Blend alpha 7 / 466 | 61.172 | 43.055 | 1.42x |
| Blend alpha 7 / 23300 | 3591.297 | 2679.484 | 1.34x |
| Blend alpha 0 / 466 | 61.070 | 0.180 | No pixel work needed |
| Blend alpha 16 / 466 | 61.086 | 1.273 | 47.97x |

Internal row fill measured 5.45x and general blend 1.43x. The refined short-fill
path remains approximately 2–7% slower across the tested memory/size combinations;
its absolute difference is tens of nanoseconds per call, so it is not a tuned
production dispatch policy. Endpoint improvements reflect skipped unnecessary
work, not equivalent gains for arbitrary alpha. The opaque constant-color case
can use fill; varying-source sprites would require a copy/mask-aware path.

Post-refinement diagnostics reported 14 checks, zero mismatches and transfer
failures, intact buffers/guards/heap and 752 bytes minimum main-task stack reserve.
The original and refined evidence/source/hash snapshots are kept separately in
`build/graphics-spans-001`. No candidate has been promoted: long PSRAM fill remains
throughput-limited, tiny span dispatch still has overhead, and clipped/masked
scene integration has not yet established end-to-end benefit.

# Validation and limitations

All three profiles built and passed isolation. Formatting, tracked C analysis
and size checks passed; new untracked C files receive explicit formatting,
Cppcheck and size validation. Initial Cppcheck printf type warnings were fixed
before collecting timings. A later duplicate-condition style finding in the
refinement was fixed before its deployment. Station tests reject failed
correctness, incomplete arrays and invalid timing/alpha metadata. No portable
core code changed, so host core/sanitizer suites were not rerun.

Physical panel/touch observations and whole-scene speedup are separate from
these raw-span timings. Promotion requires realistic clipped/masked workload
integration and end-to-end render-stage evidence, including short-span regressions
and production stack margin. No automatic sleep or other hardware policy changed.

# Render baseline after the span experiments

A separate ten-second home-scene profile captured the unchanged production
renderer after the diagnostic completed. It recorded 331 render calls:
p50 528 us, p95 107,467 us, maximum 110,393 us. The 26 LVGL render events had
p50 80,387 us and p95 80,658 us; copy p95 was 21,302 us. Render percentile
windows and total counters follow the existing profiler contract. The scene
includes occasional expensive updates, so this sample is not comparable to a
different idle scene without controlling that workload. LVGL elapsed time can
include transfer waits and submission; do not sum stages or call it pure CPU time.

This baseline is retained under `pipeline-baseline`; no candidate was installed
in the live renderer, so it establishes no whole-scene improvement. Diagnostics
after the profile remained healthy. Production integration should measure these
expensive updates and total presentation as well as cached primitive throughput.

# References

- [Station runner](../../tools/debug/span_experiment.py)
- [Station evidence checks](../../tests/test_span_experiment.py)
- [Espressif PIE instruction reference](https://github.com/espressif/esp-dl/blob/master/tools/agents/skills/esp32s3-pie-simd/references/pie_instruction_set.md)

# Session completion

Normal firmware was restored with verified transfer hashes. Debug reported
profile `game`, four display checks, equal buffers, intact guards/heap,
zero mismatches/transfer failures and 1,404 bytes minimum main-task stack reserve.
Clock trust remains unknown and serial is closed. No new physical display/touch
observation was supplied for this graphics session. Final profile isolation,
new-source formatting/Cppcheck/size checks, station validation tests and
`git diff --check` passed. Documentation check/repair passed for 83 records
without repairs. At the user's explicit request, progress and completion updates
were posted to their ntfy topic. No code was committed, merged or pushed.

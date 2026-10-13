---
title: Clipped rectangle SIMD device trial
author: Codex
created: 2026-10-11
tags: [embedded, performance, rendering, validation]
id: memo-063
project_id: jelligotchi
doc_uuid: 6eb91557-15e2-43fe-b0ce-b58dba04bc15
---

# Scope

Following the [raw-span measurements](memo-062-simd-fill-and-packed-blend-device-benchmarks.md),
the user requested continuation. This increment tests the fill candidate inside
actual rectangle clipping and round-panel row traversal. It remains power-profile
code; no candidate is installed in the production renderer.

# Implementation and method

[span_rect.c](../../ports/esp32/main/span_rect.c) preserves the current
[canvas rectangle](../../core/pet_canvas.c) clipping and dim arithmetic. Tiny rows
use an inline scalar loop; long rows call the measured span kernel. The exact
single-rectangle oracle compares the entire borrowed canvas against the untouched
engine source and an independent pixel predicate. Cases include empty, broad,
unaligned, off-screen and clipped rectangles in both dim states. Combined workload
runs compare whole-canvas checksums. Exact oracle checks and checksum checks are
reported distinctly; neither establishes physical panel scanout.

The retained [rectangle workloads](../../tools/bench/rect_workload.h) exercise
central sprite-like blocks, a full-screen fill and scattered edge blocks. The
reference is the current scanline renderer, not its slower historical per-pixel
predecessor. Each workload alternates eight method pairs. Buffer reset and output
checks are outside timing. The existing canvas is borrowed under the display
mutex and restored before unlocking, with no extra framebuffer or task.

The matched-control refinement uses one rectangle function with an engine-task-only
long-span function pointer. The SIMD and scalar controls execute identical rectangle
code; only the long-span call target changes. Both controls are separately paired
against the current renderer. The station alternates their command order across
five runs and retains all replies. This control distinguishes vector-store effects
from changes to clipping code generation, inlining, dispatch and layout. It is still
a PSRAM rectangle benchmark, not a complete animated-scene or presentation test.
The selector adds one target-sized pointer of persistent RAM; case tables are
read-only, and the existing benchmark's bounded stack buffers remain in use.

# Initial comparison

Five initial runs passed the exact clipping/dim oracle and all workload checksums.
Median per-workload times on the 160 MHz SKU31261 were:

| Workload | Current us | Candidate us | Speedup |
| --- | --- | --- | --- |
| Central blocks | 4884.375 | 4343.875 | 1.124x |
| Full-screen | 13944.375 | 13708.250 | 1.017x |
| Edge blocks | 6563.000 | 6513.750 | 1.008x |

Central blocks never enter the SIMD branch, so their measured gain cannot be
credited to SIMD. This observation motivated the matched scalar control before
any promotion decision. Post-test diagnostics showed intact buffers/guards/heap,
zero mismatches/transfer failures and 848 bytes minimum main-task stack reserve.

Evidence is under ignored `build/graphics-rect-001`, including original source
patch/new files, image/config hashes and separate logs for each candidate image.
Rediscovery identified USB serial 90:70:69:FE:21:DC at `/dev/cu.usbmodem2101`;
the intended starting firmware was verified as normal `game` on SKU31261.
Power-image flashes verified transfer hashes. No complete early-boot transcript
was captured; capability and runtime checks do not replace that evidence.

# Matched-control result and decision

Five further rounds ran both controls, alternating command order. Every exact
clipping/dim oracle and workload checksum comparison passed. Median times:

| Workload | Current us | SIMD candidate us | Scalar control us | Control / SIMD |
| --- | --- | --- | --- | --- |
| Central blocks | 4888.750 | 4381.375 | 4382.625 | 1.0003x |
| Full-screen | 13946.625 | 13714.500 | 13719.625 | 1.0004x |
| Edge blocks | 6533.625 | 6505.500 | 6513.250 | 1.0012x |

The vector-versus-control differences are only 0.03–0.12%, insufficient evidence
of a useful SIMD gain. The approximately 12% improvement over the production
renderer on central blocks is reproduced by the scalar control, whose short
rows execute the same inline loop. It cannot be credited to vector stores.
Separating the scalar clipping/code-generation effect is a future portable-C
experiment; the present trial does not identify a specific compiler decision
as its cause.

Do not promote this SIMD rectangle candidate on these results. The cached raw-row
speedup did not translate into useful full-screen/clipped-workload benefit. Keep
the test bed for future memory/layout experiments, and prioritize scalar clipping
and masked/varying-source blend measurements before a target-specific renderer
hook. No whole-scene frame-rate gain is claimed.

The matched-control image and source snapshots are retained separately under
`control-source`, `control-hashes.txt` and `controlled`. Post-test diagnostics
reported 12 checks, intact buffers/guards/heap, zero mismatches/transfer failures
and 768 bytes minimum main-task stack reserve. No new physical display/touch
confirmation was supplied for this session.

# Validation and limits

Normal and power images built; all three profile-isolation checks passed.
Tracked C formatting, clang-tidy, Cppcheck and size checks passed. New candidate
C received explicit formatting, Cppcheck and size checks. Cppcheck initially
misinterpreted termination of the compound round-edge loop as implying an empty
span. A narrow suppression documents that the loop also exits on an inside-circle
pixel while the span remains nonempty. Device oracle/workload checks exercise
that branch. The matched-control version passes Cppcheck with this explanation.
No portable core code changed, so core/sanitizer suites were not rerun. Station
tests reject failed correctness and absent, malformed or nonpositive timings.
Production clipping and product policies are unchanged. Physical display/touch
and whole-scene benefit remain separate checks.

# References

- [Benchmark implementation](../../ports/esp32/main/render_benchmark.c)
- [Station runner](../../tools/debug/rect_simd_experiment.py)
- [Original scanline experiment](memo-034-scanline-rectangle-fill-experiment.md)

# Session completion

Normal firmware was restored with verified transfer hashes. Debug reported
profile `game`, intact buffers/guards/heap, zero mismatches/transfer failures and
1,356 bytes minimum main-task stack reserve at the first sampled check. Clock
trust remains unknown and serial is closed. Current-renderer and candidate
object disassembly is retained for follow-up; both inline circle arithmetic,
so an out-of-line circle helper does not explain the central-block difference.

Final formatting, static analysis, new-source size checks, station tests and
all profile-isolation checks passed. Documentation check/repair passed for
84 records without repairs; `git diff --check` passed. Progress and completion
were sent to the user-requested ntfy topic. No commit, merge or push was made.

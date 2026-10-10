---
id: memo-033
title: Full-width display copy experiment
author: Codex
created: 2026-10-10T06:54:21Z
tags: [embedded, performance, rendering, validation]
project_id: jelligotchi
doc_uuid: 57c275bd-3aaa-4ada-bf50-d13a5e237b6f
---

# Scope

The user requested testing one optimization at a time after the
[render-path review](memo-031-esp32-render-acceleration-review.md). The first
experiment compared full-width row copies with one contiguous CPU copy. The
measured gain was only 0.09%, so the production row-copy path was restored.
The candidate helper and explicit A/B diagnostic remain for reproducibility. No SIMD, DMA-copy, buffer ownership, renderer,
clock, or cache changes are included.

Concurrent work replaced tracked edits in the original checkout during testing.
The experiment was moved to branch `perf/esp32-copy` in the sibling
`jelligotchi-render-copy` worktree, based on `80af88d`. Tests reported here apply
to that isolated source, not the concurrently edited creature implementation.
Do not interpret the earlier shared-checkout test logs as validation of this patch.

# Change and bounds

`display_copy.h` holds the copy operation so a host test can exercise the same
code used by the device. Its caller checks bounds and requires two separate,
packed 466 by 466 buffers. Full-width damage implies x is zero after validation.
Empty damage returns without accessing memory. A full-width band at a nonzero y
starts at that row and copies only the requested height. The display mutex and
synchronous present contract remain unchanged. No new production buffers or
steady-state allocations are introduced.

The host test checks every destination pixel against the requested rectangle,
source preservation, and guards before and after both frames. Cases include
full screen, middle and last-row bands, narrow damage, an edge pixel, one column,
and empty rectangles at the screen boundary. It uses explicit failures, so the
checks remain active in release builds.

# On-device comparison

The explicit `display benchmark-copy` command compares 32 pairs of full-frame
copies in the application's PSRAM buffers, alternating method order. It reports
summed elapsed microseconds for row and bulk copies; divide each by 32 for its
mean. Frame comparisons occur outside the timed section after every copy, with
boundary checks after the run. The command holds the display mutex, briefly
pauses normal rendering, and invalidates the canvas on completion. Its fixed
scratch storage is small; it allocates no benchmark framebuffers.

This measures copy elapsed time under current task/cache conditions, not panel
FPS or normal concurrent LVGL throughput. Repeat it to assess variability. It
also brings canvas contents into agreement with the engine, so capture mismatch
evidence before running it. The CLI accepts the command with a 30-second timeout.

The user authorized flashing the isolated baseline. The board was rediscovered
as USB JTAG/serial 303A:1001, serial 90:70:69:FE:21:DC, on
`/dev/cu.usbmodem2101`. Flash completed with transfer hashes verified and an RTS
reset. Debug commands responded after boot. The build descriptor reports 0.3.0;
the measured application binary SHA256 is
`1563df6f85f6797811ee6d34506764cea5f059ad8304219c11de10c47a793323`.

Five runs each compared 32 pairs (160 copies per method):

| Run | Row total, microseconds | Bulk total, microseconds |
| --- | --- | --- |
| 1 | 661820 | 661401 |
| 2 | 661893 | 661473 |
| 3 | 661893 | 661228 |
| 4 | 661941 | 661325 |
| 5 | 662256 | 661354 |

Mean copy time was 20.6863 ms for rows and 20.6674 ms for bulk. The saving was
18.8875 microseconds, or 0.0913%. This does not establish a useful frame-time
improvement. Per-run means ranged from 20.6819 to 20.6955 ms for rows and from
20.6634 to 20.6710 ms for bulk. No new clock/cache settings were used.

Every A/B response passed equality and boundary checks. Display diagnostics
before and after reported equal buffers, intact guards, valid heap, zero
mismatches, and zero transfer failures. The later sample counted 154 transfers
and reported 1,380 bytes of engine-task stack headroom. These checks do not read
panel memory or prove physical display/touch behavior. Physical confirmation
was requested separately and remains pending unless recorded below.

The production fast path was reverted after this result. The explicit benchmark
still exercises both methods. No second optimization was attempted in this run.
Ignored evidence is in `build/copy-flash.log`, `copy-benchmarks.json`, and
`copy-display-before.json` / `copy-display-after.json` in the isolated worktree.

The restored production path was rebuilt and flashed separately; transfer hashes
passed. Its application SHA256 is
`934988e3d22644eb3fe938b5472b04fe7f3767c0a9dc46bf4861abdc99c3e0ca`.
Post-flash USB diagnostics again passed buffer equality, guards, and heap checks,
with zero mismatches and zero transfer failures (62 transfers at the sampled
check, 1,252 bytes of engine-task stack headroom). Final build/flash, diagnostic,
and runtime logs use the `build/copy-final-*` prefix. Physical verification is
still pending. No commit, push, or merge into the creature branch was performed.

# Validation and build findings

- Isolated desktop tests passed 49/49, including the copy test and SDL smoke/demo.
- Isolated core-only tests passed 24/24.
- The ESP32 firmware build passed after the separate build repair below.
- Sanitizer tests passed 49/49; the copy test passed again after a test-only rename.
- Formatting, clang-tidy, Cppcheck, and C size checks passed.
- Documentation checks passed. Repair sorted tags in the existing memo-032 and
  RFC-005 frontmatter; these metadata-only repairs were reviewed.

The first firmware attempt failed on a pre-existing static assertion comparing
two anonymous enum types in `pet_ui.h`, under GCC's enum-compare warning. Casting
both small enum constants to int preserves the assertion and fixes the warning
without relaxing diagnostics. The isolated firmware then built successfully.

Cppcheck initially flagged the test's file-scope source/destination names as
shadowed by the helper's arguments. The test buffers were renamed; no diagnostic
suppression was added. The device result above supersedes the initial timing
uncertainty; broader scene and pipeline timings remain unmeasured.

# References

- [Copy helper](../../ports/esp32/main/display_copy.h)
- [Presentation and benchmark](../../ports/esp32/main/display_output.c)
- [Damage and guard tests](../../tests/test_display_copy.c)
- [Diagnostic commands](../../docs/playing.md#debug-cli)

---
title: Hardware optimization continuation and startup recovery
author: Codex
created: 2026-10-11T05:28:53Z
tags: [embedded, performance, validation]
id: memo-056
project_id: jelligotchi
doc_uuid: a672e04a-55d2-442c-8806-9eda4d91d4b6
---

# Scope

The user requested continuing each outstanding hardware optimization. Work uses
`perf/hardware-optimization-passes`, based on main `88cc868`, in the isolated
jelligotchi-hardware-optimization worktree. The earlier investigation and its
experiment gates remain authoritative. This pass does not promote unmeasured
power settings or claim the whole plan is finished.

# Implementation and resource budget

The power image gains bounded pipeline samples for engine work before present,
display-lock wait, damaged canvas copy, diagnostic checks, adapter byte swap,
internal DMA staging and submission-to-color-completion latency. Engine timing
includes simulation and rendering; it is not isolated renderer timing. Transfer
timing includes the panel submission call and interrupt latency; it is not a
measurement of visible pixels. Stages overlap across tasks and cannot simply be
summed into an end-to-end response figure. LVGL rendering and input IRQ-to-visible
latency still need separate attribution.

Each stage keeps its last 128 microsecond samples, total count, cumulative time,
bytes and maximum. Nearest-rank percentiles describe that window; totals describe
the whole run. Seven windows and report scratch use 4,288 bytes, bounded by a
4.5 KiB assertion. No task, queue, framebuffer or steady-state allocation is added.
A critical section protects shared state. The ISR performs constant work, with
its code in IRAM and shared state in internal DRAM. Sorting and JSON output stay
in task context. Boot/run correlation rejects stale station results. Capture expires after 60
seconds even if the station disappears; expired results are rejected. Power
trials and pipeline capture exclude one another. Normal firmware has no profiling
storage or adapter patch.

A source-hash guard instruments a build-local copy of pinned adapter 0.6.4.
The managed source remains unchanged. Its existing transfer-completion behavior
is preserved. Both normal and power images retain separate engine/canvas buffers,
display mutex and the reserved internal DMA strip.

The fused RGB565 swap/copy candidate uses alias-safe word operations and a scalar
tail, accepting arbitrary byte alignment. Host tests compare all output bytes,
source preservation and guards for offsets 0 through 15, odd/even spans, empty
input, rows and a full 46,600-byte strip. A power-only A/B command compares the
existing LVGL swap plus memcpy with the candidate in the actual PSRAM-to-internal
memory path. It drains pending SPI transfers with a checked brightness command
before borrowing the strip, restores the canvas and invalidates it afterward.
It is not enabled in the normal display path and has no established device gain.

New resume coverage checks an eight-hour linked sleep across checkpoint reload,
interrupted catch-up, rejection of a partial-resume save, identical completed
checkpoints, one journal entry and a 24-hour simulation cap. Calls remain bounded
to one simulated minute. Existing desktop session tests cover reversed clocks,
capped gaps and corrupted checkpoint preservation. This is a prerequisite test,
not an automatic hardware-sleep feature.

# Device observations and recovery

Rediscovery found SKU 31261 USB identity 90:70:69:FE:21:DC. Before changing firmware,
normal-game diagnostics reported matching buffers, intact guards/heap, zero
mismatches and transfer failures, and 1,260 bytes of minimum main-task stack reserve.

The first profiling image flashed with verified hashes and completed a ten-second
home-idle capture. That sample reported:

| Stage | Samples | Median us | p95 us |
| --- | --- | --- | --- |
| Engine work before present | 365 | 833 | 83,873 |
| Display-lock wait | 365 | 18 | 29 |
| Canvas copy | 27 | 21,074 | 21,326 |
| Diagnostics | 2 | 22,363 | 22,395 |
| Byte swap per strip | 254 | 1,494 | 1,526 |
| Staging per strip | 254 | 1,275 | 1,300 |

These are exploratory observations from a changing game scene. They do not
establish power savings, acceleration or latency under audio/network load.
The completion count was zero: the initial hook used the adapter's externally
notified completion path, whereas this board registers the SPI IO callback.
The corrected hook targets that actual callback. The station now rejects a run
with staging samples but no completion samples. The original capture must not
be used as a complete pipeline baseline.

The corrected profiling image flashed with verified hashes, but startup repeatedly
aborted at checked brightness. The returned -1 was portable device-unavailable,
not proof of a failed brightness SPI transaction. Inspection found a pre-existing
race: the panel handle is assigned in the custom draw callback, but startup called
brightness without ensuring any draw had occurred. The fix explicitly invalidates
and refreshes the canvas while holding the LVGL mutex before checking the handle
and requesting brightness. This avoids depending on scheduler timing or guessed
delays. The corrected source builds, but its device verification is still pending.

The recovery attempt to flash normal firmware could not establish the bootloader
connection: esptool reported no serial data. Rediscovery still saw the same USB
identity and no remaining client held the port. The user was asked to enter
BOOT/reset download mode. No partition erase, eFuse write, PMIC change or repeated
blind flash was attempted. At this point normal-firmware restoration is **not
verified**; the last successful flash was the failing validation image.

The initial sleep-cap test incorrectly expected zero journal records while a
session was active. The journal creates its record at bedtime. The test was fixed
to expect that one active record; no game behavior was changed.

# Remaining plan and gates

| Planned pass | Status / next gate |
| --- | --- |
| Pipeline attribution | Profiler implemented; corrected completion hook needs a working device run; LVGL/input stages remain |
| Touch latency / recovery | Existing guard and synthetic A/B retained; physical short taps/swipes and idle-current measurements pending |
| Ordinary display sleep / brightness | Earlier 121 ms vs 1.36 s result retained as validation-only; current and repeated physical wake gate production policy |
| Touch wake | Physical source/edge/held-contact/release matrix pending; timer fallback is not touch wake |
| IMU motion wake | Existing identity and quiet INT2 qualification retained; confirmed pickup event is required before sleep trials |
| RTC / automatic sleep | New deterministic interrupted overnight coverage; trusted RTC, station-timed gap and safe host sleep/resume policy remain |
| CPU spans / SIMD | Engine work is substantial, but needs isolated render attribution and scalar reference measurements before target kernels |
| Fused display staging | Candidate and A/B diagnostic implemented; device result and end-to-end comparison pending |
| TE synchronization | Exact-board TE wiring and pulse/tearing observation still required |
| CPU clock / cache / affinity | One-setting A/B builds await a complete baseline and recovered board; no settings changed |
| Peripheral idle | Readback/current comparison needed; preserve RTC trust and independently powered sensor cleanup |
| PMIC telemetry | Existing read-only inventory retained; enabled ADC channels need comparison with a meter |
| PMIC rails / sleep | Fitted-board loads, back-power, current, reset/wake and save/RTC recovery prerequisites remain |
| Touch protocol / gesture, display partial, IMU interaction | Need project-specific protocol or product behavior qualification; no speculative register writes |

An external current meter, physical observer and (for TE/protocol work) a signal
capture fixture are still missing from this session. Do not turn these skipped
gates into passed tests or blanket-enable all proposed settings.

# Evidence and validation

Ignored evidence is under `build/pipeline-pass-001`: initial capture, flash logs,
raw reboot trace, failed recovery log and candidate sources/configuration.
Normal, power and factory firmware builds pass, with isolation checked for each
profile. C formatting, clang-tidy, Cppcheck and size checks pass. Documentation
check and repair pass for 77 records with no repairs. The final desktop suite passes 74/74, core-only passes 33/33 and ASan/UBSan
passes 74/74. The station rejection tests also passed after adding expiration
checks. An altered adapter-source fixture was rejected by the SHA256 gate.

Static analysis initially interpreted the power-only profiler through its disabled
header stubs. Guarding the implementation with the same profile flag removed that
invalid combination. Test buffer names were changed to avoid shadowing helper
parameters. The display command dispatcher exceeded complexity 20 after adding
experiments; a separate experiment dispatcher restored the existing limit. A missing JSON format placeholder in the expiration-report change was caught
by the firmware compiler and corrected; the final power image builds cleanly. No
warning, complexity limit or comparison tolerance was relaxed.
This work remains experimental. Publication of the branch does not close the
hardware validation and recovery gates above.

# References

- [Ranked validation plan](../../docs/device-validation-passes.md)
- [Capability backlog](memo-047-device-capabilities-and-experiment-backlog.md)
- [Portable device boundaries](memo-053-portable-device-boundaries.md)
- [Pipeline station](../../tools/debug/pipeline_experiment.py)
- [Profiling implementation](../../ports/esp32/main/pipeline_profile.c)
- [Staging experiment](../../ports/esp32/main/staging_benchmark.c)

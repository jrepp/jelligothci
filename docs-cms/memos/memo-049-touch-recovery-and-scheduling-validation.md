---
title: Touch recovery and input scheduling validation
author: Engineering Team
created: 2026-10-10T20:25:17Z
tags: [memo, technical]
id: memo-049
project_id: jelligotchi
doc_uuid: d93143b0-1c05-46e6-94f7-76275b1f729f
---

# Overview

Continued the impact-ranked methodology from
[memo-048](memo-048-factory-inventory-validation-pass.md) with touch failure
recovery and input scheduling. The real LVGL adapter reproduced two unsafe
release behaviors under scripted fault injection. A shared ESP32 guard cancels
the interrupted gesture and requires a valid no-contact report before accepting
a new one. Normal firmware uses that guard; timer-period changes remain experiments.

# Experiment and boundaries

The power validation image is used because this case depends on the real LVGL
adapter/canvas and game input contract. It shares the factory USB parser/client
instead of adding a bespoke factory renderer. The runner injects eight bounded
samples through the adapter's custom-read and task-context notification APIs:
idle, press, failure, continued hold, release, fresh press, release, idle.
The existing gesture functions consume LVGL events into a diagnostic gesture;
the event filter prevents synthetic actions reaching the live game queue.

Legacy mode models the pinned adapter ignoring a read error and consuming the
last cached point. Propagate mode returns the error, causing LVGL release.
Guard mode calls the same cancellation/re-arm helper used in normal firmware.
This verifies adapter/event semantics, not real electrical I2C failures or the
controller's retry/reset delays. Normal driver read failures are now checked
before obtaining cached points; the guard cancels the application gesture before
LVGL's forced release. A continuing contact remains suppressed until a valid
zero-contact report. That can deliberately discard a contact after a fault.

Each case has a three-second device deadline, five-second host deadline, retained
run ID/result and restoration to CONFIG_LV_DEF_REFR_PERIOD. Physical IRQ activity
marks a case contaminated. Sleep/reset and touch trials cannot overlap. The
station records raw traffic, checks expected control failures and guarded success,
and does not retry an ambiguous action. Normal firmware excludes the runner.

Trial state is capped at 192 bytes plus a 768-byte report. The normal guard is
caller-owned, retains only a gesture pointer and a release gate, and allocates
nothing. Both callback and application gesture are serialized by the LVGL lock.
No SDK component was edited, no new task created, and portable core behavior is
unchanged.

# Results

An initial six-case hardware probe matched the hypotheses, then the shared guard
was incorporated and a 60-case matrix captured on the same discovered device
`90:70:69:FE:21:DC` through `/dev/cu.usbmodem2101`.

| Treatment | Runs | False taps | Notification-to-read median / p95 / worst (us) |
| --- | --- | --- | --- |
| Cached-state control, 33 ms | 10 | 10 | 26,627.5 / 32,592 / 32,716 |
| Error propagation only, 33 ms | 10 | 20 | 23,990 / 32,607 / 41,345 |
| Shared guard, 33 ms including restored baseline | 20 | 0 | 24,304.5 / 32,016 / 32,898 |
| Shared guard, 16 ms | 10 | 0 | 9,997.5 / 14,997 / 15,859 |
| Shared guard, 10 ms | 10 | 0 | 6,028 / 10,640 / 12,660 |

Every guarded run accepted exactly one fresh tap. The two controls demonstrate
why checking the error alone is insufficient: one can commit the interrupted
contact on the forced release, then treat the continuing hold as another press.
All 480 injected samples were consumed. No trial reported contamination or
timeout. p95 uses nearest rank over the eight samples per run. The restored
33 ms group is combined with its initial baseline in the table.

Timing begins at software notification, excluding controller scan, physical IRQ
and I2C read latency. Main-loop phase, LVGL work and the running game's changing
scene affect scheduling; this is an exploratory scheduling comparison, not a
controlled end-to-end latency benchmark or power measurement. Production keeps
33 ms until physical short-tap/swipe and idle-wakeup/current measurements justify
changing it.

After the matrix, display diagnostics showed zero mismatches/transfer failures,
equal buffers, intact guards/heap and 1,232 bytes reported main-task stack reserve.
Physical touch behavior, real fault recovery, display quality and energy remain
unverified. The bounded valid-release recovery policy is the only production
behavior promoted from this pass.

# Validation and reusable workflow

The [touch pass workflow](../../docs/device-validation-passes.md#touch-recovery-and-scheduling-pass)
documents commands, replay, interpretation and restoration. Station tests reject
false taps, missed recovery, stale runs, contaminated/time-out cases, incomplete
samples, invalid times and incorrect restored periods. They are included in the
normal CTest suite, which passed 65/65 tests.

C formatting/static analysis/size checks cover the new sources. One intermediate
command dispatcher exceeded complexity 20; a focused dispatch helper resolved it.
Cppcheck also treated an unsigned wrap guard as always true; the code now checks
UINT32_MAX before incrementing. The final size check covers 201 files with the
existing limits unchanged. No suppression or relaxed threshold was needed.

Evidence is under ignored `build/touch-pass-001/`: initial probe, 60-case matrix,
raw/reply logs, tested binaries/configuration, flash/restoration logs and source
snapshot. All three firmware builds and profile isolation checks passed. A six-case
smoke run of the final image passed after formatting and run-ID wrap handling.
Normal firmware was restored with verified transfer hashes and queried for game
capabilities, state and display diagnostics. Documentation check/repair validation
passed without repairs. No serial client remains open. This finding does not authorize or establish a new factory acceptance
threshold.

# References

- [Capability research](memo-047-device-capabilities-and-experiment-backlog.md)
- [Factory methodology](../../docs/device-validation-passes.md)
- [Validation layout](../../ports/esp32/main/validation/README.md)

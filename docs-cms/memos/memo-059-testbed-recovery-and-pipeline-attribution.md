---
title: Test-bed recovery and pipeline attribution
author: Codex
created: 2026-10-11
tags: [embedded, performance, validation]
id: memo-059
project_id: jelligotchi
doc_uuid: cf875988-89d8-4b32-9d0d-786b28eb0eca
---

# Scope

The user requested sequential work on remaining hardware features through the
factory test bed. Work is based on `ab2b6f7`, on
`fix/hardware-testbed-startup`. Factory inventory uses the standalone image;
display/input/pipeline experiments use its companion power profile and shared
station protocol. No charging, rail, automatic sleep or gameplay defaults changed.

# Startup failure and remedy

Rediscovery found USB serial 90:70:69:FE:21:DC at `/dev/cu.usbmodem2101`, running
a healthy power image. A fresh normal-image flash verified every transfer hash,
but the app repeatedly overflowed the main-task stack after LVGL startup. The
serial trace identifies version 0.8.0 and ELF prefix 13fa735bf. Thus the synchronous
first-refresh fix from memo-056 did not establish safe recovery.

Startup now invalidates the canvas and releases the display mutex, allowing the
LVGL task to perform the refresh on its own stack. A bounded readiness wait reads
the handle, submitted-transfer count and failure count under that mutex. Checked
brightness follows successful readiness; it also checks its handle under the
mutex. No task, queue, framebuffer, persistent state or stack increase is added.
The wait deadline is owned by
[display_transfer.c](../../ports/esp32/main/display_transfer.c).

The fixed normal image flashed successfully and responded as profile `game` with
healthy buffers/guards/heap, no mismatches or transfer failures and 1,420 bytes
of minimum main-task stack reserve. This closes the failed-restoration gate in
memo-056 for that tested image. At session end the final normal image was flashed
with verified hashes and confirmed as profile `game`: healthy buffers/guards/heap,
zero mismatches/transfer failures, 476 submitted transfers and 1,404 bytes of
minimum main-task stack reserve. Clock status remained unknown. The serial client
was closed. Physical confirmation is distinct from these serial observations.

# Pipeline attribution

The corrected completion hook recorded transfers successfully. The test bed now
also wraps the actual pet renderer using a power-only GNU linker option, and
records LVGL render-start/render-ready events. The portable renderer is unchanged.
Normal/factory link-map isolation checks reject accidental wrapper inclusion.

Nine timing windows plus report scratch occupy 5,360 bytes, bounded by a 5.5 KiB
assertion. The two additional windows cost 1,072 bytes; LVGL timing adds an
eight-byte timestamp and one startup event registration in LVGL-managed memory.
There is no per-frame allocation, extra task or framebuffer. Existing capture
expiration and mutex/ISR boundaries remain. Station validation now rejects
missing renderer/LVGL instrumentation as well as missing completion samples.

LVGL's interval includes dirty-area rendering, flush submission and waits for
earlier strips. It is elapsed time, not pure CPU time or final visible scanout.
Renderer time is also elapsed time and may include task preemption. These
overlapping stages must not be summed into a response figure. Physical
IRQ-to-visible instrumentation remains outstanding.

Ten ten-second captures of the evolving saved-pet scene passed with healthy
display counters. Per-run last-window p95 ranges were 4,729–5,389 us for the
renderer, 82,026–82,097 us for LVGL, 21,089–21,101 us for canvas copy and
2,739–2,745 us for strip completion. The captures recorded 4,804 renderer calls,
1,533 LVGL intervals and 2,633 completions. These ranges are not pooled
percentiles. Maximum renderer elapsed time was 112,575 us, so the short last
windows do not characterize every scene transition. This points to examining
LVGL/full-update work before assuming an S3 arithmetic kernel is the next win.

The initial build caught an incorrect assumption that LVGL's event-registration
API returns a descriptor. The pinned API returns void; registration now checks
the event-count increase. Static analysis also required a const readiness
argument. Neither compiler nor analysis limits were relaxed.

# Sequential bench results

Evidence is retained under ignored `build/hardware-followup-001`, including raw
serial, station replies, image/configuration hashes and source patches.

| Pass | Observation | Remaining limit |
| --- | --- | --- |
| Factory inventory | Six automated checks passed, no errors; four physical checks skipped | No manufacturing acceptance, voltage calibration or current measurement |
| RTC progression | Ten station-bounded intervals passed | Oscillator-stop flag set; UTC trust and retention remain unestablished |
| Fused staging | Ten runs, sixteen A/B pairs per size; exact pixels and healthy restoration | Full-strip median totals: existing 42,586.5 us, fused 124,132.5 us; candidate is about 2.9 times slower and remains disabled |
| Synthetic touch | All sixty trials matched expected guarded/control behavior | Synthetic notification timing does not measure physical IRQ-to-visible latency |
| Panel transition | All eighty transitions restored; median ordinary-sleep recovery 121,544.5 us versus standby 1,364,576 us | Command completion does not prove visible restoration or energy savings |
| IMU prerequisite | Ten identity/handshake and ten quiet-INT2 cases passed with cleanup | Confirmed pickup is required before motion sleep trials |

One prompted `motion-int2` trial ran on the attribution image for 21,097,059 us
including setup. GPIO21 remained low and STATUS1 was zero. The station correctly
rejected pickup qualification. All three commands/ACKs completed, the original
register snapshot was restored, and no cleanup fault was latched. The user had
confirmed readiness, but whether the lift occurred during the armed window is
still awaiting confirmation; readiness alone is not stimulus evidence. No IMU
sleep trial followed. Retain this failure and resolve stimulus/configuration
before another explicitly coordinated trial; do not lower the acceptance gate.

The user subsequently confirmed that the display and ordinary short taps/swipes
look normal on the attribution power image. This is an operator observation,
not a timed physical input matrix or confirmation of each earlier sleep/wake
transition. The user is available for pickup/touch checks but has no current meter.

# Remaining work

Repeat pickup qualification and complete display/touch-wake checks before promoting
wake behavior. One confirmed awake pickup now passes, as recorded below.
Automatic sleep also needs trusted-time recovery and an explicit host
policy. Current, PMIC voltage comparison, rail/back-power, TE and module protocol
gates remain open. AP provisioning and authenticated network debug remain a
separate Draft RFC-003 increment. No energy or battery-life claim follows from
these USB-attached measurements.

# Validation

All three firmware profiles build and pass source/configuration isolation;
the updated link-map check also isolates the render wrapper. Station transcript
tests passed for factory, RTC, touch, panel, IMU and pipeline rejection, including
missing render/LVGL samples. C formatting, clang-tidy, Cppcheck and size checks
passed. `make docs-check` and `make docs-fix` passed for 80 documents with no
repairs; `git diff --check` passed. No portable game logic changed; the desktop,
core-only and sanitizer C suites were not rerun in this adapter/profiling pass.

# Prepared operator retry

The user requested another attempt, then specified a confirmed window so they
could return later. Rediscovery found the same USB identity and normal game
profile. The unchanged power image rebuilt, passed isolation and flashed with
verified hashes. It reports healthy display diagnostics, zero mismatches/transfer
failures and 1,312 bytes of minimum main-task stack reserve. `power imu result`
reports `idle`; no retry was armed. Serial ownership was released.

Evidence is under `build/hardware-pickup-retry-001`. The board intentionally
remains in the power profile awaiting the user's explicit ready message, per
their requested test arrangement. Start one bounded trial only after that message,
announce `ARMED`, then record whether the pickup actually occurred in the window.
Restore normal firmware after the observed trial/session. This preparation adds
no pickup or MCU-wake evidence and supersedes the earlier end-of-session firmware
state, not its successful restoration evidence.

# Confirmed pickup window

After the user explicitly reported ready, rediscovery found the same USB identity
at `/dev/cu.usbmodem2101`. The station ran one unchanged `motion-int2` trial,
announced `ARMED`, and the user confirmed lifting/tilting during that window.
The station reported `motion_observed`: initial GPIO21 low, observed GPIO21 high,
STATUS1=04, all three command/ACK handshakes successful, and the original register
snapshot restored with no operation or cleanup error. Total setup/observation was
5,412,058 us; this is not pickup-to-interrupt latency because the physical stimulus
was not timestamped. Firmware identity remained 1e0301, revision7c, USID
32b65a0e65c3. Evidence and the operator confirmation are in
`build/hardware-pickup-retry-001/confirmed-window`.

This establishes awake pickup detection for this specimen and explicit INT2
comparator. It does not establish repeated-event reliability, MCU wake from sleep,
or power savings. GPIO remained high immediately after STATUS1 acknowledgement
and through the weak-pull checks, before register cleanup. Qualify deassertion and
already-active-level handling before relying on this signal for repeated sleep.
The next IMU increment is a bounded sleep/wake experiment with explicit wake-cause
reporting, timer fallback and restoration. No new sensor settings or code changes
were needed for this successful retry.

Normal firmware rebuilt, passed profile isolation and was restored with verified
flash hashes after the trial. The shared debug path confirmed profile `game`,
healthy buffers/guards/heap, zero mismatches/transfer failures and 1,420 bytes of
minimum main-task stack reserve. Clock trust remains unknown. The serial client
is closed; the earlier prepared power-profile state is no longer current.
Documentation check/repair passed for 80 records without repairs, and
`git diff --check` passed. No host C tests were rerun for this unchanged-firmware
retry; physical evidence is limited to the operator-confirmed pickup.

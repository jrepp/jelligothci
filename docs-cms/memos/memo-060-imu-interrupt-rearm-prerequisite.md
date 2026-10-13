---
title: IMU interrupt rearm prerequisite
author: Codex
created: 2026-10-11
tags: [embedded, imu, validation]
id: memo-060
project_id: jelligotchi
doc_uuid: 75a9be98-a3ed-4ab2-b535-57968a61bf52
---

# Scope and evidence

The user requested continuing after the
[confirmed awake pickup](memo-059-testbed-recovery-and-pipeline-attribution.md#confirmed-pickup-window).
That trial observed INT2 high immediately after acknowledgement. The next bounded
increment qualifies return to idle and another pickup before MCU sleep.

Reviewed the exact-board vendor's pinned QMI8658A Rev A datasheet, document
13-52-25, section 12.4, printed page 67. It specifies that a STATUS1 read clears
the motion flag and resets the selected interrupt to its configured initial
level; further motion events toggle the line. Therefore the previous immediate
high reading cannot distinguish a stuck line from movement during acknowledgement.
The explicit INT2 comparator remains restricted to the previously observed
firmware. This reference does not resolve the fitted A/C suffix or authorize
generic sensor initialization.

# Implementation

The power-profile `rearm-int2` mode uses the existing board probe, shared protocol
and retained results. It initializes motion once and observes two episodes.
Each episode has a twenty-second pickup window, three seconds to put the board
down, one STATUS1 read, and a one-second deadline to establish a sampled low
level for at least 100 ms. It does not repeatedly read/clear status to force a
successful result. Only after the first idle observation does it arm the second
pickup window. Sensor cleanup and register readback follow success or failure.

The trial reports both observed motion levels/status bytes, immediate ACK levels,
idle levels, completed-episode count and elapsed acknowledgement windows. A
missing observation is explicit. Any operation/cleanup failure stops and latches
the runner. The station requires both complete episodes and clean restoration;
it accepts only one operator-coordinated run per invocation. Existing single-event
modes retain their behavior and acquire only additive report fields.

The linker map confirms 64 additional bytes of persistent state, bounded by an
assertion. The existing report buffer is unchanged. There is no additional task,
queue, framebuffer, per-trial allocation, sensor reset or change to motion
thresholds. All changes remain in the validation layer; the portable driver and
normal/factory sensor policies are unchanged. Polling does not prove the absence
of short interrupt pulses, and acknowledgement time includes the stable-low
observation interval. The MCU never sleeps in this mode.

# Validation and remaining gates

All three firmware profiles build and pass isolation. Station tests reject a
missing second event, missing motion status, invalid pins, partial arrays, short
or expired idle observations, stale identity and failed restoration. Hardware
evidence is under ignored `build/imu-rearm-001`, including source patch, image/
configuration and datasheet hashes, flash log and station transcripts.

The updated power image flashed with verified hashes on USB identity
90:70:69:FE:21:DC at `/dev/cu.usbmodem2101`. Ten identity/handshake and ten
quiet-INT2 trials passed with clean cleanup. Display diagnostics reported intact
buffers/guards/heap, zero mismatches and transfer failures, and 1,280 bytes of
minimum main-task stack reserve. The serial client is closed and the power image
is prepared for the explicitly confirmed operator window; no two-pickup run has
started. Normal firmware remains available for restoration after that session.

C formatting, clang-tidy, Cppcheck and size checks passed. All four station test
methods passed. Documentation check/repair passed for 81 records; the sole repair
sorted this memo's tags and was reviewed. `git diff --check` passed. No portable
C driver/core changes were made, so host C and sanitizer suites were not rerun.

The subsequent operator-confirmed rearm result is recorded below. MCU sleep,
already-active wake-level handling and current measurements remain later gates.
No success in those categories is inferred from this awake diagnostic.

# Confirmed two-pickup result

After explicit operator readiness, rediscovery confirmed the same USB identity.
One `rearm-int2` run completed both pickup windows without reconfiguring the
sensor between them. The user confirmed both lifts occurred after the respective
prompts and that the board was promptly set down each time. The station returned
`rearm_observed`, with motion levels [1,1], motion status [04,04], immediate
ACK levels [0,0] and established idle levels [0,0]. The acknowledgement windows
were 119,683 us and 322,708 us, including sampled stable-low observation and
host scheduling; these are not interrupt deassertion latencies.

The total setup/two-window duration was 24,174,607 us. All three command
handshakes completed and the baseline register snapshot was restored with no
operation or cleanup error. Run 21, boot 52158692 retained firmware identity
1e0301 and USID 32b65a0e65c3. Evidence and operator confirmation are under
`build/imu-rearm-001/observed`.

This confirms two awake detections and return to sampled idle on this specimen.
It resolves the earlier inconclusive immediate-high reading for this tested
sequence; it does not establish long-run reliability or rule out short pulses
between polls. The MCU did not sleep and no current was measured. The next
increment is bounded MCU light sleep with GPIO21 wake, an independent timer
fallback, already-active-level handling and verified restoration.

After the observed trial, normal firmware rebuilt, passed profile isolation and
flashed with verified hashes. Debug confirmed profile `game`, intact
buffers/guards/heap, zero mismatches/transfer failures and 1,420 bytes of minimum
main-task stack reserve. Clock trust remains unknown. The serial client is
closed; the prepared power image is no longer running. Documentation check and
repair passed for 81 records with no repairs; `git diff --check` passed. No code
changed during this operator trial, so host tests/static analysis were not rerun.

# References

- [Operator workflow](../../docs/device-validation-passes.md#two-pickup-interrupt-rearm-prerequisite)
- [Trial implementation](../../ports/esp32/main/validation/power/imu_trial.c)
- [Pinned vendor QMI8658A Rev A datasheet](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/e4344e70c2fa78a13e8a06566507f1ba8af6672a/examples/arduino/libraries/SensorLib/datasheet/QMI8658A%20Datasheet%20Rev%20A.pdf)

---
title: Bounded IMU light sleep trial
author: Codex
created: 2026-10-11
tags: [embedded, imu, validation]
id: memo-061
project_id: jelligotchi
doc_uuid: d9d1f1cc-146e-4b45-a62d-e742f2c02116
---

# Scope

Following the [confirmed two-pickup rearm test](memo-060-imu-interrupt-rearm-prerequisite.md#confirmed-two-pickup-result),
this increment adds an explicit bounded MCU light-sleep experiment in the power
companion of the factory test bed. It does not enable automatic product sleep or
pickup gameplay. The existing sensor firmware restriction, motion configuration,
single probe ownership and final register restoration remain in force.

# Implementation and resource budget

[imu_sleep.c](../../ports/esp32/main/validation/power/imu_sleep.c) configures active-high
GPIO21 wake with an independent finite timer. It checks for an already-active
line before quiescing and immediately before the sleep API; an active line skips
sleep and reports failure without retrying or clearing the evidence to force entry.
It records API attempt/completion, SDK wake cause, the pin level immediately on
return and elapsed time before host recovery or the STATUS1 acknowledgement.
The ESP32-S3 SDK does not expose the deep-sleep GPIO bitmap API for this light-sleep
path; attribution uses the configured GPIO source, captured level and motion bit.

The experiment reuses the existing LVGL pause, SPI-draining brightness command,
audio close and panel sleep/recovery sequence through
[power_diag.c](../../ports/esp32/main/validation/power/power_diag.c). It excludes
blocking diagnostic time from live pet progression and drains bounded pending
input; the touch filter consumes the eventual release. Configured/pending network
work prevents entry. Cleanup disables each successfully enabled wake resource;
wake-source cleanup, host restoration and sensor restoration are separately
reported. Any operation/restoration error latches the IMU trial against another run.

The linker map measures 32 bytes of new static sleep state; a compile-time budget
bounds it. The existing retained report grows by 300 bytes. No new heap allocation,
task, queue, framebuffer or sensor handle is added. The callback executes on the
engine task and uses bounded local state; physical trials must inspect stack reserve.
Normal and standalone factory profiles exclude the sleep probe. The offline query
in the network adapter is read-only and called on the engine task.

The [station runner](../../tools/debug/imu_sleep_experiment.py) submits exactly one
action, records its boot/run cookie, closes USB for the bounded window, rediscovers
the same unique USB identity and queries the retained result. It never automatically
resubmits an action after transport loss. Timer fallback is a separate outcome and
cannot satisfy a requested motion wake. GPIO success requires matching wake cause,
pre-ACK pin level and sensor motion status, with clean recovery in every layer.

# Validation

Normal, power and factory firmware builds passed, with profile isolation verified.
C formatting, clang-tidy, Cppcheck and tracked-source size checks passed; the new
untracked sleep source was additionally formatted, Cppchecked and size-checked
explicitly. Five station test methods passed, including rejection of mismatched
wake cause/pin/status, timer under-duration, stale identity and cleanup failures.
No portable C or core code changed, so core/sanitizer suites were not rerun.
The user has no external current meter, so no current or energy claim is possible.
An already-active input is handled in source but has not yet been physically induced.

# First timer trial: USB recovery unresolved

Evidence is under ignored `build/imu-sleep-001`, based on `ab2b6f7` plus the
retained working-tree patch/new sources and image/configuration hashes. Port
rediscovery found USB serial 90:70:69:FE:21:DC at `/dev/cu.usbmodem2101`, and the
pre-flash capability reply identified normal `game` firmware on SKU31261. The
power image flashed with verified transfer hashes and reported profile `power`.
No complete startup transcript was captured for this deployment, so PSRAM/startup
milestones are not separately re-established by that capability response.

Immediately before the trial, display diagnostics reported three checks,
zero mismatches/transfer failures, intact buffers/guards/heap and 1,296 bytes of
minimum main-task stack reserve. Network was disabled, disconnected and idle.
One `sleep-int2` action was accepted as run 1, boot 1310838260. After the station's
bounded wait, USB identity was absent; a fresh port scan still found no board.
No second action was submitted and no timer/GPIO wake success is claimed.

The pinned ESP-IDF documentation at
`.tools/esp-idf/docs/en/api-guides/usb-serial-jtag-console.rst`, Light-sleep section,
explains that the host may fail to re-enumerate after manual light sleep because
D+ stays pulled up. Its stated recovery is physically reconnecting USB. This is
a plausible explanation, not proof that this specimen resumed: operator display
observation and retained result retrieval remain required. The operator was asked
whether the display is animating, dark or frozen, with no reset yet, to preserve
evidence where possible.

The station now has `--recover`: it validates the original USB identity, loads the
single accepted boot/run cookie and requests only retained results and health.
It does not start another trial. The first run's identity manifest was added from
the recorded discovery after that runner enhancement. If reconnecting resets the
MCU, cookie validation rejects the lost result; do not infer success from reboot.
Normal firmware is built but cannot be restored while the USB device is absent.
The serial client is closed. Pickup, physical already-active handling, recovery
appearance/touch and current evidence remain unverified.

Documentation check/repair passed for 82 records; no repairs were needed.

# Retained timer result recovered after cable reconnect

The user replugged USB. Fresh discovery recovered the same serial identity at
`/dev/cu.usbmodem2101`. The station's read-only `--recover` invocation retrieved
run 1 with the original boot cookie 1310838260, establishing that this result
survived without an MCU reboot. No sleep action was repeated.

The outcome was `timer_fallback`: wake cause 4, immediate wake level 0, STATUS1
0 with valid status, and 9,991,570 us measured around the sleep API. Total trial
time was 12,776,399 us. Sensor, wake-source cleanup and host-restoration errors
were all zero; the original register snapshot was restored and the trial was
not faulted. This establishes bounded timer return on this specimen. It does
not establish GPIO-triggered wake or current savings.

Post-reconnect display diagnostics reported 278 checks, zero mismatches and
transfer failures, intact buffers/guards/heap, and 768 bytes of minimum main-task
stack reserve. The lower reserve is measured evidence from this diagnostic path,
not a general stack-margin guarantee. Network remained disabled and idle. Physical
appearance/touch after the sleep cycle was not confirmed by the user. The USB
recovery symptom is consistent with the pinned SDK's documented host
re-enumeration limitation; cable reconnection recovered the original result.

Normal firmware subsequently passed profile isolation and flashed with verified
transfer hashes. The restored board reported profile `game`, two display checks,
zero mismatches/transfer failures, intact buffers/guards/heap and 1,404 bytes of
minimum main-task stack reserve. Clock trust remains unknown. Serial is closed.
No code changed during this recovery; firmware/host tests were not rerun.
Documentation check/repair passed for 82 records with no repairs, and
`git diff --check` passed. The next physical gate is a separately confirmed
pickup window with the power image prepared before operator readiness.

# Pickup-window preparation

On the user's continuation, rediscovery found the same USB identity and normal
`game` profile. The prepared power image matched the earlier SHA-256 exactly,
passed profile isolation and flashed with verified transfer hashes. Serial
inspection confirmed profile `power`, IMU result `idle`, disabled/idle network,
three display checks, intact buffers/guards/heap, zero mismatches/transfer
failures and 1,296 bytes of minimum main-task stack reserve. The post-flash
capture did not include the early boot milestones; no new claim about that
startup transcript is made. Evidence is in `pickup-preparation.json`, its serial
log and `flash-pickup-preparation.log` under the existing evidence directory.

The power image is prepared but no motion sleep command has been submitted.
Serial is closed while awaiting explicit operator readiness. After readiness,
the accepted trial prompt will instruct pickup only once the display goes dark,
within the bounded sleep window. Separate operator stimulus and physical
recovery confirmation remain required. If USB does not reconnect automatically,
retrieve this trial's retained cookie after cable reconnection before repeating
or flashing. Normal firmware remains available for session-end restoration.
Documentation check/repair passed for 82 records without repairs; no code changed.

# Operator-ready GPIO wake trial

After the user's explicit “ready for sleep pickup”, rediscovery confirmed the
same USB identity and exactly one sleep action was submitted. The operator was
prompted to lift/tilt after the screen went dark. The station retrieved run 1,
boot 3080690582 without needing a cable reconnect and classified it
`gpio_motion_wake`. The MCU sleep API elapsed 5,733,517 us; total trial duration
was 8,339,217 us. Wake cause was GPIO (7), immediate wake level was high (1),
and STATUS1 contained the motion bit (04). Initial level was low. The later
`observed_level` after host recovery was low; the immediate `wake_level` is the
wake observation, as specified by the runner contract.

Sensor, wake-source cleanup and host-restoration errors were all zero. The
baseline register snapshot was restored, all command handshakes completed and
the trial was not faulted. Display diagnostics reported 22 checks, intact
buffers/guards/heap, zero mismatches/transfer failures and 688 bytes of minimum
main-task stack reserve. That reserve is narrower than the timer trial's measured
reserve; broader reliability and stack-margin qualification remain open. Network
remained disabled and idle. Evidence is under `build/imu-sleep-001/pickup`.

This provides device-side evidence of GPIO-triggered MCU light-sleep return
before timer fallback. The user separately confirmed lifting/tilting during the
window and normal display appearance and short menu taps after wake. These
observations are retained in `pickup/observer.json`. No current was measured. Already-active-level physical
handling and broader repeatability remain separate gates.

Normal firmware passed isolation and flashed with verified transfer hashes after
the trial. Debug confirmed profile `game`, four display checks, intact
buffers/guards/heap, zero mismatches/transfer failures and 1,420 bytes of minimum
main-task stack reserve. Clock trust remains unknown. Serial is closed. No code
changed during the operator trial, so build/host suites were not rerun.
Documentation check/repair passed for 82 records without repairs and
`git diff --check` passed. This is one confirmed motion wake, not long-run
reliability or approval of a product sleep policy.

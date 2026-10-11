# Device optimization validation passes

Use the factory station, shared `@J1` transport and retained run results as the
measurement contract. The factory image handles isolated board checks; the
power profile supplies the same transport when an experiment needs the real
game/display/input pipeline. Reuse board probes across those profiles. Do not
reimplement the game renderer or input scheduler in a factory-only imitation.

## Run contract

Every experiment records a stable case ID, hypothesis, board revision, device
MAC, ELF and image hashes, source/configuration identity, station/fixture, power
source, stimulus, raw evidence, bounded deadline, cleanup and interpretation.
Keep three decisions separate: check executed successfully, hypothesis improved,
and manufacturing acceptance. None currently grants manufacturing acceptance.
An unavailable fixture is a skip. A read failure is an error, not an absent
battery or zero voltage. A timer fallback is not successful peripheral wake.

For A/B trials capture A, B, then restored A under the same stimulus. Start with
10 repetitions for repeatability; expand to 100 only after basic correctness.
Report sample count, median, p95 and worst case for latency; missed/duplicate
inputs, wake failures and restoration errors must remain zero in that sample.
These sample sizes and zero-error gates are experiment criteria, not production
reliability certification. Energy claims require an external current/charge
instrument with USB conditions controlled. Record instrument range and uncertainty.

Never change multiple clocks, timers, modes or rails in one treatment. A failed
cleanup aborts subsequent trials and triggers recovery to the recorded baseline.
After a reset, re-discover identity and capabilities; never retry an ambiguous
state-changing command automatically. Restore normal firmware and verify its
profile/state after each bench session. Keep binary/config hashes and serial
captures under a unique ignored `build/` experiment directory.

## Ranked opportunities

Impact is expected user benefit, not a measured speedup or battery-life estimate.
Dependencies can run earlier than their priority: read-only inventory is pass 0.
All cases below beyond the inventory IDs are proposed experiments, not implemented
factory commands.

| Rank / case | Hypothesis and A/B | Required measurement and gate | Prerequisite / cleanup |
| --- | --- | --- | --- |
| 1 `touch.latency` | 33 ms input timer adds delay; compare 16 then 10 ms separately | IRQ→report→gesture→engine→transfer timestamps; lower p95, no lost short tap/swipe, idle wake cost recorded | Real power-profile input path, physical touch fixture; restore timer |
| 1 `touch.recovery` | Cached pressed data survives failed reads | Inject bounded read/ACK failures; no synthesized tap or stuck press, next valid gesture works, recovery duration bounded | Test adapter hook and cancellation semantics; remove fault injection and verify release |
| 2 `display.sleep` | Plain sleep can wake faster than deep standby | Existing ~1.36 s baseline versus explicit ordinary-sleep option; latency, physical image/brightness, current and 10 clean restores | Qualified driver option, panel observer and current fixture; restore full initialization/frame |
| 2 `display.brightness` | Lower hardware brightness reduces sleeping-screen energy | Fixed scene at recorded baseline then lower level; current plus readability/color checks | Meter and image observer; restore brightness |
| 3 `touch.wake` | Physical touch can reliably wake retained sleep | Short tap, held-before-entry, touch-during-entry, release-during-restore, repeated wake; actual source, level, edge count and latency | Physical fixture; timer fallback distinctly recorded, restore controller/panel |
| 3 `imu.motion_wake` | Accelerometer-only pickup wake permits quiet sleep | INT2/GPIO21; quiet desk vs pickup, false/missed wakes, wake cause, current | Identity/config baseline, documented CTRL9 handshake; capture status before acknowledgement, restore controls |
| 3 `rtc.resume` | Long sleep preserves correct pet history | Deterministic overnight/boundary/24 h cap/backward/unknown/interrupted save cases, then station-timed hardware gap | Existing trusted-time checks and bounded recovery; preserve saves, do not silently set time |
| 4 `cpu.spans` | Long RGB565 fill/blend/tint can benefit from S3 PIE | Scalar equivalence, clipped/unaligned guards, all alpha endpoints, per-stage and total scene timing | Attribute real render cost first; scalar fallback and no SIMD in portable core |
| 4 `display.staging` | Fused swap/copy reduces memory traffic | Compare real PSRAM→DMA strips, exact pixels, completion latency and CPU time | Disable original swap exactly once; retain buffer/DMA ownership, restore adapter configuration |
| 5 `display.te` | Synchronizing suitable dirty updates improves tearing | Measure TE and transfers, visual tearing, latency and bytes; full-frame wire time remains ~21.7 ms at 40 MHz | Verify wired TE GPIO and pulse timing; restore scheduling, no wait per strip |
| 5 `cpu.config` | Clock/cache/affinity improves the identified bottleneck | One setting per build; latency, internal RAM/stack, audio/network load and energy | Preserve single-threaded engine; restore config and image |
| 6 `peripheral.idle` | Unused IMU and RTC clock output consume avoidable energy | Inspect first; one reversible power-down/clock-output change, readback and external current | No claim if already configured; preserve RTC trust and other control bits |
| 6 `pmic.telemetry` | Existing PMU settings and telemetry can guide power work | Inventory then enabled voltage channels versus DMM; source events preserved before W1C ack | No inferred calibrated current/SOC; leave charging and gauge model untouched |
| 7 `pmic.rails` | Confirmed unused regulators/audio domain can be gated | External current, bus recovery, touch/display/audio and RTC retention checks | Fitted-board load/back-power map; never gate shared VCC3V3, restore every register |
| 7 `pmic.sleep` | Qualified PMIC policy can improve deeper standby | Power/reset/wake trace, save recovery and retained RTC | PWROK/CHIP_PU and actual wake route established first; recovery fixture |
| 8 `touch.protocol` | ACK/mode handling may improve qualified controller behavior | Firmware identity, bus trace; independent ACK/restoration trials | CST9217 project-specific vendor evidence; do not transplant family firmware |
| 8 `display.partial` / `touch.gesture` / `imu.interaction` | Small sleep display or semantic gestures improve product behavior | Product-specific quality, input correctness and energy against existing game | Explicit interaction design; do not enable eight-color idle or continuous gyro by default |

## Pass 0: available now

Factory schema 3 adds `pmic.inventory`, `imu.identity` and `rtc.snapshot` to the
existing smoke suite. Run the [build/flash/capture workflow](factory-validation.md).
Retain two independent captures to inspect unchanged controls and RTC progression.
Do not call a plausible/ticking calendar trusted UTC: the station has not read
application trust metadata or established a time reference through this probe.

The PMU inventory includes enable/configuration and status, without changing
rail or charging policy. The IMU captures identity/revision and configuration,
excluding clear-on-read status. RTC captures controls/calendar coherently and
its timer configuration without setting time. Results are narrow transaction
checks; external battery voltage, motion, retention, image quality and current
remain unverified. Inventory timeout/identity failures block dependent writes.

Maintain test definitions beside their board probes and station validators.
Use portable tests for transcript rejection and arithmetic/renderer correctness;
use firmware builds and isolation checks for every affected profile. Promote
only measured production fixes into normal firmware. Preserve successful probes
as factory regression checks with fixture requirements and explicit criteria.

Research basis: [memo-047](../docs-cms/memos/memo-047-device-capabilities-and-experiment-backlog.md).

## Touch recovery and scheduling pass

This pass uses the power image because the actual LVGL adapter, canvas and game
input contract are dependencies. It uses the same USB station path as factory
inventory. It does not add a separate renderer or fake core to factory firmware.

```sh
./scripts/esp validate
./scripts/esp ports
./scripts/esp validate -p "$JELLI_DEBUG_PORT" flash
./scripts/uv run --python 3.12 tools/debug/touch_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/touch-run-001 --repetitions 10
make esp-build
make esp-flash PORT="$JELLI_DEBUG_PORT"
```

The script checks the power profile, records every request/reply and raw serial,
then runs legacy handling, error propagation, guarded recovery at 33 ms, guarded
recovery at 16 and 10 ms, and restored 33 ms. Every case sends eight scripted
samples through the adapter's task-context IRQ notification and custom-read API:
idle, press, failed read, continued hold, release, fresh press, release, idle.
Synthetic events use the existing gesture functions and never enter the live
game's queue. Baseline cases are expected to demonstrate false taps; a successful
experiment means the observed behavior matched the hypothesis, not that those
control implementations passed a product correctness gate.

Each case has a three-second device deadline and five-second station deadline,
a monotonically advancing run ID, retained result, and automatic restoration to
the board's configured input period. Touch and sleep/reset trials cannot overlap.
Physical IRQ activity marks a trial contaminated. A station error stops the
matrix without retrying an action; the device still performs bounded cleanup.
Results include consumed samples, valid/false tap counts, press/release events,
and eight notification-to-read delays. The station rejects stale, contaminated,
partial or unrestored results.

The production touch guard cancels a gesture on read error before LVGL's forced
release, then ignores continued contact until a valid zero-contact report. The
experiment calls this same guard with a diagnostic gesture instance. It tests
the LVGL event/recovery contract, not the controller's real ACK errors, I2C noise,
reset timing or physical release interrupts. Those still require physical tests.
A failed report must not be treated as a successful release or the start of a
new valid contact. Recovery can intentionally discard a contact until a valid
release is seen.

Timer results are software-notification-to-adapter-read measurements, not physical
IRQ-to-visible response. They exclude controller scanning and I2C time, include
main-loop notification timing and LVGL scheduling, and do not establish idle
energy cost. Production retains its 33 ms input period pending physical and power
measurements. The normal image contains the guard but no synthetic runner.

## Display transitions and brightness pass

```sh
./scripts/esp validate
./scripts/esp ports
./scripts/esp validate -p "$JELLI_DEBUG_PORT" flash
./scripts/uv run --python 3.12 tools/debug/panel_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/panel-run-001 --repetitions 10
make esp-build
make esp-flash PORT="$JELLI_DEBUG_PORT"
```

The matrix covers baseline, requested 30%, 10%, black, display-off, ordinary
sleep, deep standby, and restored baseline. A trial pauses LVGL, sends a checked
blocking brightness command to drain preceding SPI colors, enters its treatment,
holds for 200 ms, restores panel state/brightness, resumes LVGL and requests a
full repaint. Engine elapsed time excludes this diagnostic hold and queued
input is discarded. The fixture should remain untouched during the run.

`power panel run MODE` queues one treatment and returns boot/run identifiers;
`power panel result` reads the retained result. The host validates identity,
mode, entry/restoration errors, requested brightness and timing. It never retries
an action. Display guard counters are checked after their five-second update
interval; the final logical framebuffer is saved separately from physical evidence.
A failed transition/restoration latches the panel experiment against more runs
until reboot. Touch and older power trials cannot overlap this queued trial.

Ordinary sleep is a validation-only policy seam in the pinned CO5300 driver.
`ports/esp32/cmake/PanelSleepTrial.cmake` verifies the 2.2.0 SPI source SHA256,
creates a build-local copy, and adds one condition to deep-standby entry. The
existing driver continues to own sleep/deep-standby/reset/init flags. Its normal
SLPIN/SLPOUT path supplies the existing 120 ms waits. Hardware reset remains
configured, the module's two 600 ms initialization waits remain unchanged, and
no private driver structure is cast or copied. Default policy is deep standby;
only the bounded ordinary-sleep trial changes it, then restores it. Normal and
factory profiles compile the original source. A changed dependency fails
configuration until reviewed. No managed dependency source is edited.

The public CO5300 brightness setter reports SPI errors; the BSP setter does not.
Trials use the checked API and restore the startup baseline (currently 60%),
without round-tripping each treatment through the BSP's truncated getter. The
reported percentages are requested software state, not panel register readback,
measured luminance or energy. BSP cached brightness remains at the baseline.

Entry/recovery timing ends at command completion, not the first visible frame.
Serial success and matching logical framebuffer cannot establish visible wake,
color correctness or lower power. Record observer feedback and external current
measurements as separate evidence. Neither the ordinary-sleep policy nor new
brightness defaults are enabled in normal firmware by this pass.

## RTC progression prerequisite

After building and flashing the factory image, run:

```sh
./scripts/uv run --python 3.12 tools/debug/rtc_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/rtc-run-001 --repetitions 10
```

This station runner reuses the complete factory suite and shared client. Eleven
snapshots bound ten intervals using station monotonic time. Calendar differences
must fit the request windows plus one second of RTC quantization. Invalid BCD,
stopped/test/12-hour modes, nonpositive progression, changed controls or OS flag,
and changed device/image/boot or stale runs abort the pass. Raw replies survive
failure; actions are never retried automatically. Restore normal firmware using
the standard flash command, then query `clock` and `display`.

A progression pass proves only that the calendar advanced within these loose
bounds. It does not measure oscillator accuracy, retention, UTC trust or pet
catch-up. The runner leaves time and the oscillator-stop flag untouched. Clock
trust still requires the application's explicit time-sync path and persisted
trust metadata. See [memo-051](../docs-cms/memos/memo-051-rtc-progression-validation.md).

## IMU command and interrupt qualification

Build/flash the power profile, then use the shared station client:

```sh
./scripts/uv run --python 3.12 tools/debug/imu_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/imu-identity-001 --repetitions 10 identity
./scripts/uv run --python 3.12 tools/debug/imu_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/imu-quiet-001 --repetitions 10 quiet-int2
./scripts/uv run --python 3.12 tools/debug/imu_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/imu-motion-001 --repetitions 1 motion-int2
```

Keep the board still for quiet cases. For a motion case, wait for `ARMED`, then
pick up the board within 20 seconds. Retain whether the observer actually supplied
the stimulus. No-event without confirmed stimulus is inconclusive. The runner
never sleeps the MCU; awake motion qualification precedes wake testing.

`power imu run MODE` and `power imu result` use boot/run correlation and retained
results. Identity requires WHO=05, revision=7c and known idle controls. COPY_USID
checks the newer STATUSINT.bit7/CTRL9 ACK handshake. Each phase has a 200 ms
deadline and bounded polling; bus errors stop the operation. No reset or
calibration command is used. One I2C device is allocated at startup, with an
80-byte probe budget, a 1200-byte static report and small runner state. Trials
allocate no heap memory. Normal and factory profiles exclude this active probe.

The `quiet` and `motion` modes preserve CTRL1=20. Explicit `quiet-int2` and
`motion-int2` comparators set CTRL1.bit4, following the exact-board vendor's
SensorLib WoM path and its bundled QMI8658A datasheet. They require the observed
firmware bytes 1e0301. This is not proof of the fitted A/C suffix: the C Rev A
datasheet calls that bit reserved. Do not silently promote this comparator to
normal firmware or extend it to unqualified revisions.

Motion setup uses 21 Hz accelerometer-only sampling, +/-2g, 128 mg slope threshold,
eight blanked samples and INT2 idle low. DRDY output is disabled so it cannot
masquerade as motion. Register readback must match; the runner settles for one
second, then requires idle low. Quiet observation lasts two seconds; pickup has
a 20-second deadline. GPIO is sampled before the single clear-on-read STATUS1
acknowledgement. A brief input-only weak-pull diagnostic follows, restoring both
pulls disabled. Following both pulls supports a high-impedance path, not continuity
or package identity. The game continues during the bounded awake observation.

Cleanup disables sensors, waits 100 ms, explicitly exits hidden WoM state through
zero-threshold command08, restores modified controls/CAL bytes, and compares all
saved register readbacks. An operation/cleanup failure latches the runner; a
verified register match alone cannot excuse a failed WoM exit command. Do not
assume an MCU reset resets the independently powered sensor. Power, panel, touch
and IMU trials reject overlapping runs. After the session restore normal firmware
and check its profile/display state. No current or manufacturing acceptance is
inferred from these checks.

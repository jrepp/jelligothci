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
The case IDs below name experiments, not necessarily factory commands. Several
now have power-profile implementations; use the status review and workflow
sections below to distinguish available diagnostics from outstanding work.

| Rank / case | Hypothesis and A/B | Required measurement and gate | Prerequisite / cleanup |
| --- | --- | --- | --- |
| 1 `touch.latency` | 33 ms input timer adds delay; compare 16 then 10 ms separately | IRQ→report→gesture→engine→transfer timestamps; lower p95, no lost short tap/swipe, idle wake cost recorded | Real power-profile input path, physical touch fixture; restore timer |
| 1 `touch.recovery` | Cached pressed data survives failed reads | Inject bounded read/ACK failures; no synthesized tap or stuck press, next valid gesture works, recovery duration bounded | Test adapter hook and cancellation semantics; remove fault injection and verify release |
| 2 `display.sleep` | Plain sleep can wake faster than deep standby | Existing ~1.36 s baseline versus explicit ordinary-sleep option; latency, physical image/brightness, current and 10 clean restores | Qualified driver option, panel observer and current fixture; restore full initialization/frame |
| 2 `display.brightness` | Lower hardware brightness reduces sleeping-screen energy | Fixed scene at recorded baseline then lower level; current plus readability/color checks | Meter and image observer; restore brightness |
| 3 `touch.wake` | Physical touch can reliably wake retained sleep | Short tap, held-before-entry, touch-during-entry, release-during-restore, repeated wake; actual source, level, edge count and latency | Physical fixture; timer fallback distinctly recorded, restore controller/panel |
| 3 `imu.motion_wake` | Accelerometer-only pickup wake permits quiet sleep | INT2/GPIO21; quiet desk vs pickup, false/missed wakes, wake cause, current | Identity/config baseline, documented CTRL9 handshake; capture status before acknowledgement, restore controls |
| 3 `rtc.resume` | Long sleep preserves correct pet history | Deterministic overnight/boundary/24 h cap/backward/unknown/interrupted save cases, then station-timed hardware gap | Existing trusted-time checks and bounded recovery; preserve saves, do not silently set time |
| 4 `cpu.spans` | Long RGB565 fill/blend/tint can benefit from S3 PIE | Scalar equivalence, clipped/unaligned guards, all alpha endpoints, per-stage and total scene timing | [Raw spans](../docs-cms/memos/memo-062-simd-fill-and-packed-blend-device-benchmarks.md) and [clipped matched controls](../docs-cms/memos/memo-063-clipped-rectangle-simd-device-trial.md) measured; no useful SIMD rectangle gain; masked blend and scalar clipping remain |
| 4 `display.staging` | Fused swap/copy reduces memory traffic | Compare real PSRAM→DMA strips, exact pixels, completion latency and CPU time | Disable original swap exactly once; retain buffer/DMA ownership, restore adapter configuration |
| 5 `display.te` | Synchronizing suitable dirty updates improves tearing | Measure TE and transfers, visual tearing, latency and bytes; full-frame wire time remains ~21.7 ms at 40 MHz | Verify wired TE GPIO and pulse timing; restore scheduling, no wait per strip |
| 5 `cpu.config` | Clock/cache/affinity improves the identified bottleneck | One setting per build; latency, internal RAM/stack, audio/network load and energy | Preserve single-threaded engine; restore config and image |
| 6 `peripheral.idle` | Unused IMU and RTC clock output consume avoidable energy | Inspect first; one reversible power-down/clock-output change, readback and external current | No claim if already configured; preserve RTC trust and other control bits |
| 6 `pmic.telemetry` | Existing PMU settings and telemetry can guide power work | Inventory then enabled voltage channels versus DMM; source events preserved before W1C ack | No inferred calibrated current/SOC; leave charging and gauge model untouched |
| 7 `pmic.rails` | Confirmed unused regulators/audio domain can be gated | External current, bus recovery, touch/display/audio and RTC retention checks | Fitted-board load/back-power map; never gate shared VCC3V3, restore every register |
| 7 `pmic.sleep` | Qualified PMIC policy can improve deeper standby | Power/reset/wake trace, save recovery and retained RTC | PWROK/CHIP_PU and actual wake route established first; recovery fixture |
| 8 `touch.protocol` | ACK/mode handling may improve qualified controller behavior | Firmware identity, bus trace; independent ACK/restoration trials | CST9217 project-specific vendor evidence; do not transplant family firmware |
| 8 `display.partial` / `touch.gesture` / `imu.interaction` | Small sleep display or semantic gestures improve product behavior | Product-specific quality, input correctness and energy against existing game | Explicit interaction design; do not enable eight-color idle or continuous gyro by default |

## Implementation status and next work

Source review at `ab2b6f7` separates missing product behavior from existing
diagnostics. Profile inclusion is owned by
[the ESP32 build definition](../ports/esp32/main/CMakeLists.txt).
Historical experiment results do not establish the current board state.
The subsequent [test-bed pass](../docs-cms/memos/memo-059-testbed-recovery-and-pipeline-attribution.md)
records startup recovery, successful completion captures, new render/LVGL timing,
and a slower fused-staging candidate. The table distinguishes those implemented
capabilities from remaining gates.

| Area | Implemented foundation | Outstanding increment or gate |
| --- | --- | --- |
| Automatic screen/device sleep | Explicit bounded [power trials](../ports/esp32/main/validation/power/power_diag.c) | Host-owned automatic entry/resume policy, coordinated display/audio/network handling, trusted elapsed-time recovery and physical wake qualification |
| Touch recovery and latency | Production [gesture cancellation guard](../core/touch_guard.c) and [synthetic trials](../ports/esp32/main/validation/power/touch_trial.c) | Physical short-tap/swipe and fault recovery evidence; input-to-visible instrumentation and idle-current comparison before changing the production timer |
| Panel sleep and brightness | [Panel trial matrix](../ports/esp32/main/validation/power/panel_trial.c) with ordinary-sleep comparator | Product brightness/sleep policy and repeated physical restoration/current measurements |
| Touch wake | GPIO/timer sleep diagnostic in the power trials | Physical entry/held-contact/release matrix; reliable peripheral wake before automatic policy |
| IMU wake and interaction | [Awake command/INT2 qualification](../ports/esp32/main/validation/power/imu_trial.c) and [confirmed two-pickup/idle return](../docs-cms/memos/memo-060-imu-interrupt-rearm-prerequisite.md#confirmed-two-pickup-result), plus [timer and confirmed GPIO MCU wake](../docs-cms/memos/memo-061-bounded-imu-light-sleep-trial.md#operator-ready-gpio-wake-trial) | Already-active-level physical check, sleep-path stack margin and broader repeatability; gameplay interaction remains a separate design increment |
| RTC and long resume | [RTC adapter](../ports/esp32/main/rtc_clock.c), [progression station](../tools/debug/rtc_experiment.py), [overnight resume tests](../tests/test_sleep_resume.c) | Trusted station-timed sleep gap, power-loss retention and host sleep integration; ticking alone does not establish UTC trust |
| Pipeline and staging | [Profiler](../ports/esp32/main/pipeline_profile.c), [station capture](../tools/debug/pipeline_experiment.py) and [fused staging A/B](../ports/esp32/main/staging_benchmark.c) | Physical input-to-visible attribution; [empty-overlay candidate](../docs-cms/memos/memo-071-lvgl-dispatch-and-empty-overlay-trial.md) needs uninstrumented timing and output/physical verification; fused scalar staging remains disabled |
| CPU spans, clock/cache/affinity and TE | Ranked hypotheses above | Raw fill/blend and clipped-rectangle trials are recorded in [memo-062](../docs-cms/memos/memo-062-simd-fill-and-packed-blend-device-benchmarks.md) and [memo-063](../docs-cms/memos/memo-063-clipped-rectangle-simd-device-trial.md); masked blends, scalar clipping and controlled clock/cache/affinity/TE experiments remain |
| PMIC and peripheral idle | [Read-only PMIC probe](../ports/esp32/main/validation/boards/pmic_probe.c) and [board inventory](../ports/esp32/main/validation/boards/inventory.c) | Meter comparison, reversible idle experiments, fitted-board rail/back-power map, then qualified rail/sleep policy |
| Touch modes, gestures and panel partial mode | Capability research in [memo-047](../docs-cms/memos/memo-047-device-capabilities-and-experiment-backlog.md) | Module-specific protocol evidence and explicit interaction requirements before implementation |
| Wi-Fi setup and network debug | USB-configured [station/time service](../ports/esp32/main/network_worker.c) and [OTA](../ports/esp32/main/network_ota.c) | Settings entry, temporary AP/browser provisioning and authenticated network debug remain in [Draft RFC-003](../docs-cms/rfcs/rfc-003-wifi-provisioning-and-network-debug.md); existing network/OTA device acceptance also remains |

The recovery and pipeline prerequisite is now implemented and exercised. On each
new session, rediscover identity/profile and verify healthy display counters;
use [memo-059](../docs-cms/memos/memo-059-testbed-recovery-and-pipeline-attribution.md)
for the dated result, not an assumption about current board state.

Next, qualify physical pickup/touch wake and investigate the measured LVGL/full
update cost before selecting an arithmetic optimization. Preserve image/config
identity and restore normal firmware after each bench session. An external
current fixture is still required for energy comparisons. Automatic sleep and AP
provisioning remain larger increments with policy/design dependencies; the
[initial follow-up](../docs-cms/memos/memo-058-planned-hardware-follow-up.md) did
not approve their draft defaults.

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

For remotely coordinated operator trials, prepare and verify the image first,
then wait for an explicit operator-ready message before issuing the run command.
Announce the armed window promptly and obtain a separate confirmation that the
pickup occurred inside it. Availability or readiness is not stimulus evidence.
If the operator steps away, leave the trial unarmed; do not spend the observation
window waiting for their return. Restore normal firmware when the bench session
finishes.

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


### Two-pickup interrupt rearm prerequisite

After one confirmed pickup, use the same awake test bed to check acknowledgement
and a second event without reconfiguring the IMU:

```sh
./scripts/uv run --python 3.12 tools/debug/imu_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/imu-rearm-001/observed \
  --repetitions 1 rearm-int2
```

Prepare the image first and wait for explicit operator readiness. Each numbered
`ARMED` prompt opens a bounded pickup window. Lift, tilt and promptly set the board
down, then leave it still until the next prompt. The device allows a settling
interval before one STATUS1 read, then requires sampled idle before arming the
second window. Both events need GPIO and motion-status evidence, both idle
observations and successful final cleanup. Obtain confirmation that both pickups
occurred in their respective windows. The station disallows automatic multi-run
repetition for this operator-coordinated mode.

Timing bounds and fixed state live in
[imu_trial.c](../ports/esp32/main/validation/power/imu_trial.c). Results add
`completed_events`, `motion_levels`, `motion_status`, `ack_levels`, `idle_levels`
and `ack_wait_us`. These arrays are authoritative for the two-event mode; the
older scalar pin fields are retained for compatibility with single-event modes.
An idle level of -1 means not established. ACK timing includes the sampled stable
interval; it is not electrical edge latency. Polling cannot exclude short pulses
between samples. Missing events, failure to return to sampled idle, read errors
or failed cleanup stop the sequence and latch the trial against new runs until
reboot. The MCU remains awake throughout. See
[memo-060](../docs-cms/memos/memo-060-imu-interrupt-rearm-prerequisite.md).

### Bounded IMU MCU sleep trial

After the awake rearm prerequisite, use the power image for one explicit sleep
trial. First leave the board still and qualify timer recovery:

```sh
./scripts/uv run --python 3.12 tools/debug/imu_sleep_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/imu-sleep-001/timer \
  --expect timer_fallback
```

For motion, prepare and inspect the image before asking for readiness. Wait for an
explicit ready response, then run the same command with a fresh evidence directory
and `--expect gpio_motion_wake`. Immediately announce the accepted trial; the
operator lifts/tilts only after the display goes dark, within the configured window.
Obtain a separate confirmation that the stimulus occurred during that window and
check physical display/touch restoration. An accepted request is not proof the MCU
entered sleep. Never spend the observation window waiting for the operator to return.

Timing and wake-source configuration live in
[imu_sleep.c](../ports/esp32/main/validation/power/imu_sleep.c). The station closes
USB during sleep, then checks the retained boot/run cookie. A timer outcome cannot
pass a requested pickup test. Read `wake_level` for the immediate pre-ACK observation;
`observed_level` is sampled later, after host recovery. Already-active levels skip
sleep, and operation, wake cleanup, host recovery and sensor cleanup failures remain
visible. Do not automatically repeat failed or ambiguous actions. If USB stays absent,
observe the display before resetting; the SDK documents that cable reconnection
may be needed after manual light sleep. Once the same identity is available,
add `--recover` to the original station command to query its retained result
without another action. A changed boot cookie invalidates the original trial.
Restore normal
firmware after the session. See
[memo-061](../docs-cms/memos/memo-061-bounded-imu-light-sleep-trial.md) for evidence and
remaining physical gates; this diagnostic does not establish current savings or
an automatic sleep policy.

## Graphics span performance pass

The power test image provides `display benchmark-spans`. It compares scalar fill
with an S3 SIMD candidate and component-wise RGB565 blending with an exact packed
integer candidate. Both run against internal scratch and borrowed PSRAM canvas;
normal/factory images exclude these kernels. Run:

```sh
./scripts/uv run --python 3.12 tools/debug/span_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/graphics-spans-001/measured --runs 5
```

The runner checks correctness/timing metadata and post-test display health.
Workload sizes, alpha coverage and the fixed memory budget live in
[span_benchmark.c](../ports/esp32/main/span_benchmark.c). Timings alternate methods
across alignment offsets and exclude initialization/guard comparisons. The flat
`us` array is ordered memory, operation, then size; each pair is scalar/candidate.
`endpoint_us` is ordered memory then transparent/opaque alpha. These are raw
constant-color span measurements, not panel FPS or a production renderer change.
See [memo-062](../docs-cms/memos/memo-062-simd-fill-and-packed-blend-device-benchmarks.md)
for results, short-span regressions and promotion gates. Restore normal firmware
after the session.

### Clipped rectangle SIMD follow-up

Use the power-only `display benchmark-simd-rect` and `display benchmark-control-rect`
commands through the paired station runner:

```sh
./scripts/uv run --python 3.12 tools/debug/rect_simd_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/graphics-rect-001/controlled --runs 5
```

Each command checks an exact full-canvas single-rectangle oracle, then compares
workload checksums and timings with the current production scanline renderer.
The two commands execute identical candidate rectangle code, selecting SIMD or
scalar only for the long-span call. The station alternates command order and
reports both production-relative and control-relative speedups. The scalar
control is needed because even non-SIMD tiny rows can change timing through code
generation/layout. Neither command installs the candidate in the live renderer.
See [memo-063](../docs-cms/memos/memo-063-clipped-rectangle-simd-device-trial.md).

## Pipeline and staging pass

The power profile adds `display profile start`, `display profile stop`, and
`display profile STAGE` through the shared debug protocol. Stage names live in
[the profiler](../ports/esp32/main/pipeline_profile.c) and its
[station runner](../tools/debug/pipeline_experiment.py). Normal firmware rejects these commands.
Capture the current scene with:

```sh
./scripts/uv run --python 3.12 tools/debug/pipeline_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/pipeline-run-001 \
  --scene home-idle --seconds 10
```

Captures expire on the device after 60 seconds; the station accepts 6–30 second
windows and rejects expired results.

Build and flash the power image first using the existing procedure. Keep the
flashed binary/configuration hashes and source patch with the capture. Stop on
missing completion/render samples or unhealthy display counters. The stages have
independent sample windows; percentiles use the last 128 samples, while totals
and maxima cover the full run. Engine includes simulation, input and rendering.
Transfer ends at SPI completion, not visible scanout. `render` wraps the actual
portable pet renderer through a power-only linker option. `lvgl` measures LVGL's
render-start to render-ready interval, including dirty-area drawing, flush
submission and waits for earlier strips; it is not pure LVGL CPU time. These
stages overlap other timings and must not be summed. Physical input-to-visible
response remains unmeasured. No energy inference is valid without an external meter.

For repeatable full-update pressure, pass `--refresh-ms 500` (or `1000`).
This invalidates the current canvas while normal animation continues; it is not
a fixed-frame benchmark. `--memcpy builtin|libc` selects a power-only linker
wrapper after bounded copy/guard qualification. The default is `builtin`; the
station stops profiling and restores that path in cleanup, including failed
captures. If serial cleanup fails, restore normal firmware before ending the
session. This switch is experimental and does not change the normal image.

`lvgl_flush` measures synchronous flush callbacks; `lvgl_wait` measures separate
LVGL wait intervals. `lvgl_work` is the remaining render interval, including
drawing, dispatch, bookkeeping and interrupts. These three totals must account
for `lvgl` exactly; their percentiles must not be added. `lvgl_image` and
`lvgl_fill` wrap the pinned RGB565 image and solid-color draw functions and are
subsets of `lvgl_work`, not additional disjoint intervals. A fully covered scene
may have no fill calls. Profiling adds timing/lock overhead shared by both copy
modes. See [the copy trial](../ports/esp32/main/lvgl_copy_trial.c) and
[memo-064](../docs-cms/memos/memo-064-lvgl-update-cost-and-copy-trial.md) for device
results and limits.

Power-only dispatch probes add `lvgl_tasks`, `lvgl_dispatch` and `lvgl_draw_wait`
as non-overlapping subsets of work. `lvgl_events` contains cover/draw event work,
including task/image/fill time; do not add it to those nested intervals. See
[the wrappers](../ports/esp32/main/lvgl_dispatch_profile.c) for the synchronous
single-draw-unit contract and [memo-071](../docs-cms/memos/memo-071-lvgl-dispatch-and-empty-overlay-trial.md)
for attribution, probe overhead and the rejected root-hidden-flag experiment.

`--overlays baseline|skip` tests layered opacity on guarded empty top/system
layers. The default is baseline. Cleanup independently stops profiling and
restores exact local styles and built-in copy; failed cleanup rejects the run.
Never use root hidden-flag removal as a substitute: it crashed in pinned LVGL.
This mode remains experimental pending uninstrumented timing, final draw-buffer
equivalence and physical verification; fewer events also remove probe overhead.

`display benchmark-staging` is a power-only shared-protocol command. It alternates
16 pairs of LVGL swap-plus-copy versus a fused scalar swap/copy at 36, 466 and
23,300 pixels. It borrows the existing buffers under the display mutex after a
checked SPI drain, verifies every output byte and restores the canvas. It pauses
rendering; do not combine it with a timing capture or another power trial. It is
not a production setting. Host tests cover unaligned buffers, tails and guards.
Device staging measurements rejected the fused candidate; physical scanout
remains separate. See [memo-059](../docs-cms/memos/memo-059-testbed-recovery-and-pipeline-attribution.md).

## Back-buffer memory placement pass

The power-only [memory probe](../ports/esp32/main/display_memory_trial.c) exposes
`display memory` and `display benchmark-memory`. The readout records actual pointer
classification, active LVGL buffer capacity/current stride, staging capacity and
internal heap free/largest/minimum values. The benchmark borrows existing buffers
after draining transfers; it verifies copied bytes and guard regions, then restores
the canvas. Do not combine it with another trial or timing capture.

Use `./scripts/esp validate menuconfig` to select `JELLI_MEMORY_TRIAL` in the
Jelligotchi validation menu: baseline, small PSRAM, or small internal buffers.
The option is restricted to the power image. Build with `./scripts/esp validate`,
record the selected configuration and binary hash, and flash separately using
the established power-image procedure. Normal and factory images exclude the
placement wrapper. The source owns buffer dimensions and fixed scratch limits.

```sh
./scripts/uv run --python 3.12 tools/debug/memory_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/memory-copy-001 --mode 2
./scripts/uv run --python 3.12 tools/debug/pipeline_experiment.py \
  --port "$JELLI_DEBUG_PORT" --output build/memory-pipeline-001 \
  --memory-mode 2 --refresh-ms 500 --scene home-idle --seconds 10
```

Match `--mode` / `--memory-mode` to the flashed setting. The station rejects a
wrong configuration or memory class. The active stride may reflect a narrow dirty
rectangle; allocation capacity is a different value. Report CPU copy timing,
LVGL render timing, SPI completion and physical scanout separately. Setup affects
cache state; the copy probe is not a cold-cache DMA-bandwidth measurement.

Compare same-sized PSRAM/internal buffers before attributing a gain to placement,
and compare both against the baseline before promotion. The initial small-buffer
variants were slower overall, despite faster individual copies. Their main-task
stack margin and internal heap also need attention before broader trials. See
[memo-065](../docs-cms/memos/memo-065-back-buffer-memory-placement-trial.md).
Restore the power setting to baseline and restore normal firmware after the
session, then check profile/display health and close serial. Direct PSRAM DMA
is a separate driver/alignment/cache/lifetime experiment; it is not enabled here.

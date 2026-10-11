---
title: Device capabilities and experiment backlog
author: Engineering Team
created: 2026-10-10T20:09:05Z
tags: [memo, technical]
id: memo-047
project_id: jelligotchi
doc_uuid: 7c24066a-eaa5-44da-a4a0-3721033d7851
---

# Overview

Second research pass for SKU 31261 against the current game, pinned drivers,
and earlier optimization experiments. Three delegated reviews covered touch,
IMU/RTC and ESP32-S3 CPU; the main review covered CO5300 and integration.
This pass changes documentation only. Capabilities and proposed experiments
below are not hardware validation or measured energy savings. Normal game
firmware remains restored following [memo-046](memo-046-shared-factory-debug-path.md).

# Context

Keep reusable experiments in the validation profiles and shared `@J1` command
path described in [memo-045](memo-045-validation-and-factory-firmware.md).
Promote a proven capability into the board adapter only when production needs
it. Keep SDK/register logic out of the portable game and do not initialize every
sensor merely because factory firmware can test it.

Evidence has four separate levels: manufacturer capability, exact-board wiring,
pinned implementation, and bench observation. Only the last can establish
physical responsiveness, wake reliability or power savings. Existing successful
timer wake and serial startup do not establish touch wake or display quality.

# Details

## CST9217: scheduling and recovery before undocumented modes

The current generated configuration uses `LV_DEF_REFR_PERIOD=33`. Main changes
the display timer to 16 ms, but LVGL creates the input timer at 33 ms. The pinned
adapter consumes its IRQ semaphore when that timer runs; ISR notification does
not make the active input timer immediately due. Trace IRQ, report completion,
gesture enqueue, engine consumption and panel completion; compare 33/16/10 ms
input periods independently. Measure idle wakeups alongside latency. Never call
I2C or LVGL directly from ISR.

The pinned touch driver returns early on failed read/ACK before clearing cached
points. The adapter discards the read return and can consume cached pressed
state. Its recovery paths can also incur two reset sequences totaling 170 ms of
waits, plus I2C retries. Since game actions commit on release, fault cancellation
must not synthesize a successful tap. Inject bounded read failures before
optimizing this path; verify recovery, no duplicate action and next valid input.

Waveshare's exact-board SensorLib sends report ACK `D0 00 AB`, reads firmware
identity starting at `D208`, and implements `D105` sleep with reset wake. Its
constants also name `D10F` low power and `D106` disable low-power scan, but its
`idle()` is empty and active mode handling does not implement low power.
The bundled Hynitron v3.5 porting manual corroborates family protocol but does
not list CST9217 in its applicability list. Both pinned and vendor active init
enter `D101` without an explicit `D109` afterward: a discrepancy to investigate,
not proof of a bug or authorization for blind mode writes.

Capture firmware version/checksum and existing bus/IRQ behavior first. Qualify
ACK and mode changes independently against project 5734 with vendor guidance.
The chip sheet advertises standby gesture/monitoring capabilities, but a working
module-specific monitoring sequence remains missing. Current single-finger
care/swipes do not justify multitouch work. I2C already runs at the advertised
400 kHz limit; do not overclock it. The explicit 2 ms address/read delay is a
later measurement candidate after protocol correctness.

## CO5300: distinguish display off, sleep and deep standby

The pinned 2.2.0 driver sends SLPIN, waits 120 ms, then sends DSTBON when a reset
pin exists. This board has one, so wake resets and replays initialization. The
BSP table includes two 600 ms waits; earlier measured restoration was about
1.36 seconds. A separate ordinary SLPIN/SLPOUT state could trade retention power
for shorter wake. Prototype an explicit driver option with coherent state,
brightness restoration and full-frame repaint; do not hide the reset pin from
the driver or bypass its state machine. The datasheet's 120 ms timing does not
prove either module-specific 600 ms delay can safely be removed.

The BSP sends TEON but selects no tear avoidance. Inspect the exact schematic
and measure the TE signal before adding scheduling. At configured 40 MHz QSPI,
466 x 466 RGB565 requires 434,312 bytes and an ideal 21.7 ms wire transfer even
before overhead. A 16 ms engine tick is not proof of 60 full-panel frames/s;
TE synchronization alone cannot fit that transfer into a 16.7 ms panel period.
Measure dirty-region transfer and tearing together; avoid waiting once per strip.

Hardware partial-display mode is distinct from the address windows already
used for dirty updates. It might suit a deliberately small sleep display, but
requires panel-specific visual and energy testing. CO5300 idle mode is eight
colors, unsuitable for current RGB565 gradients, tint and particles. It is not
a generic low-refresh mode. Hardware brightness already has an API; game night
colors currently retain the same configured 60 percent brightness. State-aware
brightness plus black backgrounds is a simpler first energy experiment than
undocumented gamma, high-brightness or dimming settings.

Keep the separate engine/canvas ownership and reserved DMA strip. Panel DMA
completion, not `present` return, is the end of the visible-response pipeline.

## IMU and RTC: useful capabilities, different wake routes

The exact-board schematic routes QMI8658 INT2 directly to GPIO21 and INT1 to
EXIO6. RTC INT routes to EXIO3, correcting the earlier EXIO1 claim in memo-043.
TCA9554 INT has a pullup but no ESP32 connection on the schematic. Consequently
RTC alarm cannot directly wake this board's MCU through that route; use ESP32
timer wake and read RTC afterward. QMI uses VCC3V3; RTC uses VCC-RTC.

Pinned BSP declares `BSP_CAPS_IMU=0`; application startup does not initialize a
QMI driver. QMI8658C supports accelerometer wake-on-motion through selectable
interrupts, low-rate acquisition and FIFO batching. Its motion status read
clears/toggles interrupt state, so capture pin/wake evidence before acknowledging.
Datasheet current figures at 1.8 V are not board measurements at 3.3 V. Begin
with identity/revision/configuration capture without clear-on-read status, then
accelerometer-only motion wake on INT2 with bounded timer fallback. Test quiet
desk, deliberate pickup, repeated motion and already-active wake level. Keep
pickup-to-wake-screen distinct from ending the pet's sleep state. Do not enable
gyroscope continuously without an interaction that needs it. Datasheet self-test
procedure is TBD, insufficient for manufacturing acceptance limits.

RTC trust handling already rejects STOP, test, 12-hour mode, oscillator-stop,
invalid BCD/date, unknown persisted trust and backward recovery anchors. Preserve
these checks. RTC ticking and trusted UTC are separate results. Test progression
against station time, then retention with controlled main-power loss. Calendar
alarms, offset calibration and timer exist but are unused. CLKOUT is unconnected;
inspect its configuration before disabling it. NXP requests unused timer clock
selection of 1/60 Hz, already the reset default. Read before writing, preserve
unrelated bits/flags, and measure before claiming savings. Drift calibration
needs a measured reference, not a guessed offset.

Long hardware sleep must explicitly use bounded resume recovery. The live path
admits only a two-second backlog. Add deterministic overnight journal, scheduled
boundary, 24-hour cap, backward/unknown RTC and interrupted-checkpoint coverage
before product sleep. Sensor events belong in a host adapter and fixed queue.

## AXP2101: inventory before power policy

The AXP2101 is the board's power-management IC: regulators, battery charging,
source management, ADC telemetry and battery gauge. Pinned BSP 3.0.1 has no AXP
initialization policy. Validation currently reads only 03/26/80/90 at address
0x34; the observed 4a/08/0f/ff establishes register access, not charging accuracy
or an efficient configuration.

DCDC1 supplies shared VCC3V3 including ESP32 and display/touch, so it cannot gate
the panel independently. ALDO1 supplies A3V3 analog/audio circuitry, a possible
later experiment after checking back-power through digital connections. RTCLDO
supplies VCC-RTC and must preserve the time-retention path. Several other outputs
are drawn unloaded; verify the fitted board before disabling them. AXP IRQ
routes through EXIO5, with no connected expander-to-MCU interrupt in the published
schematic. It is not an established direct MCU wake route.

Start with bounded raw/decoded inventory: identity, power reasons, source/battery
status, ADC enables, IRQ enables/status, regulator settings and charge settings.
Read enabled battery/VBUS/system voltage channels in documented high/low order,
then compare with a fixture meter. Disabled channels must report unavailable.
Preserve IRQ evidence before write-one-to-clear acknowledgement. Battery percent
is a model-dependent estimate, not a calibrated manufacturing acceptance value.
ADC voltage telemetry does not replace external board-current measurement.

The vendor example changes charging current, target voltage and TS settings;
its comments reference unrelated camera/PIR loads. Do not copy that initialization
as this board's battery policy. Its event handler is called by a polling task,
not proof of working IRQ wake. PMIC sleep register 26h affects outputs and PWROK,
which connects to CHIP_PU; it is not equivalent to ESP32 light sleep. Defer PMIC
sleep until rail dependencies, save/recovery and wake behavior are established.

Reusable cases should progress from `pmic.inventory` to fixture voltage checks
and source/event correlation. Battery characterization and rail switching need
separate procedures with restoration, selected battery specifications and
external current/charge measurements. These are proposed case IDs, not commands
already implemented in the factory image.

## ESP32-S3 LX7: specialized kernels after pipeline measurement

The S3 implements Espressif PIE with eight 128-bit vector registers and packed
integer operations. This is an S3 extension, not a generic capability of every
LX7. Existing O2 compilation already emits scalar `mul16`; inspection of current
pet_canvas, pet_particles and pet_atmosphere objects found no `ee.*` vectors.
The core already uses integer arithmetic. Current rectangle drawing clips once
per row and fills spans, superseding memo-031's historical per-pixel description.
Sprite expansion still produces many short scale-six spans. Memo-033's bulk
copy comparison gained only 0.09 percent; do not repeat it as a new opportunity.

First separate render, lock wait, canvas copy, LVGL draw, byte swap/staging and
SPI completion. Compare idle, actor motion, captions, particles and sleep scenes.
Then benchmark long RGB565 fill/blend/tint spans against the scalar reference.
Coalescing tiny sprite spans may matter more than assembly. A second candidate
fuses byte swap with PSRAM-to-DMA staging, explicitly disabling the original
swap and retaining ownership/completion behavior.

Most PIE vector accesses force low address bits to zero; unaligned accesses can
silently target a preceding block. Our 932-byte stride cycles through offsets
0,4,8,12 modulo 16. Aligned allocation alone is insufficient. Require bounded
scalar prefix/body/tail, exact RGB565 rounding, alpha endpoints, clipped edges,
guards and scalar equivalence. Keep optional specialized kernels in the ESP32
port; retain portable behavior and deterministic tests.

Current normal configuration is 160 MHz, 16 KiB I-cache, 32 KiB D-cache/32-byte
lines; main is on core0 and LVGL is unpinned. Independently compare 240 MHz,
cache configuration and task affinity only after timing attribution. Larger
cache consumes internal memory. Measure energy per scene and idle opportunity,
not just FPS. Core-local cycle counters require pinned tasks and known frequency.
LVGL SIMD does not automatically accelerate our pre-rendered canvas. The
Espressif lvgl_port integration gates its assembly to LVGL 9.1, while this BSP
uses lvgl_adapter and LVGL 9.3. ESP-DSP is useful implementation evidence, not a
drop-in RGB565 blend library. No P4 PPA is available here.

# Recommendations

| Order | Bounded experiment | Evidence required |
| --- | --- | --- |
| 1 | Input/render/transfer timing and PMIC baseline | Build/config identity, scene, latency distribution, bytes, external power baseline |
| 2 | Touch failure/cancel and timer period A/B | No synthetic taps, valid next gesture, latency and idle wakeups |
| 3 | Brightness and ordinary panel sleep A/B | Physical image quality, wake latency, energy, restored driver state |
| 4 | IMU identity, RTC integrity and idle configuration | Explicit trust/absence/errors, readback and restoration |
| 5 | IMU and touch physical wake matrix | Actual source, pin level, missed/false wake count, timer fallback distinct |
| 6 | Long-span scalar/PIE and fused staging | Exact pixels/guards plus end-to-end latency and energy |
| 7 | TE, cache/clock/affinity and sensor interaction | One variable per run, game quality and resource margin |

Each case should use retained results through the existing factory/power debug
path: prerequisite, hypothesis, baseline, one change, bounded execution,
measurement, restoration and outcome. Record raw register values separately
from interpreted state; avoid clear-on-read status in generic telemetry. Keep
factory identity/electrical checks distinct from performance exploration.
Fixture-dependent motion, image quality and current checks remain skipped until
actually observed. No battery-life claim can be based on USB-attached timing
alone; existing USB light-sleep protection still affects that baseline.

# References

- [Earlier board audit](memo-054-device-power-and-driver-audit.md)
- [Measured power trials](memo-044-device-power-experiments.md)
- [Exact-board schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf)
- [Vendor resources and manuals](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Resources-And-Documents)
- [CO5300 V0.01 datasheet](https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf), sleep pp143–146, partial pp165–168, idle p175, standby p183.
- [Vendor touch implementation](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/e4344e70c2fa78a13e8a06566507f1ba8af6672a/examples/arduino/libraries/SensorLib/src/touch/TouchDrvCST92xx.cpp)
- [Vendor touch constants](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/e4344e70c2fa78a13e8a06566507f1ba8af6672a/examples/arduino/libraries/SensorLib/src/REG/CST9xxConstants.h)
- [AXP2101 manufacturer manual](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf)
- [Vendor PMU example](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/e4344e70c2fa78a13e8a06566507f1ba8af6672a/examples/esp-idf/01_AXP2101/main/port_axp2101.cpp)
- [QMI8658C manufacturer datasheet](https://files.waveshare.com/wiki/common/QMI8658C.pdf)
- [NXP PCF85063A datasheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf)
- [ESP32-S3 TRM v1.8](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf), chapter1 PIE and alignment.
- [Pinned IDF performance guidance](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/performance/speed.html)
- [Espressif LVGL integration](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/CMakeLists.txt)

# Appendix

Ignored research evidence lives in `build/device-research-pass2/` (CO5300) and
`build/device-power-research/` (schematic, enlarged routing evidence and
`touch-second-pass/` vendor sources). CO5300 PDF extraction used repository uv
with PyMuPDF because pdftotext was unavailable. No global tool installation was
needed. The vendor touch snapshot is pinned above; bundled family-manual SHA256
is `0e9a0c42358c907a1926a17aea987453d144f2f0dfbd5a694bd745bb32791860`.

---
id: memo-054
title: Device power and driver capability audit
author: Codex
created: 2026-10-10
tags: [drivers, embedded, investigation, power, sleep]
project_id: jelligotchi
doc_uuid: 574710f4-0c3f-4521-8ca3-9c14f37134df
---

# Scope and evidence

Follow-up hardware experiments: [memo-044](memo-044-device-power-experiments.md).

Created `investigate/device-power` in the sibling `jelligotchi-device-power`
worktree from `15553f6` on `feat/authored-activities`. This is a source and
hardware-document review for SKU 31261, not firmware implementation or a claim
of measured power savings. The user asked to check chip datasheets against the
default drivers as well as investigate touch wake and sleep.

Read local ESP-IDF v5.5.5 and managed components from the original checkout,
without copying its SDK, generated config, or build caches into this worktree.
Tracked pins are BSP 3.0.1, LVGL 9.3.0, adapter 0.6.4, CST9217 driver 2.0.0,
and CO5300 driver 2.2.0 (corrected against lockfile and deployment evidence). Adapter and touch `.component_hash` markers match the
lockfile; this is not a full byte-for-byte package integrity audit. Generated
`sdkconfig` observations below describe that checkout, not a fresh build.

`./scripts/esp ports` found only Bluetooth and debug-console ports, no ESP32 USB
device. No build, SDL test, flash, current measurement, or physical touch/display
test was performed. Documentation checks are recorded at the end.

# Findings

## Touch IRQ is already enabled

The BSP configures CST9217 INT on GPIO11, active low, and reset on GPIO40.
The exact board schematic agrees. Display reset is separately wired to GPIO39;
the BSP comment saying touch shares LCD reset is stale. Do not copy reset pins
from the 1.75C board.

In `esp_lv_adapter_input_touch.c`, `esp_lv_adapter_register_touch` enables IRQ
mode whenever `int_gpio_num` is present. Default callback registration is enabled.
It registers an ISR that gives a binary semaphore and requests adapter wake.
`lvgl_touch_read` consumes that semaphore before reading I2C. Thus the absence
of a callback in the BSP initializer does not mean polling-only touch.

This still uses an LVGL input timer. IRQ-driven I2C reads and waking a sleeping
MCU are separate concerns: no application code configures a GPIO sleep wake
source. Verify the interrupt waveform, release reporting, and pending events
before relying on level wake. The hardware document describes configurable
interrupt edges, not a guarantee that INT stays asserted until serviced.

## Chip capabilities versus driver coverage

| Chip or subsystem | Documented capability | Current coverage and gap |
| --- | --- | --- |
| CST9217 | Hynitron V1.0 sections 2 and 10.2 distinguish active scanning, low-power monitoring with touch detection, and deep sleep requiring a wake command or external reset. | Driver installs read, coordinate, and delete callbacks, but no touch sleep callbacks or explicit monitoring-mode control. Need the module firmware command protocol; the electrical datasheet does not supply a usable mode-register sequence. |
| CO5300 | V0.00 sections 7.5.11, 7.5.12, and 7.5.37 define sleep-in, sleep-out, and deep standby. Deep standby exits through a reset pulse longer than 3 ms. | SPI driver implements `disp_sleep`: sleep-in, 120 ms delay, then deep standby when a reset pin exists. Wake resets and reinitializes. App uses none of this. Preserve panel brightness and restore a full image after wake. |
| ES8311 | User Guide Rev1.11 sections 9.1–9.3 describe standby, analog power-down and low-power controls. | Codec driver has a stop sequence that shuts down analog blocks and clocks. App opens codec at startup and leaves it open while its sound task waits. Sending silence is not power-down. Audit close/reopen, I2S clocks, and PA control together. |
| AXP2101 | Datasheet V1.0 section 6.13.2.24, register 26h, exposes sleep/wake control. | No AXP sleep policy in the application/BSP source reviewed. Rail control requires a schematic-derived load map and readback of actual PMIC settings first. A PMIC sleep bit is not a substitute for the ESP32 sleep API. |
| PCF85063A | Datasheet describes alarm/timer and interrupt output. | Session uses time registers only. Second-pass schematic inspection corrects the route to EXIO3; the expander INT has no ESP32 connection. See [memo-047](memo-047-device-capabilities-and-experiment-backlog.md). RTC alarm is not a direct MCU wake source on this board. ESP32 timer wake is a separate option. |

The Hynitron sheet quotes typical chip currents of 1.3 mA active at 100 Hz,
12 microamps monitoring at 30 Hz, and 2 microamps sleeping. These are chip
figures under specified conditions, not board current or promises for this
module firmware. Monitoring is the candidate for touch-to-wake; deepest touch
sleep intentionally stops touch detection.

The same sheet, section 10.5, lists a typical reset recovery time of 100 ms.
`cst9217_reset` waits 50 ms after raising reset, then initialization sends its
first command. This is a timing discrepancy to investigate with module firmware
and startup traces, not proof of a minimum-time violation: the sheet gives a
typical value. Avoid claiming the driver is wrong without that evidence.

## The host keeps the device active during pet sleep

- `main.c` targets 16 ms engine frames and sets the display refresh timer to
  16 ms. Pet sleep does not change this policy; brightness remains 60 percent.
- Adapter defaults use a 1 ms periodic `esp_timer`, a 15 ms maximum worker
  delay, and disabled auto-sleep. Its pause API stops the tick timer, which
  offers an existing integration point. Pausing LVGL alone does not stop the
  engine, network worker, or peripherals.
- Local generated config has `CONFIG_PM_ENABLE` unset and a 160 MHz CPU.
  Tracked defaults do not enable PM/tickless idle, and app code does not call
  `esp_pm_configure`. Automatic light sleep needs both PM and tickless idle
  plus runtime setup. IDF timers and task deadlines limit sleep duration.
- Network worker wakes every 250 ms even when networking is disabled. With
  Wi-Fi enabled, coordinate modem sleep, reconnection and OTA with device sleep.
- Audio blocks on a queue, but its open codec/I2S path still needs explicit
  shutdown review. Measure driver locks and clock activity rather than assuming
  an idle task means idle hardware.
- BSP `backlight_off` merely sets AMOLED brightness to zero. It does not invoke
  the panel's sleep callback. Display-off, brightness-zero and deep standby
  are distinct operations with different wake costs.

## Long sleep needs explicit game-time recovery

`core/game_advance.c` caps the live backlog at 2,000 ms and processes at most
800 ms per call. Resuming an hours-long sleep through an ordinary frame would
discard time. Use a deliberately defined bounded resume path, update the host
monotonic anchor once, and avoid double advancement through session checkpoints.
The existing offline path has a cap and different behavior semantics; review
sleep journal, rest gains, scheduled wake and missing RTC behavior before reuse.

Keep display wake distinct from ending a pet sleep session. A first tap should
be able to reveal the screen without accidentally activating a care/menu item
or terminating the journal. This is a recommended policy, not an approved UI
change. Preserve the single engine owner and enqueue bounded work from ISRs.

# Recommended experiments

1. Capture battery-powered baselines for awake, pet asleep with display on,
   brightness-zero, panel sleep, audio closed, and MCU light sleep. Record USB
   presence, Wi-Fi, brightness, image, current, wake latency and PM locks. USB
   Serial/JTAG can prevent automatic light sleep when connected; verify the
   relevant config and measure unplugged operation separately.
2. Prototype a host-owned screen-sleep state: drain panel DMA, stop rendering,
   pause LVGL/tick, silence and close audio, then sleep the panel. Keep touch
   powered in a wake-capable mode. Restore panel state and a full frame before
   accepting ordinary gestures. Test a press held across entry and fast taps.
3. Add GPIO11 wake and a bounded timer fallback. Enable PM/tickless idle and
   configure automatic light sleep only after other wake sources and driver
   locks are understood. Check the already-asserted INT race before sleeping.
   Normal GPIO interrupts alone do not wake an ESP32-S3 from light sleep.
4. Add fake-time regressions for short/long sleep, cap boundaries, RTC loss,
   repeated wake, pending saves, and scheduled wake. Then run the required
   host tests and firmware build. Test repeated physical sleep/wake cycles,
   missed/duplicate gestures, display restoration and audio pops on hardware.
5. Consider deep sleep later. It restarts the application and needs checkpoint,
   trusted elapsed-time recovery, wake pin configuration and peripheral power
   retention. Do not cut the shared touch/MCU rail to save panel power.

Keep the existing engine/canvas separation and display lock. There are 868,624
bytes in two application frames, plus guards, BSP buffers, DMA staging, stacks
and SDK allocations. Reducing wake frequency is the first experiment; removing
buffers is a separate ownership change. See the earlier
[render audit](memo-031-esp32-render-acceleration-review.md) and
[copy experiment](memo-033-full-width-display-copy-experiment.md).

# Datasheet evidence and remaining gaps

Use the exact schematic first, then identify each chip and record document
revision/page, relevant registers and timing, driver symbol, and a hardware
check. API names and default-driver behavior do not establish chip limits.
For any undocumented mode, obtain the module firmware protocol or vendor
confirmation before writing guessed registers.

- [SKU family schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf): page 1 visually inspected. Shared VCC3V3 powers the panel connector and MCU; reset lines are separate. Board revision and fitted parts still need physical confirmation.
- [Waveshare resource index](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Resources-And-Documents): primary source for board, PMIC, RTC, codec and IMU documents.
- [Hynitron CST9217 V1.0 manufacturer sheet, mirrored PDF](https://raw.githubusercontent.com/ScripTerasu/cst92xx-touch-driver/main/docs/CST9217.pdf): pages 1, 6–7 and 9 inspected. Third-party hosting means provenance should be confirmed with Waveshare/Hynitron. Do not use that repository's example board pins.
- [Chipone CO5300 V0.00, Espressif-hosted](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf): sleep commands and page 183 deep standby.
- [Everest ES8311 User Guide Rev1.11](https://files.waveshare.com/wiki/common/ES8311.user.Guide.pdf): sections 9.1–9.3.
- [X-Powers AXP2101 V1.0](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf): sleep/wake control; full rail sequencing audit remains open.
- [NXP PCF85063A](https://files.waveshare.com/wiki/common/PCF85063A.pdf): alarm/interrupt capability; expander wake routing remains to be implemented and tested.
- [IDF 5.5.5 power management](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/power_management.html) and [sleep modes](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/sleep_modes.html).

Downloaded schematic and CST9217 PDFs, extracted text and schematic image are
local ignored evidence under `build/device-power-research/`. SHA256 values:

```text
board.pdf: 26d33d14ae975f8d6817d103c41e704dc0682c8b28e61aed845dba933e9133e3
CST9217.pdf: 74cf16e6dc45fbbb75c06a79c9d285d6f34c3202c4ff5aef9e24dd53ef9ee3bc
```

Outstanding: touch firmware mode commands and interrupt pulse behavior, full
PMIC rail audit, QMI8658/ES7210 unused-peripheral power state, and fitted GNSS
variant. The initial Waveshare wiki request returned HTTP 403; its documentation
resource page and directly hosted schematic worked. The CST PDF web reader
failed, but direct download and local PDF extraction succeeded.

# Validation

`make docs-check` passed with one fixable tag-order finding. `make docs-fix`
was run to apply and review that repair. Firmware and hardware behavior are
unverified.
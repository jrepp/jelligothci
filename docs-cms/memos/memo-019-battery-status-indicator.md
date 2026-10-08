---
title: Battery status indicator and read-only AXP2101 reading
author: Claude
created: 2026-10-08T17:47:43Z
tags: [assets, embedded, memo, ui]
id: memo-019
project_id: jelligotchi
doc_uuid: 0764a2f6-8d8b-4ab5-979b-4e2208287a8f
---

# Overview

The user asked for a Settings option to display the battery, an attempt to read
the battery level and charge status, and battery artwork. This memo records the
implemented design, the hardware register evidence, the decisions made under the
art budget, and what remains unverified.

# Context

Both Waveshare boards carry an X-Powers AXP2101 PMU on the shared BSP I2C bus
(SCL 14, SDA 15), the same bus as the PCF85063 RTC already read by the session.
The embedded skill requires separate authorization for charger, ADC, gauge, or
power-path configuration, so the firmware reads the PMU without writing it.
The portable core must not poll hardware or include SDK headers.

# Details

## Core contract

`include/jelli/power.h` defines a host-reported `JelliPowerReading`: a state
(unknown, no battery, discharging, charging, charged) and a percentage 0–100 or
an explicit unknown sentinel. Hosts write `engine->ui.power` between frames, the
same way they write the save status and time availability. A debug override in
`JelliPetUi` takes precedence until cleared; it is never saved.

`jelli_battery_indicator` is a pure mapping to one of seven icons and a caption
of at most seven glyphs. Thresholds: empty below 10 percent (no bars and an
exclamation mark), low 10–24 (one bar), medium 25–69 (two bars), high 70–100
(three bars). Below 25 percent the caption reads `LOW 8%`, so low charge is shown
by bar count and text as well as by the pink caption. Charging shows a bolt with
the percentage; charge complete shows three bars and `FULL`; external power
without a battery shows a plug and `PLUGGED`; an unknown level shows `--%`.
Out-of-range states read as unknown and percentages 101–254 clamp to 100.

`jelli_power_normalize` gives a canonical reading in which ignored percentages
(unknown, plugged, full) become unknown. The render key stores only the
canonical reading, and only while the indicator is visible. An unchanged
displayed value therefore causes zero damage, and a hidden indicator never
causes a redraw.

## Setting and save codec

`JelliGame.battery_display` defaults on. The Settings ring adds BATTERY at slot
5 (bottom left), with the same ON/OFF badge style as REST; existing slot IDs are
unchanged. The toggle requests a save, plays the accepted-input cue, emits input
event code 41 (codes 28–40 were already used), and does not close Settings when
the pet is busy. Save codec version 4 appends one strict boolean before the
trailer. Versions 1–3 load with the indicator shown; decoders accept 1..4, and
the SDL storage layer now uses the shared `JELLI_SAVE_VERSION` constant.

## Rendering

The indicator draws at 2x in the upper right of the resting home scene: icon
at (356, 96), caption centered at x = 372 on a soft scrim. It is clear of the
widest creature frame (x <= 306), the headings, the status caption, the stat
tile, and the MENU button, and it stays inside the visible circle. It is hidden
while any ring, the collection, or an activity is open because ring slot 2 and
the activity controls occupy that corner. A battery-only change redraws a fixed
129x62 rectangle through the existing region renderer; tests check that the
partial update matches a full redraw pixel for pixel.

## Hosts

SDL samples `SDL_GetPowerInfo` once per five seconds of injected time and maps
each `SDL_POWERSTATE_*` value. Headless runs do not sample, which keeps snapshots
deterministic. The development Mac (`Mac17,6`) reports charged at 100 percent,
displayed as `FULL`.

The ESP32 adds the PMU at 7-bit address 0x34 as a second device on
`bsp_i2c_get_handle()` and samples every ten seconds from the engine task,
outside the display lock, with a 10 ms I2C timeout. Any read failure reports an
unknown reading and logs once until reads recover.

| Register | Bits used | Source |
| --- | --- | --- |
| 0x00 PMU status 1 | 5 VBUS good; 3 battery present | Datasheet V1.0 §6.13.2.1; `isVbusGood`, `isBatteryConnect` |
| 0x01 PMU status 2 | 6:5 direction (01 charge); 2:0 charger (100 done) | §6.13.2.2; `isCharging`, `getChargerStatus` |
| 0x18 control | 3 gauge enable (power-on default 1) | §6.13.2.14; `enableGauge` |
| 0xA4 percentage | 7:0 percent | §6.13.2.88; `getBatteryPercent` |

Sources: X-Powers AXP2101 datasheet V1.0 and Waveshare's XPowersLib copy
(`src/REG/AXP2101Constants.h`, `src/XPowersAXP2101.tpp`), which their ESP-IDF
`01_AXP2101` example mirrors. Battery absent with VBUS good reports no battery.
Charge done with VBUS good reports charged; direction "charge" reports charging;
otherwise the state is discharging, including plugged-in idle, so the stored
level is shown.
If register 0x18 bit 3 is clear, the percentage is reported unknown rather than
enabling the gauge.

## Artwork and budget

`assets/slice/source/battery_icons.py` authors seven 16x16 icons (IDs
12001–12007) in the shared palette with binary alpha, following the ink-outlined,
cream-cased style of the existing 16px icons. The planned slice suggested eight
icons with separate high and full levels. Eight would raise the raw payload to
151,840 bytes and the planned pack to 164,128 bytes, over the 163,840-byte
(160 KiB) ceiling enforced by the asset builder. Seven icons with three bars use
151,296 bytes, and the planned pack is 163,584 bytes, leaving 256 bytes. The
ceiling was not raised; that is a separate budget decision.

# Verification

- Core tests cover mapping boundaries, invalid input, canonical equivalence,
  the setting toggle and persistence, v4 round trip and v3 default, damage
  rectangles, hidden-indicator stability, and the debug override.
- `make test` (37 tests), `make core-test` (18), and `make sanitize` (37) pass.
- Both firmware images build: 1.75 app 0xd9f00 bytes (15 percent of the 1 MB
  partition free), 1.75C 0xd7530 bytes (16 percent free).
- SDL interactive run read the host battery; headless screenshots used the
  debug override.

# Remaining uncertainty

- No firmware with this change has been flashed. The AXP2101 reading, the
  presence of the PMU on the 1.75 (SKU 31261), and the gauge's default state are
  unverified on hardware.
- Physical legibility of the 2x icon and caption on the AMOLED is unverified.
- The thresholds and the decision to show the level while plugged in but idle
  are prototype choices.

# References

- [Hardware guide](../../docs/hardware.md#battery-status)
- [Playing and debugging](../../docs/playing.md#battery-indicator)
- [Artwork inventory](../../assets/slice/README.md)
- [Bounded debug interface](../adr/adr-010-bounded-usb-serial-debug-interface.md)

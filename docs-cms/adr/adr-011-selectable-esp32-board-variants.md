---
title: Select the ESP32 board variant at build time
status: Proposed
created: 2026-10-08T16:24:03Z
deciders: Michael Weingartner
tags: [architecture, embedded, esp32, build]
id: adr-011
project_id: jelligotchi
doc_uuid: f4a6afd9-8418-480d-9c1f-3d613feb3cfc
---

# Context

The firmware targeted only the Waveshare ESP32-S3-Touch-AMOLED-1.75, SKU 31261,
and the project guidance warned against the similarly named 1.75C. A
contributor's attached board reported an ESP32-S3 revision 0.2 with 8 MB
embedded PSRAM and 32 MB flash; the 1.75's recorded board has 16 MB flash
([memo-003](../memos/memo-003-first-usb-deployment.md)), and Waveshare lists
32 MB for the 1.75C.

Both boards use the CO5300 display and CST9217 touch controllers on the same
QSPI and I2C pins. Waveshare's 1.75C examples and its 1.75C BSP move the LCD
reset from GPIO 39 to 1, the touch reset from 40 to 2, and the audio MCLK from
42 to 16, and drop the uSD slot and TCA9554 expander. Waveshare publishes a
separate BSP for each board. The firmware calls only display, brightness, lock,
speaker codec, and I2C-handle functions, which both BSPs provide with the same
signatures and initialization code apart from those pins.

# Decision

Select the board at build time with the `JELLI_ESP_BOARD` CMake cache variable
(`1.75`, the default, or `1.75c`). `scripts/esp` reads the same name from the
environment, rejects other values, and gives each board its own build directory.

Each board is a `jelli_board` wrapper component under
`ports/esp32/boards/<board>/` that pins its Waveshare BSP as a public dependency
and defines a `JELLI_BOARD_LABEL` logged at startup. The shared `main`
component requires `jelli_board` and includes `bsp/esp-bsp.h`, so firmware code
does not name a specific board. Pins stay in each BSP.

The 1.75 keeps its existing `dependencies.lock` and `ports/esp32/sdkconfig`
paths. Other boards keep a tracked `dependencies.<board>.lock` and a generated
`sdkconfig` in their build directory. Both boards share `sdkconfig.defaults`,
including the 16 MB flash layout.

# Approval basis

On 2026-10-08 Michael chose to add 1.75C support as a build option that keeps
the 1.75 as the default, rather than flashing the 1.75 image to a 1.75C. The
repository maintainer has not reviewed this decision, so it remains Proposed.

# Consequences

## Positive

- The 1.75C runs with its own BSP's pins instead of the 1.75's.
- The 1.75 build, its lock versions, and its paths are unchanged.
- Each image's serial log names its board.
- Adding a board needs a wrapper, a lock file, and a selector entry, not
  changes to firmware code.

## Negative

- Each board has a lock file to maintain on BSP or dependency upgrades.
- Switching boards removes the other board's BSP from `managed_components`, so
  the next build for that board downloads it again from cache or network.
- Nothing detects the attached board; selecting the wrong image is still
  possible and drives the reset and audio-clock pins incorrectly.

## Neutral

- The 1.75C uses 16 MB of its 32 MB flash.
- CI does not build firmware, so neither board is built in CI.

# Alternatives Considered

## Kconfig choice with conditional dependencies

The component manager can resolve dependencies from Kconfig values, but that
requires a second resolution pass and still rewrites a shared lock file when
the board changes. The lock path must be set before Kconfig is loaded, so a
CMake variable is needed regardless.

## Copy the 1.75C pins into shared code

Overriding pins in the port would duplicate BSP internals and diverge from
Waveshare's maintained initialization. Each BSP already owns its pins.

## Flash the 1.75 image on a 1.75C

The image would drive the display reset, touch reset, and audio clock on the
wrong pins. It was rejected as unreliable and not representative.

# References

- [Hardware guide: board variants](../../docs/hardware.md#board-variants)
- [Project CMake board selection](../../ports/esp32/CMakeLists.txt)
- [Board wrappers](../../ports/esp32/boards)
- [ESP32 workflow](../../scripts/esp)
- [Repository-local toolchains](adr-004-repository-local-pinned-toolchains.md)
- [Waveshare 1.75C BSP 3.0.0](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75c/versions/3.0.0/readme)
- [Waveshare 1.75C examples](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C)

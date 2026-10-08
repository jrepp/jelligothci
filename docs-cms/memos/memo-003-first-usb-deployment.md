---
id: memo-003
title: First USB deployment of the shapes MVP
author: Codex
created: 2026-10-07
tags: [deployment, esp32, hardware, mvp]
project_id: jelligotchi
doc_uuid: 068b250c-1436-41ab-a485-4e88b06523cd
---

# Scope

On 2026-10-07 at approximately 23:24 UTC, the user requested deployment of the
shapes MVP to the attached USB board. Firmware version 0.1.1 was built locally
with the pinned ESP-IDF v5.5.5 environment and flashed successfully.

# Device and deployment

Serial discovery found one Espressif USB JTAG/serial device at
`/dev/cu.usbmodem101` (VID:PID 303A:1001). The ROM tool identified an ESP32-S3
revision 0.2 with 8 MB embedded PSRAM and 16 MB flash, consistent with the
specified Waveshare ESP32-S3-Touch-AMOLED-1.75 target.

Commands used:

```sh
./scripts/esp ports
make esp-build
make esp-flash PORT=/dev/cu.usbmodem101
./scripts/esp monitor /dev/cu.usbmodem101
```

The standard flash operation wrote the bootloader, partition table, and
558080-byte application image, verified their hashes, and reset the board.
No full-chip erase or eFuse changes were performed. The port path is an
observation from this Mac, not a portable device identifier.

# Startup evidence

Serial output after reset reported:

- Application `jelligotchi`, version 0.1.1, ESP-IDF v5.5.5.
- 16 MB flash and 8 MB PSRAM; the PSRAM memory test passed.
- CO5300 panel creation succeeded.
- CST9217 touch controller detected at 466 by 466 resolution; IRQ input registered.
- LVGL task started, followed by `Shapes MVP ready: tap to pause/resume`.

Startup emitted driver notices about the panel pixel-format initialization,
I2C pull-up configuration, and disabled gesture recognition. Touch controller
communication succeeded; the MVP uses single-point input. These notices alone
do not establish a display or touch fault.

# Verification boundary

Firmware transfer, boot, memory initialization, and driver/application startup
are verified through serial output. No panic or reboot loop was observed during
monitoring. Physical shape appearance, colors, circle motion, and tap behavior
await the user's observation; serial initialization alone does not prove them.

This deployment supersedes the not-yet-flashed status in earlier memos, which
remain historical records. Battery, charging, sleep/wake, audio, and other
peripherals remain outside the MVP bring-up scope.

# References

- [Build and release validation](memo-002-build-release-and-runner-validation.md)
- [Hardware bring-up instructions](../../docs/hardware.md#hardware-bring-up)

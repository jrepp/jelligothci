---
title: Pet and debug firmware deployment
author: Codex
created: 2026-10-08T02:09:41Z
tags: [debug, deployment, esp32, memo, validation]
id: memo-010
project_id: jelligotchi
doc_uuid: 53dd2a0a-1967-4d0a-b095-55ac713ffaa1
---

# Scope

The user requested flashing the current version before trying the debug interface.
The current working tree was built and flashed successfully. Its base commit is
`efdf404`; the USB/local debug changes were still uncommitted. The application
version remains 0.1.1 because no release-version change was requested. The image
hash below identifies the actual deployed firmware more precisely than that
version string.

# Deployment evidence

Fresh discovery found one Espressif USB Serial/JTAG device at
`/dev/cu.usbmodem1101`, VID:PID 303A:1001. The flashing tool identified an ESP32-S3
revision 0.2, MAC `90:70:69:fe:21:dc`, consistent with the intended board.

Commands completed:

```sh
./scripts/esp ports
make esp-build
make esp-flash PORT=/dev/cu.usbmodem1101
make esp-monitor PORT=/dev/cu.usbmodem1101
```

The binary descriptor was inspected with the pinned esptool before flashing:
application `jelligotchi`, version 0.1.1, ESP-IDF v5.5.5, ESP32-S3, 16 MB flash.
The application is 645,568 bytes. The flash wrote bootloader, partition table,
and application, verified all three transfer hashes, and reset the board. No
full-chip erase or eFuse operations were performed.

Application file SHA-256:

```text
ac29d3fcee13e9389b301794c76a54d17441ac7525eb6007ecea95fe85b565ff
```

ELF descriptor SHA-256:

```text
fe6948b06737e2f0d490b1a20745553c90c16f3f70b34b126e528178a002c6c3
```

Local supporting logs are `build/flash-current-build.log`,
`build/flash-current-transfer.log`, and `build/flash-current-startup.log`.
The port is a dated observation; rediscover it before the next deployment.

# Startup and runtime observation

The monitor initiated `USB_UART_CHIP_RESET`, followed by normal startup:

- Firmware version and ELF hash matched the inspected image.
- 8 MB PSRAM detected; memory test passed.
- CO5300 panel creation and CST9217 466x466 touch detection succeeded.
- Touch input registered and the LVGL task started.
- `Pet slice ready` and `@J1 debug ready` messages appeared.
- Three five-second reports showed approximately 45 engine updates/second.
  Redraw samples reported 30 ms rendering plus 101 ms presentation; an unchanged
  sample reported zero for both. These are engine samples, not panel FPS or
  measured touch latency.

No panic or reboot loop appeared in the observation window. Existing notices
about panel pixel-format initialization, I2C pull-ups, and disabled gestures
were followed by successful driver startup. They were not treated as evidence
requiring board configuration changes.

The monitor was closed with Ctrl+] and the serial port released. No debug
commands or screenshots were attempted during this flash-first step.

# Remaining verification and polish

Flashing and serial startup are verified. Physical pet appearance, colors, and
touch response still require observation; startup logs cannot establish them.
Live USB debug state, input, capture throughput, and disconnect recovery remain
to be tested next. Firmware persistence/retained time remain unimplemented.

The new pet renderer's observed update rate is lower than the earlier shapes
firmware. Full-surface redraw/presentation cost is a measured polish target;
profile damage-region updates and transfer cost before claiming a 60 Hz pet UI.
No performance or electrical changes were made as part of this deployment.

# References

- [Debug protocol and validation](memo-009-wired-debug-interface-and-validation.md)
- [Pet MVP and polish backlog](memo-008-playable-pet-mvp-and-polish-backlog.md)
- [Process learnings](memo-004-process-learnings-and-context-remediation.md)
- [Initial USB deployment](memo-003-first-usb-deployment.md)

---
id: memo-001
title: Shapes MVP foundation and validation status
author: Codex
created: 2026-10-07
tags: [architecture, esp32, mvp, tooling]
project_id: jelligotchi
doc_uuid: 41f1debb-e3e8-4808-85db-16597c546303
---

# Scope

The project is a C foundation for a virtual pet on the Waveshare
ESP32-S3-Touch-AMOLED-1.75, SKU 31261. The initial scope is three simple shapes:
a square, a triangle, and a moving circle. Tap or Space pauses the animation.
Creature behavior, storage, audio, and battery management are future work.

# Platform boundary

The shared core depends on injected timing, input, presentation, output, and
drawable memory. The host owns each buffer and the main loop. The core uses
elapsed time from the injected clock; it does not sleep or call a system clock.
It does not allocate heap memory or include platform SDK headers.

The surface is a 466 by 466 native-endian RGB565 buffer. Its stride is measured
in pixels. Each frame masks the corners to match the round panel. SDL presents
the buffer as a texture. Tests supply fake time, input, and output callbacks.

The ESP32 port uses the Waveshare BSP for display and touch. It copies the core
frame into a separate LVGL canvas while holding the display mutex. A queue moves
touch events from the LVGL task to the engine task. These boundaries keep LVGL
from reading a frame while the core is drawing it.

# Local toolchain

The repository pins ESP-IDF to a release and commit. SDK sources, compilers,
Python environments, and caches live under the ignored `.tools` directory.
Generated output lives under `build`. The component manifest, dependency lock,
and board configuration defaults are tracked.

Use `make esp-bootstrap` for setup, `make esp-sync` to restore the tracked pin,
and `make esp-build` to compile. Sync does not select a new release on its own.
Use `./scripts/esp ports` to discover the current serial port before flashing.

Docuchango manages this docs tree. Its version and the uv version are pinned in
`toolchain.env`. `scripts/docs` runs the pinned package through local uvx. Use
`make docs-check` to report issues and `make docs-fix` to apply available repairs.

# Validation recorded on 2026-10-07

- Desktop engine tests and the headless SDL smoke test passed.
- Address and undefined behavior sanitizer checks passed.
- The core built and passed tests without SDL.
- The interactive SDL host ran, and a rendered snapshot was inspected.
- Repository-local SDK bootstrap, repeat sync, and firmware builds passed.
- USB enumeration found an Espressif JTAG/serial device on the development Mac.

The board has not been flashed with this firmware. Physical display colors,
touch orientation, frame rate, and reset behavior remain unverified. A compiled
binary and USB detection do not establish that those checks pass.

# References

- [Project setup and commands](../../README.md)
- [Engine interface](../../include/jelli/engine.h)
- [ESP32 adapter](../../ports/esp32/main/main.c)
- [Toolchain pins](../../toolchain.env)

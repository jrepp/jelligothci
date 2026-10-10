---
id: memo-024
title: ESP32 display mismatch diagnostics
author: Codex
created: 2026-10-09
tags: [debugging, display, esp32, memory]
project_id: jelligotchi
doc_uuid: 1b34d727-a0e5-440e-9f6a-f7661bf4cc16
---

# Observation

The user reported intermittent random colors or scrambled rows on the ESP32.
Two USB framebuffer captures appeared intact. The user explicitly confirmed
that the physical panel differed from the captured framebuffer. The symptom
also cleared without a firmware change. This establishes a display-path
mismatch, not a confirmed framebuffer overwrite or driver race.

The old firmware did not expose its LVGL canvas or memory guards, so it cannot
localize the fault between the engine-to-canvas copy and panel transfer. Local
captures and accompanying state are under ignored build output:
`display-corruption-before.png` and `display-corruption-repeat.png`. The latter
shows the Food screen. A read-only serial observation produced no log output;
that is not evidence that no runtime error occurred.

# Diagnostic change

Preserve the separate engine and canvas buffers, display mutex, native RGB565,
and BSP transfer configuration. Add 32-byte guards before and after each frame
(128 extra PSRAM bytes total). Each frame remains 434,312 bytes. There are no
new tasks or steady-state allocations. Validate dimensions, stride, and damage
bounds before copying; assert the LVGL canvas stride agrees at initialization.

Once every five seconds, compare the complete buffers under the display mutex,
check guards and heap metadata, and record the task stack low-water mark. The
comparison reads 868,624 bytes per sample, in addition to heap metadata checks;
it is diagnostic overhead, not a new per-frame full copy. Counters and flags
are exposed by the ESP32-only `display` debug command without blocking I/O.
Guard or heap failure stops the app with an error. Unequal pixels are reported
without silently repairing the canvas.

`display refresh` explicitly invalidates the whole existing canvas on the next
uncaptured frame. It neither resets the pet nor overwrites the canvas. If both
buffers agree while the panel differs, a successful refresh would narrow the
remaining fault to LVGL drawing or panel transport. It would not prove which
component caused it.

# Validation

`make test` passed 41 tests, `make core-test` passed 20, and `make sanitize`
passed 41. `make lint-c`, `make esp-build`, and docs validation passed. The
Docuchango repair sorted this memo's tags.

Diagnostic image SHA-256:
`e4d0307749eaa63f6ef33b4e13ab44eacaaaf248fe60db344d39b1e81b803fa5`.
It includes the working tree artwork and the committed food/volume changes.
Flash hashes verified on the rediscovered USB board (serial 90:70:69:FE:21:DC).
The monitor caught `equal=1 guards=1 heap=1 stack_free=1348 mismatches=0`, followed
by repeated LCD SPI queue failures and `Draw bitmap failed: ESP_ERR_NO_MEM`.
The ensuing USB_UART_CHIP_RESET was initiated by opening the monitor.
Evidence is retained in `build/display-diagnostic-boot.log`.

The pinned SPI driver's `setup_dma_priv_buffer` allocates a temporary internal
DMA buffer when given a PSRAM color buffer without direct-PSRAM DMA enabled.
The BSP uses PSRAM draw strips. Failure to allocate that transfer buffer is a
concrete explanation for missing panel updates while both application buffers
agree. The next change reserves DMA transfer storage at startup; physical
resolution remains to be verified after that change.

# References

- [Debug commands](../../docs/playing.md)
- [Display output implementation](../../ports/esp32/main/display_output.c)
- [Deployment evidence requirements](memo-004-process-learnings-and-context-remediation.md)
# Reserved transfer buffer

Reserve a 46,600-byte, four-byte-aligned internal DMA strip before BSP and Wi-Fi
startup. The size matches the pinned BSP's 466-by-50 RGB565 partial draw buffer.
The adapter's supported custom bitmap callback copies its already-packed,
byte-swapped pixels into this strip and submits the existing panel driver.
LVGL waits for the preceding flush before invoking another flush callback, and
the existing BSP completion ISR releases it. No early completion or SDK edits
are introduced. Bounds are checked before copying, including tall narrow strips.

This adds 46,600 persistent internal RAM bytes instead of asking the SPI driver
to allocate temporary DMA storage during each transfer. The two full PSRAM
frames and BSP draw buffers remain. Diagnostic snapshots now include submitted
and failed transfer counts, read under the display mutex.

The fix passes `make esp-build` and `make lint-c`. Its initial flash attempt
failed before transfer: the enumerated USB device returned no serial data.
Reconnection was requested; the fixed image is not yet hardware-verified.

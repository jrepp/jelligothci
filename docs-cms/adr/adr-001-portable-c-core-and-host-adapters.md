---
id: adr-001
title: Share a portable C core across SDL and ESP32 hosts
status: Accepted
created: 2026-10-07
deciders: Jacob Repp
tags: [architecture, c, esp32, portability, sdl]
project_id: jelligotchi
doc_uuid: 37ea92b0-a5b9-4a7d-8a1c-38de17845cd6
---

# Context

Jelligotchi will become a virtual pet for the Waveshare
ESP32-S3-Touch-AMOLED-1.75, SKU 31261. Local development needs fast feedback
without requiring a flash cycle for every engine change.

# Decision

Build the shared engine in C, with SDL as a pluggable desktop host and a separate
ESP32 host. Inject the drawable surface and input/output through an explicit C
interface. Keep SDK calls and hardware setup in the hosts. Both hosts compile
the same core sources.

The current implementation uses C11, a host-owned RGB565 surface, and callback
functions collected in `JelliPlatform`. SDL2 presents a texture. The ESP32
adapter uses the board BSP and an LVGL canvas. These are implementation choices
within the boundary; changing a host should not require a second engine.

# Approval basis

In the 2026-10-07 session, Jacob requested C with SDL as a pluggable emulator
layer, dependency injection for the drawable surface and input/output, and a
porting layer for this ESP32 board. This record captures that explicit direction.

# Consequences

- Engine behavior can be exercised on desktop and with a fake test host.
- The core can build without SDL, ESP-IDF, LVGL, or FreeRTOS headers.
- Hosts must define buffer ownership, input coordinates, and presentation
  lifetime. The core may reuse its buffer after `present` returns.
- Hardware transport and synchronization still need device testing. The current
  firmware compiles, but its physical display and touch have not been verified.

# Alternatives

Embedding SDL calls in engine code would tie the engine to the desktop runtime.
Separate desktop and firmware engines would create two implementations of each
behavior. Both approaches conflict with the requested shared, pluggable design.

# References

- [Engine interface](../../include/jelli/engine.h)
- [SDL host](../../ports/sdl/main.c)
- [ESP32 host](../../ports/esp32/main/main.c)
- [Timing dependency](adr-002-inject-time-and-keep-pacing-in-hosts.md)
- [Initial validation memo](../memos/memo-001-shapes-mvp-foundation.md)

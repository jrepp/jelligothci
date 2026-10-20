---
title: Event-specific celebrations and USB deployment
author: Codex
created: 2026-10-20T23:00:00Z
tags: [animation, deployment, memo, validation]
id: memo-087
project_id: jelligotchi
doc_uuid: 684ce7df-6f2a-4d97-b947-69d1e509858a
---

# Requested increment

The owner requested three animations: evolution, acquiring a new gift, and a pet
accepting a present. [Authored content](../../content/effects.json) now includes
three named timelines and enables their corresponding event bindings:

- `evolution`: gathering light, a rainbow reveal, rising stars and a proud settle.
- `gift-acquired`: a wrapped gift arrives over the pet, with discovery sparkles
  and a musical note, then drifts away.
- `present-accepted`: the wrapped present approaches the pet, opens at its feet,
  and releases staggered thank-you hearts.

Each coordinates curious, happy and content creature clips, named sprite tracks,
attachments and render layers. The generic `celebrate` remains available as an
authoring example. Existing registered artwork is reused, with no new palette or
raster generation. Additional definitions live in immutable generated content;
the pending state budget and single active timeline remain unchanged.

Acceptance uses the existing successful collected-present command, or a completed
inventory-giving effect whose gift inventory decreased. Failed/self-origin gifts
and merely starting the timed giving action do not celebrate. New-gift acquisition
means catching an offered present; it still defers until home after the gallery.
Offline resume remains suppressed. [Event routing](../../core/pet_celebration.c)
and the [editor](../../tools/jelli-art/effects.js) support the same three events.

# Initial deployment evidence

After publishing `2cd265b`, serial discovery found an Espressif USB JTAG/serial
device. Debug capabilities identified the intended `waveshare-31261` game profile.
The normal image's app descriptor reported Jelligotchi 0.13.0, ESP-IDF v5.5.5,
and a valid ESP32-S3 image hash. Standard flashing verified transfer hashes and
reset the board. The monitor then caused a recorded USB_UART_CHIP_RESET.

Startup reported 16 MB flash, 8 MB PSRAM with a passing memory test, successful
CO5300 panel creation, CST9217 touch registration and the pet app-ready message.
Existing panel-format/I2C/gesture notices and an I2S pre-enable disable message
were followed by successful display, touch and audio initialization. No panic
was observed. Display samples reported equal engine/canvas data, intact guards
and heap, zero mismatches and 1324 remaining engine stack bytes. These samples
are evidence from the initial follow-up image, not a celebration frame benchmark.
The generic celebration replay command succeeded over USB.

The sanitized boot log is under ignored `build/celebration-boot.log`; display
samples and rendered previews are also under ignored build output. Software
framebuffer evidence does not establish physical panel appearance or touch.

# Updated-image deployment attempt

The three-animation image built successfully and passed normal-profile isolation.
Before the follow-up flash could open the serial port, the ESP32 disappeared from
USB. The tool reported a nonexistent port; discovery then found only Bluetooth
and system debug-console ports. No bytes from this updated image were flashed.
The remedy is to reconnect and rediscover the intended board, then flash the
already built image and repeat replay/display/touch checks. The user was notified;
no repeated reset/erase attempts were made. The last successful device image
therefore contains the earlier generic celebration, not this three-animation set.

# Validation

- `make test`: all 93 checks pass; `make core-test`: all 41 checks pass.
- `make lint-c` and TypeScript checking pass with no added exceptions.
- All eight Effects browser tests pass in the pinned image, including enabled
  defaults, save/undo and in-flight reload handling.
- Normal, power-validation and factory firmware builds and profile-isolation
  checks pass. The updated normal image is ready for deployment after reconnect.
- Rendered all three sequences through the actual engine into animated previews
  under ignored `build/celebrations`. Inspected anticipation and peak frames and
  the acceptance layout on the axolotl form. Raised the gift to clear the mood
  card. The generic celebration remains available for existing examples/tests.
- Documentation validation and repairs pass without unresolved findings.

Acceptance tests verify rejected self-origin gifts, successful collected gifts,
completion of timed inventory giving, no celebration at action start, and use of
the generated evolution binding by the engine. The initial test used an unknown
origin-pet ID; it now uses the existing valid two-pet fixture. A debug-only test
now clears its prior gameplay save request before verifying replay does not
request a save.

`make sanitize` passes all 93 checks under address/undefined-behavior sanitizers.
Final serial discovery still found no ESP32 board. The updated set has not been
flashed and physical observations have not yet been supplied.

---
title: ESP-IDF 6.1 pin and menu firmware deployment
author: Codex
created: 2026-11-11T00:00:00Z
tags: [deployment, esp-idf, firmware, menu, validation]
id: memo-117
project_id: jelligotchi
doc_uuid: 29483d7e-86c8-424e-96a3-332c38b9633a
---

# Overview

Pinned an earlier local worktree to ESP-IDF 6.1 and deployed the updated 0.18.0 game to
the attached ESP32-S3. The simplified root menu is present in the flashed image.

# SDK pin and build

`toolchain.env` pins tag `v6.1` at commit
`fff9895c82d744c7237be8847347bdd1b07c6643`. The ESP-IDF component constraint
and `ports/esp32/dependencies.lock` now select IDF 6.1.0 and its resolved
components. The hardware setup link points to the v6.1 guide.

The first v6.1 configure failed because `ports/esp32/main/CMakeLists.txt` still
listed the removed `core/pet_food.c`; removing that stale source reference
resolved generation. Dependency resolution and the normal firmware build then
passed. `esptool image-info` identified app 0.18.0, the pinned IDF commit, an
ESP32-S3 target, and a valid image checksum.

# Device deployment

The attached board was rediscovered as `/dev/cu.usbmodem101`, Espressif serial
`90:70:69:FE:21:DC`. Standard `make esp-flash` writes and hash-verifies the
firmware without erasing NVS. A follow-up incremental build included the
shortened `ACTIVITY` root-menu label, and a second standard flash completed with
verified data hashes and reset the board.

The final engine-rendered menu capture is in ignored
`build/esp32/post-flash-menu-final.png`. It shows Food, Water, Basic Care, Play,
Activity, and More at the root. The debug state command after flashing reported
the existing home page, active pet ID 5, nine pets, and save status 2, which is
consistent with the prior save being retained. Startup logs report the pinned
IDF commit and app 0.18.0, passing PSRAM memory test, CO5300 panel initialization,
CST9217 touch IRQ registration, app readiness, and display consistency samples
with zero mismatches. The monitor's reset cause was USB UART chip reset.

The startup log is `build/esp32/flash-monitor.log`, and the renderer capture is
software output. Neither establishes physical panel appearance or touch
behavior. No direct physical display/touch inspection was performed.

# Failure, remedy, and remaining uncertainty

Startup logged `i2s_channel_disable(): the channel has not been enabled yet`
during codec initialization. The current board adapter leaves enabling to
codec open; startup subsequently reported the codec ready at 22050 Hz. Audio
playback was not exercised, so audible output and the cause or impact of this
pre-enable warning remain unverified. No panic was observed in the captured
startup.

After deployment, the owner reported that Food did not work. The attached
device had a paused, active collectible session. The root Food action was mapped
as navigation (`-1`) in the UI command table, so taps were accepted without
issuing a game command. Mapping Food to `JELLI_CMD_FEED` exposed the second
condition: the global active-collectible guard rejected pet actions while a
session was paused. Feed is now allowed during a paused session; the session
remains intact and paused. On the board, the debug-injected root Food tap then
emitted a successful feed command, changed activity from idle to eating,
increased satiety from 0 to 66, and closed the menu. The paused collectible
session remained active. The test used the same UI tap pipeline but did not
verify the physical touch sensor.

# Validation

- Normal ESP32 firmware build passed on ESP-IDF 6.1.
- Standard flashes completed with transfer hash verification, including the
  follow-up Food fix.
- Post-flash debug state query succeeded and showed the saved pet collection.
- Engine-rendered menu capture confirmed the final root labels.
- Food regression reproduced and fixed; ESP32 debug tap confirmed eating starts
  and satiety increases while preserving the paused collectible session.
- Physical panel appearance, touch behavior, and audio playback were not tested.

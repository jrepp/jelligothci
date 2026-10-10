---
id: memo-023
title: Persistent volume controls
author: Codex
created: 2026-10-09
tags: [audio, hardware, persistence, validation]
project_id: jelligotchi
doc_uuid: 432cd054-7497-4de7-92d8-8f83d107da0b
---

# Request and behavior

The user requested louder sound, volume up/down in Settings, and a default 30%
higher than the previous level. The new global master setting defaults to 65/100;
50 maps to the former automatic levels. Gain is rounded to the codec's integer
control: menu taps 25 → 33, quiet coos 18 → 23. This retains the relative quiet
coo behavior. The control range maps to at most 50/36, within the adapter's existing
0–80 limit. Explicit debug sound requests retain their raw gain override.

Settings slots 5 and 6 provide 96-pixel VOL − / VOL + targets at the lower left
and right, plus a central level label. Ten-point steps clamp to 0–100. Zero shows
MUTED and skips automatic sound submission on both hosts. Accepted adjustments
use the new level for menu feedback, preserve the Settings page during activities,
and request the existing save checkpoint. Clock editing retains its original
six controls and does not change volume.

# Persistence and resources

Save v7 appends one byte per game. v1–v6 migrate to 65; v7 retains explicit zero.
The value is global, independent of pet identity and evolution. The engine rejects
values above 100 before narrowing to a byte. Preflight runs the same command on
the existing caller-owned scratch. No task, queue, heap allocation, or framebuffer
was added. The largest nine-pet save grows from 3568 to 3569 bytes within 4096.
The render key includes volume so label changes repaint immediately. Desktop
measurements retain JelliGame at 3992 bytes; JelliPetEngine grows eight bytes to
11768. Device sound logs now include the actual submitted codec level.

# Validation and device evidence

Core tests cover old-save migration, mute round trips, bounds, non-mutating
preflight, sleeping adjustments, and rounded gain mapping. UI tests cover both
buttons, save/feedback requests, immediate repaint, clamped mute, and clock-control
isolation. Validation passed: 41 desktop tests, 20 core-only tests, 41 sanitizer
tests, C formatting/static analysis/size gates, documentation checks, and ESP32
build. SDL Settings was visually inspected and CLI presses verified 65 → 75 → 65.
An additional targeted navigation/volume run passed after adding a busy-activity
navigation check. The reconnected board was rediscovered with its original serial;
its NVS was backed up to build/volume-nvs-before.bin. Deployment is held for the
user's newly reported food-screen usability fix, then both changes will flash.

Initial USB discovery did not list the ESP32, and the previous port was unavailable.
The user said they would reconnect it. Port discovery will be repeated before
flashing. Physical audibility from [memo-022](memo-022-nutrition-exercise-and-device-audio.md)
was not yet confirmed; codec write success alone does not establish speaker output.

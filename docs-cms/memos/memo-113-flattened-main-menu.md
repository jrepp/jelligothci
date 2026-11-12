---
title: Flattened main menu and direct food
author: Engineering Team
created: 2026-11-12T17:27:24Z
tags: [gameplay, menu, validation]
id: memo-113
project_id: jelligotchi
doc_uuid: f6a29da4-2cf8-4215-843a-affe91958bec
---

# Change

The [main ring](../../core/pet_ui.c) exposes Food, Water, Play, Care, Pets,
and Gifts. The [bottom control](../../core/pet_layout.c) is enlarged by 25%
in both width and height, with a raised rim and shadow for Menu, Back and Close.
Custom Gifts and game screens share this control. Its placement clears the
Dropper playfield; adjacent Settings and collectible controls retain separate
touch areas. Care retains quick care actions and Rest/Wake. The owner requested
a three-second hold on the closed Menu button for Settings, keeping it out of
the ordinary care choices. Play
filters feed-kind activities from its interactive list while retaining the
catalog and identifiers for older saves. Food starts the default meal directly;
food inventory is no longer charged or required. Existing serialized food fields
and in-flight food types remain readable. See the [playing guide](../../docs/playing.md).

# Evidence and limits

The [navigation tests](../../tests/test_navigation.c) cover the ring, direct
food and water, navigation, and filtered activity list. The [nutrition tests](../../tests/test_nutrition.c)
cover zero-stock meal save and completion; [save tests](../../tests/test_save_v20.c)
cover legacy in-flight meals. Core tests and focused desktop tests passed locally. A native-engine 3-by-3
contact sheet exposed an older clipped Back control on custom screens and an
incorrect start badge on Play pagination; both were corrected. Pets and Gifts
use distinct category icons, and Play uses the existing play icon.
A sanitizer run reached the server tests after passing earlier tests; the full
run was stopped to avoid local server contention. No current physical display or
touch evidence has been collected for this change.

Hosted sanitizer checks exposed debug integration tests that still used the old
Settings and pet-gallery routes. Their navigation was updated, and the artwork
reload test now closes the menu before sampling the pet artwork so a care icon
cannot cover its sample pixel. The affected tests passed locally. Revision `072ba44` then passed all hosted
checks, including the full desktop sanitizer suite and firmware build, before
the requested Settings hold gesture was added. Current-head hosted checks
remain the merge gate. Firmware compilation and host tests do not
establish physical touch behavior. The owner deferred flashing.

The Settings hold adds fixed state only. Desktop ARM64 `sizeof` measurements
against revision `072ba44` are: pet engine 29032 to 29056 bytes, pet UI 18408
bytes unchanged, gesture 12 to 32 bytes, touch guard 16 to 32 bytes, and queued
input 12 bytes unchanged. ESP32 ABI sizes may differ. No allocation or additional
framebuffer is introduced.

An input-queue overflow now discards stale queued gestures and enqueues a hold
cancellation, superseding the original drop-new-event policy recorded in
[ADR-009](../adr/adr-009-bounded-input-color-animation.md). This avoids leaving
an engine hold active when its release could not be queued. The current
[input contract](../../docs/development.md#input-and-animation) owns this policy.

The final hold implementation passed ten targeted sanitizer tests, including
input, rendering, debug navigation and artwork reload. Full static analysis,
formatting, complexity checks and firmware compilation passed. The initial
complexity check rejected three enlarged functions; responsibility-specific
helpers resolved those findings without changing limits. A native engine frame
at 2999 ms showed the progress bar, and fake-clock tests verified activation at
3000 ms, cancellation and release suppression. These are host observations, not
measured physical touch latency.

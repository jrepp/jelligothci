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
touch areas. Care retains quick care actions, Rest/Wake,
and Settings. Play
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
---
id: memo-028
title: Touch wake reactions and rest-based bonding
author: Codex
created: 2026-10-09
tags: [bonding, interaction, saves, sleep]
project_id: jelligotchi
doc_uuid: 6abfe2a5-5c2b-416a-a8ba-893674d8f9c2
---

# Behavior

Touching the sleeping pet on the main scene or inside a care activity dispatches
the existing Wake command. Awake activity controls retain their previous hit
handling, so the pet cannot steal routine button taps. Groggy wake-ups display
GROGGY... and add no bond. A happy wake-up first uses the curious/surprised pose
for one second, then the happy pose, with HAPPY text. The reaction lasts three
seconds total. Settings Wake and automatic wake transitions use the same rule.

Rules are authored in `content/wake.json`: 18,000 ticks (30 minutes) for a nap,
216,000 ticks (six hours) for scheduled sleep, and ten internal bonding units
(one displayed point), capped at 1,000. Only actual admitted sleeping time
accumulates; awake time clears it. The counter saturates at the longer threshold
and resets during wake before a subsequent save can be made. Repeated Wake on
an awake pet is rejected. Ordinary awake petting still has its existing reward.

# Persistence and resources

Save v9 appends one four-byte rest counter per pet, preserving the existing v8
layout. Versions 1–8 initialize unknown rest duration to zero, then apply the
existing collection migration as needed. Short-lived wake expressions are not
serialized. Tests measure a full nine-pet checkpoint at 3605 bytes, within 4096;
on the tested host the new fields fit existing padding, leaving JelliPet at
392 bytes and JelliGame at 3992 bytes. No allocation or new task is introduced.

# Verification

Tests cover one tick below and exactly at both thresholds, actual admitted
sleep advancement, persistence midway through rest, no repeated bonus after
wake/save/reload, immediate re-rest/wake without reward, bond cap, transient
reaction expiry, and tapping the pet in an activity. The UI test checks the
surprised frame followed by the happy frame. Legacy save fixtures remove the
new extension explicitly rather than relabeling a new-format payload.
`make test` and `make sanitize` each passed 42 tests; core-only passed 21.
Firmware compilation, C checks, and documentation validation passed. Groggy,
surprised, and happy activity screenshots were inspected. Hardware touch/audio
verification remains pending device reconnection.

# References

- [Wake rules](../../content/wake.json)
- [Wake implementation](../../core/wake.c)
- [Wake persistence tests](../../tests/test_wake.c)
- [Activity touch tests](../../tests/test_pet_health.c)

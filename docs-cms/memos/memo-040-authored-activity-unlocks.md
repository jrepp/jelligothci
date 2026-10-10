---
title: Authored activity unlocks and shared presentation
author: Codex
created: 2026-10-10T16:02:31Z
tags: [activities, authoring, memo, testing]
id: memo-040
project_id: jelligotchi
doc_uuid: 57c4c51d-72cf-4833-8928-2a6f35d7a5db
---

# Requested increment

The user requested pet-specific activity authoring, random fun activities,
timed meal unlocks, and reusable animations/icons across pets, like Exercise.
Breakfast is 06:00–11:00, lunch 11:00–14:00, tea 13:00–16:00, and dinner
16:00–21:00. Closing boundaries are exclusive. New recipes include dessert,
Yoga (replacing Stretch), jogging, fishing, day dreaming, nap and chess;
Movie remains the existing stable activity. The user then added drawing,
thinking, gardening, soccer, volleyball, swimming, baseball, bug catching
and science; those recipes bring the catalog to 23 activities.

Two defaults were stated while clarification remained unanswered: dessert
requires that pet to finish dinner that day, and random activities are optional
hourly choices rather than autonomous actions. These are implementation
assumptions, not recorded design approvals. No pet-exclusive defaults were
invented; authors choose eligible forms in the editor.

# Implementation and resource budget

The Activities editor saves validated recipes with stale-write protection.
Recipes define forms, windows, prerequisites, random weights, duration, gains,
location, icon, prop and reusable motion. The game enforces eligibility on
commands and preflight, with paged menus and reasons for locked actions.
Yoga's Health shortcut runs the same recipe as the Activities menu.

Random selection is a bounded weighted hash of pet ID, day and local hour.
Preflight does not advance the pet PRNG. Ordinary care/feeding remains
available outside the named meal windows. Activity completion bits are per
pet and per local day; offline completion cannot grant dessert for today.
Nap uses a resting pose and energy gain, separate from journaled sleep.

Save version 12 adds a two-byte day modulo 65,535 and a four-byte completion
mask per pet: 54 bytes for nine pets. The nine-pet save is 3,767 bytes of the
4,096-byte budget. Explicit byte-sized health/activity fields preserve the
400-byte pet and 4,080-byte game on the development host. Save encoding stays
explicit and older saves load with no completed prerequisites. Existing
moment IDs remain unchanged; the editor rejects renaming, reordering or
removal of existing recipes. Direct source edits must honor the same contract.

Presentation reuses existing sprites and poses with eighteen shared prop motions.
No new unique pet sprite sheets or heap allocation were introduced. Activity
rules need a rebuild; existing PNG hot reload only updates their artwork.

# Findings and validation

The existing tests assumed unrestricted meal times, fixed activity menu slots,
and the old save tail size. Tests now use eligible windows and the paged menu;
new regressions cover boundary times, dinner interruption, dessert reload and
expiry, stable random choices, invalid recipes and stale authoring writes.
A final clock review found that pet-time day rollover omitted display offsets;
a regression now covers local midnight with both wall and simulated time,
including a fractional-minute phase. Shrinking enum storage exposed a clang-tidy non-enum switch warning; casting
the already validated activity byte back to its enum preserves exhaustiveness.

Final validation passed: `make test` (52/52), `make core-test` (25/25),
`make sanitize` (52/52), `make lint-c` and `make esp-build`. Documentation
passed `make docs-check` and `make docs-fix` with no repairs. The save-size
regression confirms 3,767 bytes for nine pets; a version-11 fixture loads with
zero daily completions.

In Safari, the Activities view exposed all 23 recipes and 18 motion presets.
An isolated copy of content saved a pet-specific Yoga recipe, then a Science
recipe with a changed duration and axolotl-only eligibility. Tea showed
13:00–16:00 in time controls. The shared preview rendered real pet sprites.
An early automated click happened before page initialization; retrying after
content loaded exercised the intended editor. PNG captures of the SDL demo
and six activity presentations were inspected; they use existing art rather
than newly drawn activity-specific props. Generic sprites remain especially
visible for fishing, chess and the newer sports/science recipes, and can be
replaced through the editor.

Firmware compilation does not verify physical display, touch, timing or
persistence. No firmware was flashed. The concurrent Jelli Art studio branch
was preserved; this increment is based on released main and will need normal
integration with that independent work.

# References

- [Authoring workflow](../../tools/jelli-art/README.md)
- [Behavior and activities proposal](../rfcs/rfc-005-stimulus-driven-creature-behaviour.md)
- [Compatibility audit](memo-039-game-stability-and-fun-audit.md)

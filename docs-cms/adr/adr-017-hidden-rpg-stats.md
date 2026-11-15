---
title: Hidden RPG statistics for moment balancing
status: Proposed
created: 2026-11-15T20:06:54Z
deciders: Jacob Repp
tags: [architecture, gameplay, persistence]
id: adr-017
project_id: jelligotchi
doc_uuid: ab55db20-d6b2-49ed-a84e-44f43c8d7f32
---

# Context

The owner requested hidden RPG-like statistics that grow through activities and
can balance moment costs. Existing pet needs, bond, authored moments and saved
pets must retain their behavior and identity.

# Decision

Store per-pet Strength, Dexterity and Intelligence as bounded values in the
versioned save. Older saves initialize them to zero. Authored moments may grant
stat experience on completion and select one stat for cost balancing. The
[activity catalog](../../content/activities.json) owns the initial assignments;
[activity cost](../../core/activity_effects.c) owns the bounded discount. Daily
free replays do not train again. The initial values and rates are provisional
playtest tuning and do not expose the stats in the player UI.

# Consequences

Pet progression survives save and reload without altering the visible needs.
The save format grows and older firmware cannot read newer checkpoints; the
bounded codec and migration tests must be maintained. Activity authors need to
consider progression and balance together. Rates require playtesting.

# Alternatives Considered

Visible RPG meters would conflict with the requested hidden stats. Deriving
skills from current needs would lose progression when needs change.

# References

- [Activity data](../../content/activities.json)
- [Save codec](../../core/save.c)

The initial implementation admits ten training points for the selected stat on
a completed authored moment. A stat value of zero applies no discount; each
50 points reduces matching moment costs by one percent, capped at twenty percent
for a value of 1,000. Costs round upward. These values are an initial tuning
choice, not measured balance evidence. Interrupted moments do not complete.

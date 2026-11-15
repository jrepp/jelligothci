---
title: Hidden RPG stats implementation evidence
author: Engineering Team
created: 2026-11-15T20:38:01Z
tags: [gameplay, memo, persistence]
id: memo-119
project_id: jelligotchi
doc_uuid: daf5dd07-252f-43d6-aa4d-be3b60dc2af6
---

# Findings

The owner requested hidden RPG statistics trained by activities and selected
lower matching moment costs as their first balancing effect. The initial
Strength, Dexterity and Intelligence set and rates are provisional tuning;
[ADR-017](../adr/adr-017-hidden-rpg-stats.md) records the design.

A version-21 checkpoint produced by the prior writer exercises migration to the
new format with zero-valued stats. The focused test covers matching and unrelated
costs, growth, saturation and round-trip. The existing engine regression run
passed after updating legacy-layout test helpers for the appended save tail.

The first commit hook run found that an unconfigured fixture-path macro broke
standalone analysis and the save codec exceeded its file-size limit. A fallback
path and separate bounded RPG save module address those findings. Rerun results
and firmware/physical evidence must be reported separately; rates remain
unvalidated for play balance.
The combined 16-pet capacity fixture encoded to 8,164 bytes in an 8,192-byte
buffer, leaving 28 bytes for that fixture. Further persistent fields need a
save-capacity and NVS-reserve review. The combined firmware built; physical
device and play-balance evidence remain separate.

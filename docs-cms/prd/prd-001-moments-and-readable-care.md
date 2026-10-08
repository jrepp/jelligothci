---
title: Moments and readable care
status: Draft
target_release: TBD
author: Codex
created: 2026-10-08T02:39:31Z
tags: [moments, product, ui]
id: prd-001
project_id: jelligotchi
doc_uuid: d4f3594b-6d02-4da6-ab1d-9bd0d5d3e7b3
---

# Executive Summary

Make care feel like sharing a small life: breakfast, tea, going out, and watching
a movie. The user requested Moments as a main menu category with a second ring,
clock-informed suggestions, and large, readable need graphics. This draft records
that concept; its proposed mechanics and balance have not been approved.

# Problem Statement

The user found text and narrow need bars difficult to read on the physical
466x466 AMOLED. Dense status indicators also place chores ahead of enjoyable
experiences with the creature.

# Requirements

| ID | Requirement | Priority |
| --- | --- | --- |
| FR-1 | Six large category icons around the pet; MENU opens, X CLOSE dismisses | Must Have |
| FR-2 | Selecting Moments replaces the ring with breakfast, tea, going out, movie, a suggestion, and Back | Must Have |
| FR-3 | Arrow BACK returns one level; all moments remain selectable regardless of clock | Must Have |
| FR-4 | Suggest breakfast in morning, tea at midday, outings in afternoon, movies at night | Should Have |
| FR-5 | One large progressive stat tile, score 1–100 with 100 best; automatic slide and manual advance | Must Have |
| FR-6 | Visible button acknowledgement with bounded native particles; failures must still show their status | Must Have |
| FR-7 | A ritual eventually has a distinct creature response and scene | Should Have |

# Design and Current Prototype

Two 32px creature forms render at 6x; menu art renders at 96 physical pixels.
Rounded hearts, friendly cups, and warm confetti establish the tone. The tile
cycles every 10.8 seconds with a 900 ms slide by default. Existing need units map to displayed
ceil(value/10), clamped to 1–100; zero still displays 1.

Breakfast currently reuses feeding, tea and movies reuse play, and going out
reuses travel. Existing inventory, sleep, health, and cooldown rules apply.
These are prototype mappings, not complete ritual systems. Local SDL time supplies
suggestions; absent wall time uses pet-relative phase. Proposed suggestion windows
are 05:00–10:59, 11:00–14:59, 15:00–18:59, and movie otherwise. Clock changes affect
suggestions only; they never mint rewards or fast-forward the simulation.

# Resource Targets

The core must remain allocation-free. A 24-slot pool uses eight bytes per
particle, at most 224 bytes including bookkeeping, and the existing framebuffer.
Simulation uses integer steps (60 ms at the default duration scale), bounded catch-up, and expires old particles
after long stalls. Native presentation clips drawing and restores damaged regions.
Several simultaneous bursts can expand their union; hardware timing remains a
required acceptance check.

# Success and Validation

A physical-device review must establish that the creature, six icons, bottom
control, and stat number are readable and touchable without interpreting tiny
text. Desktop tests verify navigation, deterministic particle timing, bounded
capacity, and exact restoration after effects. Build success alone cannot satisfy
physical readability or performance acceptance.

# Open Questions and Polish

- Define distinctive moment durations, animation beats, rewards, and cooldowns.
- Decide how reliable local time is configured on a disconnected ESP32.
- Add accessible labels/focus for icon-only rings and a device reduced-motion setting.
- Review rounded silhouettes and confetti density on the actual panel.

# References

- [RFC-001](../rfcs/rfc-001-virtual-pet-systems-architecture.md)
- [Implementation evidence](../memos/memo-011-readable-rings-moments-and-particles.md)
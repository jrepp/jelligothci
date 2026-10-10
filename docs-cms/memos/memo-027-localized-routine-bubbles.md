---
id: memo-027
title: Localized routine bubbles
author: Codex
created: 2026-10-09
tags: [animation, particles, routines]
project_id: jelligotchi
doc_uuid: feae29f8-fada-431b-b78b-f9e447ad6f6a
---

# Behavior

Brushing emits two small rising bubbles every 160 ms near the visible pet's
centroid, offset upward by 25 percent of its visible height. Bathing emits four
falling bubbles every 100 ms across the top of its opaque bounds. Opaque bounds
and centroids come from the existing sprite metadata; no runtime alpha scans
are needed. Bubble rings have a pale highlight and cyan edge.

Leaving the routine, completing it, or sleeping clears its bubbles. Switching
between brushing and bathing replaces the previous bubble mode. Completion
celebrations still use sprite particles, independent of this cleanup.

# Bounds and verification

The existing 24-particle pool remains fixed, with eight-byte particles and no
new allocation. One eight-byte timestamp plus a mode byte and emitted flag
are added to UI state (structure padding applies). The emitter uses injected
animation time and emits a single batch after any stall. Particle motion uses
the existing injected elapsed time. Screen clipping and a six-pixel damage
radius include the full bubble rings.

Tests check mouth offset, spawn counts, bath bounds and downward motion,
bounded emission after a stall, damage bounds, and cleanup on completion/exit.
The full host suite passed 41 tests, core-only 20, and sanitizers 41. Firmware
compilation and C checks passed. Brush and bath screenshots were inspected.
Physical display verification remains pending reconnection.

# References

- [Bubble emitter](../../core/pet_bubbles.c)
- [Rendering and cleanup regression](../../tests/test_pet_health.c)
- [Interaction feedback](memo-025-distinct-interaction-feedback.md)

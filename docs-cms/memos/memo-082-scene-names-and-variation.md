---
title: Named scene layers and reproducible authored variation
author: Codex
created: 2026-10-20
tags: [authoring, memo, scenes, validation]
id: memo-082
project_id: jelligotchi
doc_uuid: 325eaa58-b124-480c-937e-914419aa4866
---

# Request and scope

The owner requested layer names and authorable randomness after discussing street
traffic. This increment implements named layers and seeded initial scene variation.
Independent vehicles, foreground actor occlusion and traffic spawning remain later
work; this feature chooses a repeatable initial composition for the existing renderer.

# Implementation

[The shared schema/compiler](../../tools/assets/scene_data.py) accepts optional layer
names, scene seeds and bounded per-layer variation. Position jitter, travel variation
and appearance probability resolve once when compiling a scene. Older v1 documents
remain valid and produce identical runtime tables when variation is absent or zero.
Names are authoring labels and do not change artwork identities or runtime asset IDs.

[The editor](../../tools/jelli-art/scenes.js) displays layer names and supplies seed,
jitter, travel variation and appearance controls, a resolved-value readout and a
“Try another variation” button. Saves, stale-write detection and browser drafts use
the existing document pipeline. Each new layer receives an unused seed. Reordering,
renaming or changing shared artwork does not replace a layer's seed. Authors can
choose matching seeds deliberately to coordinate choices.

[The browser helper](../../tools/jelli-art/scene_variation.js) and Python compiler
share a fixed unsigned integer sampling contract: scene and layer seeds form a
32-bit initial value, the LCG advances once per x/y/travel/chance sample, and its high
24 bits supply bounded choices. Generated positions and travel percentages clamp to
the existing runtime ranges. Hidden authored layers remain hidden even at full
appearance chance; travel variation requires horizontal repeat. Repeated image tiles
share their layer's selected variation. No per-frame randomness, new C state, heap
allocation or runtime RNG is needed. Saving a different seed rebuilds the table.

Starter scenes now have descriptive names and distinct layer seeds. Their variation
ranges remain zero with full appearance, preserving the reviewed street composition.
The [authoring guide](../../docs/artwork.md#compose-background-scenes) explains the
controls and recommends stable ground layers.

# Validation

The Python scene suite passed eight tests, including old-document compatibility,
invalid fields, bounded values, stable identity and browser/compiler parity for seed
extremes. TypeScript passed without new baseline exceptions. The pinned Linux ARM64
browser run passed all eight selected scene cases with screenshot comparisons enabled,
including name persistence, probability controls, rerolling, reorder stability, phone
layout, stale writes and continued edits during a save. No baselines were changed.

The desktop build succeeded. All five selected native/service suites passed:
scene, pet scene, studio server, scene schema/compiler and real-engine game preview.
The native preview regression sets every layer's appearance chance to zero, rebuilds
incrementally and verifies the sampled background is only the base fill. The desktop
authoring screenshot was inspected for names and control layout. `make docs-check`
and `make docs-fix` passed with no repairs or unresolved findings.
No device build, flash or physical test was performed for this authoring increment.

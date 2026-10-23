---
id: memo-103
title: Background editor navigation and validation
author: Codex
created: 2026-10-22
tags: [authoring, scenes, validation]
project_id: jelligotchi
doc_uuid: eaee3808-778a-48bd-a65e-efcf926d0dd1
---

# Findings and implementation

The expanded activity catalog made the old scene selector cumbersome, especially
below the phone preview. Rebuilding the inspector could silently drop invalid
numeric input because only valid values had reached the document. Reload also
reset the selected background and Paint lacked a direct route back to its scene.

The [editor](../../tools/jelli-art/scenes.js) now places searchable background
navigation above the preview. Previous/next controls follow the filtered list;
no matches leave the current background available. Switching retains per-background
layer selection and resets preview motion to the destination's activity speed.
Reload retains the selected scene when it still exists.

Shared-art navigation selects the corresponding layer in another background.
Artwork dimensions and usage counts accompany the Paint action, which provides a
return button. One-pixel nudges use the existing bounded document undo history.
Required numeric fields and guarded navigation preserve invalid input for correction.
Inline checks distinguish invalid fields from composition advisories; the shared
[validator](../../tools/assets/scene_data.py) adds scene/layer context to errors.
The [authoring guide](../../docs/artwork.md#compose-background-scenes) owns usage instructions.

# Validation

All 11 focused schema/compiler tests, 36 HTTP server tests and 18 browser tests
pass, covering navigation on desktop and phone,
editing-position retention, invalid-input preservation, nudges/undo, shared artwork
and Paint return, alongside existing saves, concurrency, recovery and accessibility
checks. TypeScript validation uses the existing baseline without new exceptions.
Desktop and phone scene screenshots were inspected; no browser or type-check
baseline changed. Documentation validation and repair passed without repairs.
This increment changes editor behavior and error context, not runtime rendering or
firmware. Physical device testing is not relevant to the editor controls.

The first expanded browser run found that Paint is represented by detail view plus
paint mode, rather than a standalone paint view. Updating the return button's
visibility predicate fixed the missing route back; its end-to-end case verifies
both destination scene and selected shared-art layer.

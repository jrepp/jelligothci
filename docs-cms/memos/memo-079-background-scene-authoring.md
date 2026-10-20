---
title: Background scene authoring and muted reusable art kit
author: Codex
created: 2026-10-20
tags: [artwork, memo, scenes, validation]
id: memo-079
project_id: jelligotchi
doc_uuid: e353036b-91da-4b17-8e6a-2614a293cf08
---

# Scope

The owner requested background motion-layer editing in Jelli Art, an engine design
for device-tilt parallax and travel, muted earth-tone background palettes, and a
reusable art library. The owner chose sideways travel first, with forward travel
eventually. [RFC-011](../rfcs/rfc-011-layered-backgrounds-and-motion.md) records the
proposed runtime contract and adoption boundaries. No production sensor or layered
runtime integration is implemented in this increment.

# Implementation

[Scenes](../../tools/jelli-art/scenes.js) edits an ordered composition: shared art
references, placement, visibility, independent tilt amplitudes, travel percentages
and horizontal repeat. It supports duplicate scenes, a shared library selector,
opening a layer in Paint, neutral reset, forward/reverse sideways playback and a
pet reference. The view adapts from three columns to a stacked phone layout.
[The API](../../tools/jelli-art/jelli_art.py) confines saves to the content root,
requires a base hash, validates under the shared lock and writes atomically.
Save acknowledgements preserve later edits; draft recovery and stale-save
confirmation reuse the existing studio contracts. Read-only copied-art trials
cannot save scenes. [Validation](../../tools/assets/scene_data.py) owns the schema
and limits; travel requires repeat and layers reference registered backgrounds.

[New artwork](../../tools/jelli-art/creation.js) offers a reusable layer without a
location. Existing background/location creation remains available. The background
palette defaults to the kit; pet artwork and palettes are unchanged.

The [native kit recipe](../../assets/slice/source/background_kit.py) creates
reusable sky, hills, town, shop, trees and paving geometry. The
[seed document](../../content/scenes.json) demonstrates shared pieces in multiple
compositions. Existing location recipes now use the earth ramp, preserving their
identities and geometry. Only background outputs were accepted from the temporary
regeneration tree; generated care/effect candidates were not copied over artwork.

# Findings and remedies

- The existing device motion interface provides events rather than orientation.
  The RFC calls for a separate valid, timestamped orientation input, acquired off
  the render path, with neutral fallback and independent travel control.
- The first regeneration invocation omitted the existing healthy-art palette
  argument and failed before writing source art. Supplying the manifest palette
  generated candidates successfully; asset validation passed afterward.
- The initial JavaScript check found an unexported shell helper and an untyped
  event target. Use the public shell sync API and the typed input accessor; no
  new TypeScript baseline exceptions were added.
- The expanded catalog broke a compression test's historical fixed block/byte
  counts. The test now uses the actual captured precompression byte count while
  retaining independent native byte comparison and mismatch rejection.
- Phone inspection exposed offscreen screen-reader labels extending document
  width when the mode list scrolled. Position labels relative to their tab;
  retain the viewport overflow assertion rather than hiding the symptom globally.
- Review found that separately reading the scene document and its hash could
  pair old content with a new hash. Load under the shared lock and hash exactly
  the returned bytes; a regression simulates an external edit between reads.
- The full local browser run needed the new Scenes tab added to seven mode
  snapshots and reported one palette accessibility mismatch. The pinned Linux
  run required only those tab additions; the accessibility baseline and screenshot
  baselines were unchanged. Documentation repair sorted the new documents' tags;
  those were the only automatic repairs.

# Validation boundary

Validation was performed on the working branch based on `e98342a`.

| Layer | Evidence |
| --- | --- |
| Artwork | `build_slice.py` validated 127 assets. A pixel comparison against the base revision confirmed all six flat backgrounds preserve geometry and alpha under the earth-ramp substitution. |
| Engine/build | `make test core-test sanitize` passed: 87 desktop, 39 core-only, and 87 sanitizer tests. |
| Studio backend | All existing standalone studio suites passed. The final creation suite passed 15 tests; the scene suite passed four including invalid documents, stale/read-only write isolation and exact read/hash pairing. |
| Browser | The first full local run passed 270/278 cases; seven failures were the expected mode-tab snapshot additions and one was the local palette accessibility mismatch. All six focused scene cases then passed, including phone geometry, reload recovery, stale-save cancellation and in-flight save editing. |
| Pinned browser verification | Linux ARM64 using the pinned Playwright image passed all 87 selected scene, keyboard, layout and view cases with updates disabled and screenshot comparisons enabled against existing baselines. The preceding 28-case baseline generation changed only the seven mode snapshots. |
| Static and documentation | TypeScript reported no new baseline errors; paint-tool tests, workflow-pin validation, `make docs-check` and `make docs-fix` passed. |

Desktop and phone scene screenshots were visually inspected. The large full suite
was not repeated after the targeted fixes; the affected browser cases were rerun
in the pinned environment. Final backend snapshot-race and creation-palette fixes
were verified by their focused suites. No firmware was built or flashed, and
neither physical motion nor physical display behavior is established by this work.

---
id: memo-102
title: Activity scrolling backgrounds
author: Codex
created: 2026-10-22
tags: [art, memory, rendering, validation]
project_id: jelligotchi
doc_uuid: 807bd7a1-913d-48b2-9cef-609b0664c18a
---

# Scope and implementation

The owner requested scrolling backgrounds for all activities with shared art where
appropriate and distinctive settings. The [scene catalog](../../content/scenes.json)
covers the [activity catalog](../../content/activities.json) and workout. Existing
landscape and street artwork is reused, with original small motifs for the cinema,
library, meals, gardening, fishing, sports and other settings. The
[reproducible recipe](../../assets/slice/source/activity_backgrounds.py) generates
candidates outside source artwork for review before copying them into the catalog.

[Automatic selection](../../core/pet_activity_scene.c) is presentation-only. The UI
owns persistent composition storage; explicit previews retain priority. Idle and
resume restore location backgrounds. Activity identity, pet identity and interaction
deadline reset motion. Pauses freeze travel and stalls discard excess elapsed time.
The [development contract](../../docs/development.md) describes cache ownership;
[authoring guidance](../../docs/artwork.md#compose-background-scenes) describes bindings.

# Budget findings and remedy

The first candidate set exceeded the packed metadata block limit and left too
little live-bank headroom for animation editing. Pairing motifs into shared strips,
using shorter repeating ground tiles and concise asset keys resolved those failures
without increasing storage limits. The measured final catalog uses 224,576 live
pixels and 28,072 mask bytes; limits remain defined in the
[live pack publisher](../../tools/assets/live_assets.py). The live publisher also assumed every background
was full-sized; its validation now accepts the compositor's bounded layer geometry.

Measured on the desktop build, the engine occupies 28,968 bytes including its
persistent activity cache, below the existing 32 KiB engine bound. The game remains
8,080 bytes. These are host measurements, not ESP task-stack or heap measurements.

# Verification and remaining uncertainty

Native rendered contact sheets were inspected for all settings. Activity tests cover
catalog selection, visible scrolling, gameplay-state preservation, pause and stall
handling, activity transitions, explicit overrides and missing-art recovery. Scene
editor browser tests cover desktop/mobile layout, binding persistence and unbound
copies. The ESP32 application builds successfully; no firmware was flashed and
physical display readability, touch behavior and frame timing remain unverified.

The procedural recipe was regenerated into a separate candidate directory and
matched all shipped scene definitions, asset records and PNG bytes. The native
preview also rendered an automatically selected breakfast scene through the real
activity command. C lint, core tests, scene editor browser tests and documentation
validation pass.
The full host suites exposed a new test-fixture path issue on macOS: the temporary
directory alias differed from its resolved path. Passing the resolved source root,
as the production publisher does, fixed the geometry test in both desktop and
sanitizer builds. Full live-pack export/loading passed independently. Documentation
repair only sorted this memo's tags; there are no unresolved documentation findings.

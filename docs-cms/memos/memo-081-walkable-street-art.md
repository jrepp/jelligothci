---
title: Walkable street composition and reusable curb details
author: Codex
created: 2026-10-20
tags: [artwork, memo, scenes, validation]
id: memo-081
project_id: jelligotchi
doc_uuid: 18dd9e93-52bc-4428-909a-d28a8a1bfae7
---

# Request and implementation

The owner asked for a street-walking composition with a sidewalk, foreground
pieces and buildings behind the pet. The previous scene placed paving below the
home actor's contact position, leaving the pet visually floating over a facade.

[The native art recipe](../../assets/slice/source/background_kit.py) adds reusable
streetfront, sidewalk and street-edge images using the existing muted earth palette.
[Market street](../../content/scenes.json) now separates distant town silhouettes,
a continuous storefront row, the clear walking lane, curb, road and nearby props.
Planters, a bench, bollards and a drain provide foreground detail below the actor.
The layers have independent sideways speeds; distant buildings move more slowly.
Their vertical tilt stays zero to preserve the street's ground alignment.

The sidewalk aligns with the current home actor layout. Other activity layouts
may need their own scene placement. All pieces still use the existing background
compositor: these curb details do not require a new pass over the actor. No C API,
walking-animation behavior, gameplay location or saved progression changes were
made. Existing garden compositions and the earlier kit pieces remain intact.
The [manifest](../../assets/slice/assets.json) owns the new asset IDs and metadata.

# Validation

Candidate images were generated outside the source tree and only the three new
pieces were copied into the art library. The asset validator accepted 130 assets
and reported 412,008 raw bytes. A desktop incremental build succeeded.
The scene document/compiler suite passed all five cases. Native day and default
lighting captures were generated under `build/scenes`; the daytime screenshot was
visually inspected to confirm the pet sits on the sidewalk and curb props remain
visible beside the HUD. A second native frame after two seconds of travel was
also inspected: the sidewalk stays under the pet while the independent layers
advance. All seven selected desktop suites passed: scene, pet scene, pet locations,
pet render, Jellipak build, studio scenes and the real-engine preview service.
`make docs-check` and `make docs-fix` passed with no unresolved findings.

This is desktop artwork evidence. No device build, flash or physical display test
was performed for this art increment; the earlier runtime build evidence remains
in [memo-080](memo-080-background-scene-runtime.md).

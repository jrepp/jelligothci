---
title: Creature contact anchors
author: Codex
created: 2026-10-08T03:17:38Z
tags: [assets, memo, rendering, validation]
id: memo-014
project_id: jelligotchi
doc_uuid: 2cbf2ed0-2a6f-4b6e-b288-ec9c39997b61
---

# Observation and Implementation

The user requested a calculated bottom anchor so transitions between PNG poses
keep the creature planted and sitting poses are easy to place. Previously every
32x32 frame used the same top-left origin, although its visible bottom varied
between source rows 28 and 29. That could shift the feet by six physical pixels
at the current 6x scale.

The build now derives a contact anchor from alpha coverage for every creature:

1. Find the last row containing any opaque pixel.
2. Collect opaque pixel centers in that row and the two preceding rows.
3. Average their horizontal coordinates, weighting every covered pixel equally.
4. Place the vertical anchor at the bottom edge of the last opaque row.

The horizontal value is a centroid, while the vertical value is the contact
edge. Averaging Y as well would put the anchor inside the feet and allow their
lowest edge to move with the band shape. The three-row band avoids relying on
one rounded tip pixel. PNG alpha is binary, matching the runtime coverage mask.
Empty sprites are rejected. Upper-body motion outside the band has no effect.

Coordinates use unsigned Q8 source-pixel edge units. Pixel centers are x+0.5;
the bottom coordinate is exclusive and may equal the image height. Integer
round-half-up is applied when encoding X, then once when scaling onto the display.
The C renderer and HTML preview use the same rule and derived values.

The native creature contact point is fixed at (233, 256). A shorter sitting or
sleeping pose keeps that ground line; its head lowers naturally. Both forms and
all six states use this placement. Navigation icons and stat pictograms retain
their existing placement.

# Asset Pipeline and Resource Cost

[Geometry extraction](../../tools/assets/sprite_geometry.py) runs inside the
existing PNG validation/export path. Derived ground_anchor_q8 values appear in
the generated preview manifest, asset report, and immutable C asset records;
they are recomputed from source PNGs rather than maintained as duplicate authored
metadata. CMake tracks the geometry helper so algorithm edits regenerate firmware
assets. Existing generic manifest pivots remain separate and unchanged.

Two uint16 fields add four bytes of declared metadata per asset record. On ESP32
the table grows by 188 bytes across 47 records; non-creature anchors are zero.
No PNG pixels, sprite IDs, or raw pixel payloads changed. No runtime pixel scans,
heap allocation, or extra framebuffer are needed. The linked firmware is
0xa88d0 (690,384) bytes, 240 bytes larger than the preceding tuning image and
still 34 percent below the 1 MiB app partition capacity.

The art sheet aligns creature cards to a common ground line and displays each
contact coordinate. Its round preview has a Show contact anchor toggle for
checking pose changes. Native screenshots and a comparison guide are generated
under build/ground-*. The guide line is a review overlay, not shipped artwork.

# Validation and Limits

- All 17 desktop tests, seven SDL-free tests, and 17 sanitizer tests passed.
- Synthetic geometry tests cover unequal feet, padding translation, single-pixel
  contact, upper-body changes, shorter poses, and rejection of empty art.
- Native raster tests cover all 12 frames: lowest visible pixels end at y=255,
  nothing crosses the y=256 ground edge, and the bottom-band centroid stays within
  one physical pixel of x=233 after integer placement.
- Browser checks cover pose switching, 12 grounded cards, three gallery scales,
  the anchor toggle, and mobile width. Native SDL idle/asleep snapshots were inspected.
- C formatting, static analysis, size gates, asset validation, and ESP32 compilation
  passed. This revision has not been reflashed; physical transition feel remains
  unverified for the new alignment.

The algorithm uses all opaque pixels in the bottom band. Authored detached
shadows, sparkles, or a long tail below the body would also influence the contact.
Keep such effects in separate sprites; explicit contact overrides or body masks
can be introduced when artwork needs them. Current art needs neither.

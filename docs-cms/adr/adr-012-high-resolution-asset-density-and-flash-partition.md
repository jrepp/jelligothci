---
title: Store high-resolution sprites by density with 8-bit alpha and an 8 MB app partition
status: Proposed
created: 2026-10-09T01:11:41Z
deciders: Michael Weingartner
tags: [architecture, assets, embedded, esp32, rendering]
id: adr-012
project_id: jelligotchi
doc_uuid: d1dc1b2c-21ea-4b92-af67-7cb527d7236d
---

# Context

Every sprite was 16-colour pixel art: RGB565 pixels plus a 1-bit mask, drawn by
integer nearest-neighbour upscaling. Creatures are 32×32 source pixels shown at
6x (192×192 on screen); ring icons are 32×32 at 3x. Layout code (actor anchors,
touch, centred icons, particles, battery and gallery placement) works in those
source pixels, and cached bounds are `uint8_t`. The request to restyle every pet
and icon at high resolution needs art drawn at, or close to, its on-screen size
with soft edges, without moving any layout.

Art is embedded as immutable C arrays, which the ESP32 build places in flash
rodata. The default single-app partition table gives the app 1 MB; the legacy
firmware used about 0.9 MB. A full high-resolution set is about 1.65 million
stored pixels, roughly 5 MB as RGB565 plus alpha. Saved games live in the `nvs`
partition at 0x9000, which must survive a reflash.

# Decision

**Density.** A `JelliAsset` gains `density` D (stored pixels per logical pixel;
0 or 1 is legacy) and an optional `alpha` plane. `width`/`height`, ground and
centroid anchors (Q8) and the cached bounds stay in LOGICAL pixels; pixel planes
are stored at (width·D)×(height·D). Build tools measure coverage (alpha ≥ 128) at
stored resolution and divide by D: bounds round outwards, centroids and contact
points round half up. Nearest-upscaled art therefore keeps identical metadata.

**Sampling.** Drawing at logical scale s: s = D is 1:1; s a multiple of D is a
nearest upscale; s a divisor of D is a k×k box filter (k = D/s) over
premultiplied colour and alpha, in integers. Other pairs draw nothing. Each output
pixel is composited with straight alpha (alpha 0 skips, 255 copies). Night tint
and dimming apply to the source colour before blending, and the canvas clip and
round panel mask apply as before. Only the clip rectangle is visited, and region
redraws repaint the background first, so partial and full redraws stay identical.
Magic particles use the same sampler and multiply their life fade into alpha.
Legacy art (D = 1) renders pixel-identically to the previous renderer.

**Densities.** Creatures 6 (192×192 stored), menus 3, icons 6, meters 2,
health 3, effects 6, prizes 2, battery 4. Every scale the renderer draws each
kind at divides or is a multiple of its density; the builder enforces that
table. Props, backgrounds and the font stay legacy-only.

**Pipeline.** A manifest entry opts in with `"density": D` and a path under
`hires/<kind>/` to an 8-bit RGBA PNG of logical size × D (any colours). The
builder exports RGB565 + A8 (+ an inspection-only derived mask); the embedder
emits `const` pixel and alpha arrays and no mask; the desktop live pack (JLAP v2)
carries density and alpha and its C decoder re-measures geometry with the same
formulas. The planned-pack ceiling becomes 6 MiB; a test pins the real total.

**Flash.** `ports/esp32/partitions.csv` keeps `nvs` (0x9000, 24 KB) and
`phy_init` (0xf000, 4 KB) exactly where the default table put them and gives the
`factory` app 8 MB at 0x10000, selected through `sdkconfig.defaults`. Both boards
use the shared 16 MB layout; about 7.9 MB above 0x810000 stays unallocated.

# Consequences

## Positive

- Art can be authored at or near on-screen size with anti-aliased edges and any
  colours, kind by kind, without touching layout or game logic.
- Legacy and high-resolution assets coexist; migration is one manifest entry.
- Partial redraws remain deterministic; no heap, floating point or scratch
  buffer is added to the core.
- The saved game survives reflashing to the new table.

## Negative

- Embedded art grows from 165 KB to about 4.9 MB of rodata, and the firmware
  image from about 0.9 MB to about 5.7 MB, so flashing takes longer.
- Drawing a full-resolution creature reads about 110 KB from flash; on the
  ESP32 that is mostly cache misses at the board's 80 MHz DIO flash speed, so full
  redraws of large hi-res sprites cost more there than on desktop (measured only
  on desktop so far).
- The desktop live-art host reserves about 20 MiB when `--asset-pack` is used.

## Neutral

- Generated `sdkconfig` files do not re-read defaults; existing checkouts must
  delete them once (see the hardware guide).
- Flashing writes the new partition table; erasing the whole flash would still
  clear saves, as before.

# Alternatives Considered

## Larger palette pixel art at a higher grid

Keeps 1-bit masks and nearest scaling but cannot anti-alias edges or blend, and
still needs a new size for every kind. Rejected: it does not deliver smooth art.

## Store art at on-screen size only (no density)

Simpler drawing, but icons are shown at several scales (e.g. 6x in rings and 2x
in the stat tile) and layout units would change everywhere. Rejected in favour of
logical units plus integer up/down sampling.

## Put art in a separate data partition or file system

Would keep the app small, but needs a loader, a mapping or copy into RAM, and a
second flashing artifact. Rejected for now; rodata is mapped directly and the
ESP32-S3 32 MB data window has ample room.

# References

- [ADR-007: Bounded C and enforced quality checks](./adr-007-bounded-c-and-enforced-quality-checks.md)
- [ADR-011: Selectable ESP32 board variants](./adr-011-selectable-esp32-board-variants.md)
- [Slice artwork guide](../../assets/slice/README.md)
- [Artwork and live authoring](../../docs/artwork.md)
- [Hardware guide](../../docs/hardware.md)

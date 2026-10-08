# Jelligotchi slice artwork

The review set contains 94 PNG files: 24 creature frames, 12 small icons, 13 ring icons,
five meter pictograms, nine health icons, eight celebration sprites, nine collectible prizes,
seven battery-status icons, two backgrounds, four props, and one 96-slot bitmap font atlas.
Mint is the baby form; Lilac is the grown form; the ninja is a third pet (form 2)
that never evolves. The SDL and ESP32 pet builds embed these assets using
`tools/assets/embed_slice.py`. Physical board appearance remains unverified.

## Preview and validate

From the repository root:

```sh
./scripts/uv run --python 3.12 tools/assets/build_slice.py
open build/assets/preview.html  # macOS; otherwise open this file in a browser
```

The self-contained HTML needs no server or network connection. It has form and
pose controls, a round-panel mockup, native/3x/4x sheet scales, dark/checker
backgrounds, a font sample, and per-image downloads. It respects reduced-motion
preferences. Its gestures and rewards are visual previews, not a game simulation.

The script uses repository-local uv, Python 3.12, and Pillow 12.0.0 via inline
script metadata. First use may download those tools. No global install is needed.
Generated previews, contact sheets, raw pixels, and reports stay under `build/`.
The HTML template is tracked in `tools/assets/preview.html` and embeds the current
manifest and PNGs when built; editing a source image requires rebuilding it.

For opt-in desktop PNG hot reload, use `make run-live`; see the
[live authoring guide](../../docs/artwork.md). It recomputes frame measurements
without resetting game state. Ordinary launches and firmware use embedded art.

## Source inventory

| Directory | Contents |
| --- | --- |
| `creatures/` | Mint, Lilac, and ninja forms, each with idle A/B, eating, happy, asleep, unwell, curious, and content; 32x32 pixels. Ninja IDs 1031–1038 in that order |
| `icons/` | Basic care, food, play, clean, rest, wake, medicine, gift, reward, inventory, back, confirm; 16x16 pixels |
| `menus/` | Thirteen 32x32 category, moment, and close icons; rendered at 3x in rings |
| `meters/` | Five 32x32 stat pictograms; used at 2x for manually selected stat tiles (heart also represents mood) |
| `health/` | Nine 32x32 clicker icons, including the dental sequence |
| `effects/` | Eight 16x16 star, heart, orb, comet, fairy-wing, music, idea, and rainbow celebration sprites |
| `prizes/` | Nine unique 32x32 keepsakes: butterfly, pearl tooth, breakfast sun, tea sprite, movie star, bubble gem, moon charm, rainbow seed, friendship bow |
| `battery/` | Seven 16x16 status icons (IDs 12001–12007): empty, low, medium, high, charging, plugged, unknown; drawn at 2x on home, high at 4x in Settings |
| `backgrounds/` | Two 64x64 neutral home/garden scenes with black vignettes |
| `props/` | Food bowl, closed/open gift, and bed; 24x24 pixels |
| `font/` | 128x72 atlas: 16 columns by six rows of 8x12 cells, ASCII 32–127 |
| `source/` | Original generated atlases (`creatures-props-atlas.png`, `ninja-atlas.jpg`), their exact prompts, the pixel authoring recipe (including `battery_icons.py` and `ninja_art.py`), and glyph patterns |

`assets.json` assigns explicit IDs, paths, frame sizes, bounds, pivots, palette,
and clip timings. PNGs in the asset directories are the editable runtime
art sources. The large generated atlas is retained for provenance and reproduction;
it is not part of the runtime payload. Keep IDs stable when revising art.

Sprites use 16 shared opaque colors plus transparent pixels; backgrounds use a separate 16-gray palette. Alpha is
binary. The font deliberately uses small-cap forms for lowercase letters; slot
127 is a fallback glyph, not a printable ASCII character. The font is cream on
transparent; preview it against a dark surface. Recovery reuses the unwell pose
with a care mark and text. Gift/reward responses reuse happy; waking returns to
idle. No additional poses are implied by the preview controls.

## Exports and validation

The builder checks the exact inventory, unique IDs/paths, dimensions, palette,
binary alpha, bounds, pivots, glyph occupancy, clip references, and durations.
It writes:

- `build/assets/preview.html`: portable review sheet with embedded images.
- `build/assets/contact-sheet.png`: static review image.
- `build/assets/pixels/`: little-endian RGB565 and separate coverage masks.
- `build/assets/report.json`: byte sizes and SHA-256 hashes of binary exports.

RGB565 files contain row-major 16-bit words. Masks are row-major, MSB-first with
whole-byte row padding. Font `.bits` is glyph-major: 12 one-byte rows per glyph.
Transparent RGB is zero; coverage, not a color key, controls transparency. A host
must decode byte order before writing a native-endian surface. No C structs or
finished content-pack headers are emitted.

Actual raw pixel payload is 168,704 bytes. The RFC's 8,192-byte definition and
4,096-byte metadata allowances bring the planned pack to 180,992 bytes, 15,616 bytes
under the 192 KiB (196,608-byte) ceiling; those allowances are not a completed game
pack or a linked firmware measurement. The ceiling was raised from 160 KiB, which
had 256 bytes spare, for the ninja's eight frames (17,408 bytes).

To reproduce candidate PNGs without overwriting reviewed source art:

```sh
./scripts/uv run --python 3.12 assets/slice/source/authoring.py
```

Candidates go to `build/assets/reimport/`. Review and explicitly copy desired
changes into the source directories, then rebuild. The ninja frames regenerate
byte-identically from `source/ninja-atlas.jpg`; see the ninja section below. The authoring recipe samples
rounded quarter-grid boundaries from the actual source size, resizes using nearest
neighbor, thresholds alpha at 128, and selects the nearest shared palette color.
Do not assume the image generator honored the requested dimensions. It returned
1254x1254 for this atlas; see the validation record in memo-007.

## Provenance

Creature/prop art was generated for this project with the OpenAI built-in image
generation tool. The exact prompt is [source/imagegen-prompt.txt](source/imagegen-prompt.txt).
No uploaded reference images or external character designs were supplied. The
source atlas was retained unchanged. Cropping, sizing, alpha normalization, and
palette conversion are deterministic export steps recorded in `source/authoring.py`.

Icons and the small-caps font were authored as pixel geometry/patterns by Codex
for this project; no external icon set, font file, or stock asset was used.
No third-party attribution or separate asset license is asserted by this record.
The generated atlas is a review candidate, not evidence of human art approval.

The HTML uses system fonts for its surrounding explanatory text. Only the bitmap
font sample and round-panel labels use the game's actual font pixels. Contact-sheet
captions use Pillow's built-in font and are not part of the shipped game assets.

## Readability and celebration pass

Creature frames now occupy 192x192 physical pixels (6x); ring icons occupy
96x96. The bottom control shows MENU when closed, an X for closing, or an arrow for going back.
Care and social hearts use rounded lobes and a soft tip; cups, toast, and the
social heart include small expressions. These changes retain the shared palette,
binary transparency, stable IDs, and exact native dimensions.

The main tile can show Mood, Food, Energy, Hygiene, Play, Social, Bond, or Sleep.
Swipe sideways to choose; activity completion briefly celebrates each improved
stat. It does not cycle automatically or respond to taps. Mood and Bond reuse
heart art, and Sleep reuses the crescent.
The clean circular crescent replaces both rest icons. Night scenery uses a
procedural crescent and a slow gray fade; sleeping Zs reuse the bitmap font in
medium blue rather than adding another PNG. Dental overlays add floss, mouthwash,
spit, and a folded cleanup cloth (8006–8009). The preview demonstrates the six-step
routine and varied 1–3 tap counts for the first brush and floss phases.

Celebrations use tiny masked star, heart, orb, comet, and wing sprites, with integer
RGB565 fades. Eight particles acknowledge accepted presses; a completed multi-stage
healthy activity uses up to 24. Rejected presses use three quiet square/cross marks.
Celebration particles last at most 1.74 seconds at the default 300 percent UI duration scale.
Each particle remains eight bytes; the 24-slot pool fits within 224 bytes, with
no extra framebuffer or runtime allocation. Particle type and scale are packed
into the style byte. The art budget was 160 KiB at the time (now 192 KiB; see the
ninja section). Physical readability and touch feel still need review.

Menu plates use a thin neutral edge and plain gray fill, supporting the icon
rather than competing with it. The movie screen/clapper is squared off and loose
decorative marks have been removed. Creature names use the 8x12 font at 3x (36px,
50 percent larger); name and `@ HOME` / `@ GARDEN` are white with a black drop shadow.

## Creature contact anchors

The asset builder derives each creature's ground contact from its lowest three
opaque rows. X is the centroid of those pixel centers; Y is the lowest opaque
edge. The renderer aligns that point at (233, 256), so sitting/asleep frames stay
planted while their heads lower. Original PNGs and generic manifest pivots are
unchanged. The generated report and preview expose `ground_anchor_q8` in units
of 1/256 source pixel; C receives the same immutable metadata.

Creature cards share a ground guide. Toggle **Show contact anchor** in the round
preview to compare poses. Keep detached shadows/effects in separate sprites so
they do not influence contact detection. See
[memo-014](../../docs-cms/memos/memo-014-creature-contact-anchors.md) for the formula,
resource accounting, and validation.

## Cached frame measurements

The build calculates full opaque-pixel centroids (`centroid_q8`) for every asset,
in addition to creature contact anchors. Immutable generated C frame descriptors
contain both anchors and exclusive alpha bounds. The actor caches its current
descriptor and derived screen bounds; drawing never rescans alpha for layout.
Home uses contact (233,256); a healthy clicker uses (233,350) with its target above
the cached head bound. Open rings center the creature and icons by full pixel
centroid in both axes. See [the implementation and action audit](../../docs-cms/memos/memo-015-healthy-activities-and-action-audit.md).

The settings gear uses six broad teeth, a small hub, and one highlight for a
heavy cartoon silhouette. The clock reuses this asset at 1x; no new art payload.

## Battery status icons

Seven 16x16 icons share the ink outline and cream case of the care icons. Level
is shown by bar count (none with an exclamation mark, then one, two, or three
bars), so it never depends on color alone; low and empty also say `LOW` in the
caption. The bolt marks charging, the plug marks external power without a
battery, and a question mark marks an unknown level. Charge complete reuses the
three-bar icon. Separate high and full icons would have exceeded the then 160 KiB planned
pack ceiling by 288 bytes, so the set uses three bars rather than four. The
reproducible recipe is `source/battery_icons.py`; see
[memo-019](../../docs-cms/memos/memo-019-battery-status-indicator.md).

## Ninja pet

The ninja's eight 32x32 poses (IDs 1031–1038: idle A, idle B, eating, happy,
asleep, unwell, curious, content) were generated with Gemini image generation
(`gemini-3-pro-image-preview`, 1:1). The exact prompt is
[source/ninja-imagegen-prompt.txt](source/ninja-imagegen-prompt.txt); the returned
1024x1024 JPEG is kept unchanged as `source/ninja-atlas.jpg`. It is a 4x4 grid of
256px cells on flat magenta; only the first eight cells are used.

`source/ninja_art.py` cuts the frames deterministically. Pixels with red and blue
above 190 and green below 170 are background. The sheet was drawn on a logical
grid of 8.0 source pixels; for each cell the sampling grid's phase is searched
within ±4 pixels to minimise disagreement inside each block, then every logical
pixel takes the majority class, mapped to the nearest shared-palette colour. Each
frame is cropped to its opaque bounds and placed centred at x=16 with its feet on
y=30, matching the creature pivot. A scripted paintover then removes JPEG spill:
gold is kept only in the eating pose (the rice ball) and becomes cream elsewhere;
in the open-eyed poses (idle A/B, eating, curious) each small dark blob fully
enclosed by cream or pink face pixels becomes a crisp 2x2 deep-plum eye. Pillow is
pinned to 12.0.0, so JPEG decoding and the output PNGs are reproducible.

Clips 5015–5021 mirror the Mint and Lilac clips. Frame selection uses one table of
eight pose IDs per form, so the ninja reacts with the same state rules. The frames
are taller than the jellies' (up to 29 rows), so the 6x sprite reaches about
y=82–94 on the home screen, below the name and location lines. The generated sheet
is a review candidate; physical AMOLED readability has not been checked. See
[memo-020](../../docs-cms/memos/memo-020-ninja-pet-and-three-pet-roster.md).

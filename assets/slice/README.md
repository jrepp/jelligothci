# Jelligotchi slice artwork

The review set contains 110 PNG files: 38 creature frames, 12 small icons, 16 ring icons,
five meter pictograms, 10 health icons, eight celebration sprites, nine collectible prizes, two backgrounds, nine props, and one 96-slot bitmap font atlas. Mint is the baby form; Lilac is the grown
form; BUBBLE is a separate species that grows from the baby axolotl into the axolotl. The SDL and ESP32 pet builds embed these assets using
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
| `creatures/` | Mint and Lilac: idle A/B, eating, happy, asleep, unwell, curious, content at 32x32. Axolotl: 18 frames at 48x48 (idle bob, blink, happy, surprised, chew, sleep breath, sad, potty, hug) and the baby axolotl's 4 frames (3 idle, surprised), both in the axolotl palette |
| `icons/` | Basic care, food, play, clean, rest, wake, medicine, gift, reward, inventory, back, confirm; 16x16 pixels |
| `menus/` | Thirteen 32x32 category, moment, and close icons; rendered at 3x in rings |
| `meters/` | Five 32x32 stat pictograms; used at 2x for manually selected stat tiles (heart also represents mood) |
| `health/` | Nine 32x32 clicker icons, including the dental sequence |
| `effects/` | Eight 16x16 star, heart, orb, comet, fairy-wing, music, idea, and rainbow celebration sprites |
| `prizes/` | Nine unique 32x32 keepsakes: butterfly, pearl tooth, breakfast sun, tea sprite, movie star, bubble gem, moon charm, rainbow seed, friendship bow |
| `backgrounds/` | Six 64x64 neutral scenes: home, garden, park, pond, beach and library; black vignettes |
| `props/` | Food bowl, closed/open gift, and bed; 24x24 pixels |
| `font/` | 128x72 atlas: 16 columns by six rows of 8x12 cells, ASCII 32–127 |
| `source/` | Original generated atlas, exact generation prompt, original pixel authoring recipe, polish recipe, and glyph patterns |

`assets.json` assigns explicit IDs, paths, frame sizes, bounds, pivots, palette,
and clip timings. PNGs in the asset directories are the editable runtime
art sources. The large generated atlas is retained for provenance and reproduction;
it is not part of the runtime payload. Keep IDs stable when revising art.

Sprites use 16 shared opaque colors plus transparent pixels, unless they name a palette in `palettes` (the axolotl uses its own 11 colours); backgrounds use a separate 16-gray palette. Alpha is
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

Actual raw payload (RGB565, masks, font) is 267,304 bytes. The RFC's 8,192-byte definition
and 4,096-byte metadata allowances bring the planned pack to 279,592 bytes. The SDL
live pack is 269,960 bytes, within its 288 KiB staging buffer. Its 125,248 pixels
and 15,656 mask bytes fit the existing 131,072-pixel and 16,384-byte banks, leaving
5,824 pixels and 728 mask bytes: room for two more 48x48 frames (2,304 pixels and
288 mask bytes each), with both banks running out together. These are desktop
authoring limits, not a linked firmware memory measurement.

To reproduce candidate PNGs without overwriting reviewed source art:

```sh
./scripts/uv run --python 3.12 assets/slice/source/authoring.py
```

Candidates go to `build/assets/reimport/`. Review and explicitly copy desired
changes into the source directories, then rebuild. The authoring recipe samples
rounded quarter-grid boundaries from the actual source size, resizes using nearest
neighbor, thresholds alpha at 128, and selects the nearest shared palette color.
Do not assume the image generator honored the requested dimensions. It returned
1254x1254 for this atlas; see the validation record in memo-007.

## Polish pass and before/after review

The original recipes above are now followed by a second stage,
`source/polish.py`. It redraws the creatures, props, butterfly, friendship bow,
rainbow seed, and rainbow effect from a shared kit (`pixel_kit.py`,
`creature_art.py`, `prop_art.py`, `keepsake_art.py`). It also re-inks every
other icon, menu, meter, health, effect, and prize sprite (`icon_polish.py`)
with one closed 1px outline and no stray specks. The original generator recipe no
longer reproduces the shipped creature and prop PNGs; `polish.py` does.

```sh
./scripts/uv run --python 3.12 assets/slice/source/polish.py          # candidates in build/assets/polish
./scripts/uv run --python 3.12 tools/assets/compare_slice.py --before assets/slice --after build/assets/polish
./scripts/uv run --python 3.12 assets/slice/source/polish.py --apply  # write owned PNGs + manifest bounds
```

Assets saved from Jelli Art (`make jelli-art`) are recorded in
`source/hand-painted.json`; `polish.py` skips them.

`compare_slice.py` defaults to `--before HEAD` and the working tree. It writes
`build/assets/compare.html`, a self-contained pixel review page with these tools:
side-by-side, swipe, flip, onion, and diff modes; zoom up to 32× with a pixel grid;
speck and open-edge overlays; per-colour isolation; a hover inspector shared by
both panes; device-scale panel previews with a physical-size mode; a sheet of every
asset that flips to "before" while Space is held; and per-asset review marks that
export as Markdown. The style rules are in the
[pixel art authoring guide](../../docs/pixel-art-guide.md).

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
into the style byte. The art budget is now 160 KiB; actual raw pixels plus existing
allowances use 159,776 bytes. Physical readability and touch feel still need review.

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


The barbell (`menus/barbell.png`, 6014) and water glass (`menus/water.png`, 6015)
were generated with the built-in image tool, then exported at 32×32 with nearest
sampling, binary alpha, and nearest colors from the existing palette. The barbell
prompt requested a horizontal teal shaft, chunky lilac plates, plum outlines,
cream highlights, and transparent margins on a 32-pixel logical grid. The water
prompt requested a teal glass and droplet, no steam/text/face, plum outlines,
cream highlights, and the same transparent pixel grid. Full generation prompts
are in the implementation session; neither image uses a fallback API tool.

The two new 32×32 icons add 4352 bytes of RGB565 and masks. Raw pixels plus
the retained 8192-byte definitions and 4096-byte indexes total 164128 bytes.
The authored pack allowance is now 161 KiB (164864 bytes), leaving 736 bytes.
This does not change framebuffer, core heap, or C source-size limits.


Additional location scenes use the existing 16-gray pixel recipe. Reproduce only
Park/Pond/Beach/Library (preserving other art) with:

```sh
./scripts/uv run --python 3.12 assets/slice/source/location_art.py
```

Their IDs are 10003–10006. `content/locations.json` binds stable saved location
IDs to these background assets and specifies indoor/outdoor atmosphere. Add
locations at the end of the catalog (up to eight), register a 64×64 background
in the manifest, then choose allowed locations in Jelli Art's Activities view.
Build checks reject unknown or non-background asset references. PNG hot reload
updates artwork; catalog or activity-rule changes require a rebuild.

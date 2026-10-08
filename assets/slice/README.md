# Jelligotchi slice artwork

The first review set contains 29 PNG files: 12 creature frames, 12 icons, four
props, and one 96-slot bitmap font atlas. Mint is the baby form; Lilac is the grown
form. These assets are not yet wired into the SDL or ESP32 renderer.

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

## Source inventory

| Directory | Contents |
| --- | --- |
| `creatures/` | Two forms, each with idle A/B, eating, happy, asleep, and unwell; 32x32 pixels |
| `icons/` | Basic care, food, play, clean, rest, wake, medicine, gift, reward, inventory, back, confirm; 16x16 pixels |
| `props/` | Food bowl, closed/open gift, and bed; 24x24 pixels |
| `font/` | 128x72 atlas: 16 columns by six rows of 8x12 cells, ASCII 32–127 |
| `source/` | Original generated atlas, exact generation prompt, original pixel authoring recipe, and glyph patterns |

`assets.json` assigns explicit IDs, paths, frame sizes, bounds, pivots, palette,
and clip timings. PNGs in the four asset directories are the editable runtime
art sources. The large generated atlas is retained for provenance and reproduction;
it is not part of the runtime payload. Keep IDs stable when revising art.

All source sprites use 16 shared opaque colors plus transparent pixels. Alpha is
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

Actual raw pixel payload is 38,688 bytes. The RFC's 8,192-byte definition and
4,096-byte metadata allowances bring the planned pack to 50,976 bytes; those
allowances are not a completed game pack or a linked firmware measurement.

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

# Artwork and live authoring

Run commands from the repository root unless stated otherwise.

## Paint in Jelli Art

```sh
make jelli-art    # opens http://127.0.0.1:8765/ in your browser
make run-live      # optional, in a second terminal: see saves in the game
```

The studio is the review page with painting turned on. It needs no setup beyond
the repository's pinned tools, and it only listens on your own computer.

1. Pick a sprite in the left list, or use **Sheet** to browse them all.
2. Choose **Paint** under Mode. Use the pencil, eraser, fill, and colour picker.
   Right-click erases, Alt-click picks a colour, and **Mirror** paints both halves
   of a symmetric sprite.
3. Choose a colour under **Paint colours**. **custom** paints any colour. The
   ✎ on a palette colour changes that colour in every sprite at once; it asks
   before doing so.
4. **Tidy outline** applies the house outline and removes stray pixels. Turn on
   **Issues** to see what still needs attention. Undo with ⌘Z / Ctrl+Z.
5. **Save** (⌘S / Ctrl+S) writes the PNG. With `make run-live` running, the game
   shows it straight away. "Compare with" picks what the before image is: the
   last commit, a release tag, or a recent commit.

Saved sprites are listed in `assets/slice/source/hand-painted.json`. The polish
recipe never overwrites them. Custom colours work in the live game, but release
builds accept only the 16 palette colours. To ship a new colour, put it in a
palette slot with ✎. The font atlas and backgrounds are not paintable yet.
Follow the [pixel art authoring guide](pixel-art-guide.md) for the house style.

Sprites with their own palette, such as the 48×48 axolotl, paint from that
palette. The shared ✎ and **Tidy outline** do not apply to them.

### Animate creatures

Choose **Creature** at the top, then a form and one of its eight poses. Add
frames from that form's thumbnails, reorder or remove them, set each frame's
milliseconds, and choose **Loop** or play-once. A play-once clip holds its last
frame. **Replay one-shots** repeats it in the preview only. The preview uses the
game's scale and ground position; **All poses** plays every pose together. Space
plays or pauses; `,` and `.` step frames. **Save clips** (⌘S / Ctrl+S) checks
the whole manifest and writes only the edited clips to `assets.json`.

## Edit in the running game

```sh
make run-live
```

This builds the desktop game once, then watches `assets/slice/assets.json` and
its authored PNGs. Save an image to refresh the running game without resetting
pet state. The watcher checks every half second and publishes a complete pack
with an atomic replacement. The SDL host validates it and swaps artwork between
frames; screenshot captures defer the swap until capture ends.

The live pipeline recalculates opaque bounds, centroids, and the bottom-three-row
creature ground anchor. Keep the original dimensions, IDs, and binary alpha.
Live paintovers may use new RGB colors; they are converted to RGB565. Invalid,
empty, partially written, or missing images retain the previous valid artwork.
The game continues while the watcher reports the error.

```sh
# Explicit binary/source and normal game arguments:
./scripts/jelli-art-live --binary build/desktop/jelligotchi --assets assets/slice -- --save build/art-pet
# Use an already generated pack without a watcher:
./build/desktop/jelligotchi --asset-pack build/assets/live.jlap
```

The exact default pack filename is reported by the watcher. Hot reload is opt-in
and desktop-only. Ordinary launches and ESP32 use embedded immutable assets;
flashing or distributing changed embedded art still requires a build. The host
owns the override memory; the portable renderer performs no file IO or allocation.

## From a release ZIP

Extract the whole native game ZIP. It includes `assets/slice`, `preview.html`,
and the live-art tool with its support files.

```sh
# macOS/Linux: uses the bundled pinned uv bootstrap wrapper
./scripts/jelli-art-live
# Windows: install uv, then from the extracted directory
uv run --python 3.12 tools/assets/live_assets.py
```

The launcher discovers the bundled executable. First tool use downloads Python
and Pillow; normal gameplay does not require them. The packaged PNGs are editable
source art. Embedded fallback images remain available if no override is loaded.

## Preview and release validation

```sh
./scripts/uv run --python 3.12 tools/assets/build_slice.py
open build/assets/preview.html    # macOS; otherwise open in a browser
```

The builder validates all 79 PNGs: dimensions, IDs, bounds, palette, binary alpha,
clips, and the nine prizes' distinct pixels. It emits the self-contained HTML
sheet, contact sheet, and RGB565/mask exports into ignored `build/assets/`.
Release builds retain the stricter shared-palette and manifest-bounds checks;
update the authored manifest and palette intentionally when accepting a paintover.

The inventory includes sixteen creature frames, care and ring icons, health
activities, stat pictograms, celebration sprites, backgrounds, props, font, and
nine unique presents. See [the source inventory](../assets/slice/README.md) for
stable IDs, provenance, reproduction, and byte budgets.

## Style rules and before/after review

New or revised art follows the [pixel art authoring guide](pixel-art-guide.md):
palette ramps, a closed 1px outline, top-left light, the shared face kit, and the
review checklist. To compare art pixel by pixel against a git revision or another
asset directory:

```sh
./scripts/uv run --python 3.12 tools/assets/compare_slice.py              # HEAD vs working tree
./scripts/uv run --python 3.12 tools/assets/compare_slice.py --before v0.2.0
open build/assets/compare.html
```

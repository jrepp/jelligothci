# Artwork and live authoring

Run commands from the repository root unless stated otherwise.

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

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
creature ground anchor. Keep the original dimensions and IDs. Legacy pixel art
keeps binary alpha; high-resolution entries (`"density"` in the manifest) keep
their stored size (logical size × density) and may use any 8-bit alpha.
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

The builder validates every PNG: dimensions, IDs, bounds, clips, and the nine
prizes' distinct pixels; palette and binary alpha for legacy pixel art; density,
8-bit RGBA format and drawable scales for high-resolution art. It emits the
self-contained HTML sheet, contact sheet, and RGB565, mask and alpha exports into
ignored `build/assets/`.
Release builds retain the stricter shared-palette and manifest-bounds checks;
update the authored manifest and palette intentionally when accepting a paintover.

The inventory includes thirty-two creature frames (Mint, Lilac, the ninja, and the
unicorn), care and ring icons, health activities, stat pictograms, celebration sprites,
battery-status icons, backgrounds, props, font, and nine unique presents. All but the
backgrounds, props and font are high-resolution art. See [the source inventory](../assets/slice/README.md) for
stable IDs, provenance, reproduction, and byte budgets.

## High-resolution art

Each sprite entry can opt into high resolution with `"density": D`: its PNG,
under `assets/slice/hires/<kind>/`, stores D pixels per logical pixel (for
example a 32×32 creature at density 6 is a 192×192 RGBA PNG), with soft 8-bit
alpha and any colours. Layout, anchors and touch keep using logical pixels, so
switching an asset to high resolution never moves it on screen. The renderer
draws 1:1, upscales by whole multiples, or box-filters by whole divisors, then
alpha blends. The density table, the coverage rule (alpha ≥ 128) and the
step-by-step manifest switch are in
[the slice guide](../assets/slice/README.md#high-resolution-art-density).

To add or repaint high-resolution art:

1. Paint at the stored size from the density table, on a transparent
   background. Keep shadows that must not accept taps below alpha 128.
2. Switch the manifest entry (path + `density`) as the slice guide describes, then
   run the builder and open the preview; high-resolution cards show at on-screen size.
3. Use `make run-live` to see it in the game. Live packs (format v2) carry density
   and alpha; the desktop host reserves about 20 MiB when a pack is loaded.

Embedded high-resolution art is flash rodata on the ESP32: a full set is about
5 MB, which needs the 8 MB app partition described in the
[hardware guide](hardware.md#partition-table-and-regenerating-sdkconfig).

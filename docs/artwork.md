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
   **Changed only** narrows the list to sprites that differ from the before
   image; when none do, **Show all** turns it off.
   Press **?** for the keyboard shortcuts.
2. Choose the **Paint** tab at the top. The tools are pencil, eraser, fill,
   colour picker, line, rectangle, ellipse and select. **Filled** fills
   rectangles and ellipses; Shift while dragging snaps lines to 45° and makes
   squares and circles. Right-click erases. Alt-click (or Alt+Enter at the
   keyboard cursor) picks a colour and keeps your tool, except with Select,
   where Alt-drag copies. **Mirror** paints both halves of a symmetric sprite.
3. Selection, transforms, nudges, colour replacement, guides and **Tidy
   outline** are under **More tools**, which stays open once you open it.
   **Select** a rectangle to move it (drag inside it, or Alt+arrow keys), copy,
   cut, paste, delete, flip, rotate, or replace one colour with another. While
   a selection exists, painting stays inside it; Escape clears it. Without a
   selection, flip, rotate and Alt+arrows apply to the whole sprite. **Wrap**
   makes nudges come round the other side, for tiling patterns.
4. Choose a colour under **Paint colours**, beside the canvas. Tools paint only the sprite's
   palette colours until you choose **custom**, which paints any colour. The
   ✎ on a palette colour changes that colour in every sprite at once; it asks
   before doing so.
5. **Tidy outline** applies the house outline and removes stray pixels. Turn on
   **Issues** to see what still needs attention. Undo with ⌘Z / Ctrl+Z, or click
   any step in the **history** list beside the canvas.
6. Zoom with `[` and `]`, **Fit**, or Ctrl/⌘ and the mouse wheel; middle-drag
   pans. **Fit content** (`Z`, under **More tools**) zooms to the drawn pixels
   with a 2 px margin, which helps on frames with empty rows. **Grid** and
   **Guides** (centre and 8 px tile centres) are toggles.
   The backdrop starts as the sprite's real surroundings: the scene's wall
   grey for creatures and props, ring grey for icons, menus and health, the
   stat tile for meters, prizes and the font, and black for effects and
   backgrounds. **Backdrop** (or `B`) changes it for that kind of sprite, and
   the choice is remembered; picking the default again forgets it. A paint
   colour that would barely show on the backdrop gets a dashed ring on its
   swatch and, when zoomed in, under the cursor.
   Without a mouse, Tab to the canvas to show the keyboard cursor: the arrow
   keys move it, Enter or Space applies the tool, Shift with the arrows draws,
   and Escape hides it.
7. **Save** (⌘S / Ctrl+S) writes the PNG. With `make run-live` running, the game
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
plays or pauses; ← and → (or `,` and `.`) step frames. **Save clips** (⌘S /
Ctrl+S) checks the whole manifest and writes only the edited clips to
`assets.json`.

**Paint ✎** on a frame card opens that frame in Paint with the **Flip-book**
beside the canvas. It plays the clip at its real timing, including strokes you
have not saved (**Play**, Shift+Space). Click a thumbnail, or press ← and →,
to paint another frame; the arrows move the keyboard cursor instead while it
shows, and `,` and `.` always change frame. **Onion skin** (O) ghosts the frames
before (amber) and after (blue). **Changes** ghosts only where they differ from
this one; **Silhouette** ghosts their whole shape, to judge arcs and volume
(Shift+O switches). **±1** to **±3** sets how many on each side, and **Ghost**
sets their strength.

The flip-book compares every frame with frame 1 the way the game places it:
each frame stands on its own **ground anchor** (its bottom edge, and the
centre of its bottom three rows), as `core/pet_actor.c` does. It warns when a
frame **slides** sideways by half a source pixel or more on the panel, or when
its **eye** sits higher or lower above its bottom edge than frame 1's. The
frame's thumbnail gets ⚠. **Eye & ground** in the flip-book draws, on the
canvas and the preview, each frame's ground line (mint), its ground anchor
(white tick, with frame 1's in mint when they differ), and the eye row (coral)
where frame 1's eye height puts it. The eye is measured at its white
catchlight, inside a face feature that does not touch the outline; a face with
no catchlight, such as closed or happy eyes, uses the top of its highest
feature.

### Behaviour and size

**Behaviour & size**, below **All poses**, edits the form's entry in
`content/creatures.json`. The actor scale sets the preview size; the panel
shows the result against the 300×176 px actor and 96 px icon limits. Choose the
form's behaviour, or **Copy as new** to change one form without the others.
Order the pose rules; the first rule whose condition holds wins. With no match,
the idle schedule plays one pose per 900 ms beat. The quiet cycle holds curious
and content beats as idle every Nth cycle, offset by pet ID.

Turn on **Simulate on panel** and pick conditions, or **idle**, to see which
rule and clip the game would choose. The idle timeline plays the beat schedule
with its clips. While simulating, `,` and `.` step one beat. **Save behaviour
& size** validates the file and writes `content/creatures.json`.

State poses such as `study` and `ponder` are listed after the 8 base poses. A
form without its own clip for one uses the fallback pose shown. **Add clip**
gives the form its own clip; **Remove clip** goes back to the fallback.

### Behaviour and reactions

Choose **Behaviour** at the top. **States & looks** edits each behaviour state.
It sets how long the state lasts and its cooldown, its need and bond changes,
the stimuli that end it early, and an optional request with its bond reward.
It also sets the state's look: pose, upper-case caption of up to 16 characters,
an effect sprite above the head, and a prop. **Repertoires** chooses which forms
react and which moments they enjoy more. It also edits their reactions: a
stimulus, an optional value, the state, the weight, the chance, and conditions
for place, night, mood and bond.

**Stimulus simulator** shows what a form would do when a stimulus arrives:
every reaction that fits, the weighted pick, the chance roll and the state it
enters. A running state is only ended by its ends-on stimuli, never replaced.
The odds bar covers the next ten minutes, and the preview shows the resulting
look with an optional potty mess. **Save behaviour** (⌘S / Ctrl+S) checks both
files with the engine's own validator before writing them.

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

## Add a creature

Creatures are data ([ADR-012](../docs-cms/adr/adr-012-data-driven-creature-species-and-presentation.md)).
The axolotl was added this way, without species-specific C:

1. **Frames.** Write an import spec like `assets/slice/source/axolotl-import.json`:
   form name, `first_id`, source cell size, a 32x32 or 48x48 canvas, pivot, and a
   palette name. Then run
   `./scripts/uv run --python 3.12 tools/assets/import_creature.py SPEC SOURCE_DIR`.
   Every source cell becomes one pixel, and all frames keep their shared placement.
   IDs are `first_id` plus list position, so append new frames and never reorder.
   Jelli Art's timeline can also add frames; it appends a `"studio": true` entry
   that holds the ID, and the importer leaves that frame alone.
2. **Clips.** Give the form one `<form>.<pose>` clip for each of the eight
   `creature_poses` in `assets/slice/assets.json`. Edit and preview them in
   Jelli Art's Creature view.
3. **Catalog.** In `content/pets.json`, add a form (`art` = the import's form name,
   `portrait` = one of its frames). Add an evolution set with one or two forms,
   and point a collection entry at it.
4. **Behaviour and size.** In `content/creatures.json`, add a profile for the
   form's `art` with actor, icon and portrait scales, and pick or write a
   behaviour: ordered `{when, pose}` rules, idle beats and a quiet cycle.
5. Run `./scripts/uv run --python 3.12 tools/assets/build_slice.py`, then
   `make test`. The generators reject missing poses, foreign frames, unknown
   conditions, and actors too large for the panel.

Saves follow the catalog. Moving an entry to another evolution set converts that
pet on its next load.

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

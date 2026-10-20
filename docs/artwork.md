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
   colour picker, shade, line, rectangle, ellipse, select and magic wand.
   **Filled** fills rectangles and ellipses. Shift while dragging snaps lines
   to the clean ratios (flat, 3:1, 2:1, 1:1, 1:2, 1:3 and upright), with every
   run the same length, and makes squares and circles. The left button paints
   the main colour and the right button the secondary colour, which starts
   transparent, so right-click erases (with Fill, the whole touching area) until
   you choose one. X swaps the two. Alt-click picks a colour and keeps your
   tool (with Select, Alt-drag copies instead). **Mirror** (M) paints both
   halves of a symmetric sprite. **Shade** (T) steps each pixel one colour
   lighter along its [ramp](pixel-art-guide.md#2-palette-and-ramps), following
   the row you picked the colour from; right-click or Shift steps
   deeper. The **?** overlay lists the remaining keys.
3. Selection, transforms, nudges, colour replacement, mirror axes, guides,
   the tile preview and **Tidy outline** are in the **Properties** inspector, which stays
   open once you open it. **Select** a rectangle, or click a colour with the
   **Magic wand** (Q) to select the pixels of that colour touching it
   (Shift-click, or Shift+Enter at the keyboard cursor while the wand is the
   tool, selects it everywhere). Move a selection by dragging inside it with
   Select, or with Alt+arrow keys; copy, cut, paste, delete, flip, rotate, or
   replace one colour with another. While a selection exists, painting, fill
   and shade stay inside it; Escape clears it, and undo puts back the
   selection each step had. Without a selection, flip, rotate and Alt+arrows
   apply to the whole sprite. **Wrap** makes nudges come round the other side,
   for tiling patterns (a magic-wand selection moves instead), and **Tile
   3×3** (Shift+T) shows the sprite repeated beside the canvas so the seams
   show. **Top/bottom** (Shift+M) mirrors vertically too. The arrows beside it
   move the axes half a pixel at a time, so an axis can sit between two
   columns or on one; **Centre** puts them back. Moved axes are remembered per
   sprite (so each clip frame separately) in this browser.
4. Choose a colour under **Paint colours**, in the column beside the canvas, one row per
   ramp. Click a colour for the main colour. For the secondary, right-click it,
   press Shift+F10 on it, or choose it and press X. Tools paint only the
   sprite's palette colours until you choose **custom**, which paints any
   colour. The ✎ on a palette colour
   changes that colour in every sprite at once, ramps included; it asks before
   doing so.
5. **Tidy outline** shows the house outline and stray-pixel fixes side by side,
   with the changed pixels marked. Nothing changes until you choose **Apply**.
   The header says **lint pass** or which rules the sprite fails: specks, open
   edges, or more colours than the
   [style guide](pixel-art-guide.md#2-palette-and-ramps) allows. Turn on
   **Issues** to see where. If a failure is intentional, **Waive…** records the
   rule and your reason, and the sprite stops counting as failing. Flipping a
   shaded sprite horizontally moves its light to the top right, and the studio
   reminds you once per session. Undo
   with ⌘Z / Ctrl+Z, or click any step in the **history** list beside the
   canvas.
6. Zoom with `[` and `]`, **Fit**, or Ctrl/⌘ and the mouse wheel; middle-drag
   pans. **Fit content** (`Z`, in the **Properties** inspector) zooms to the drawn pixels
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
   On an iPad the Pencil paints, and so does one finger unless **Pencil only**
   (on the quick bar) is on; then one finger pans and a tap shows that pixel.
   Two fingers pan and pinch to zoom, a two-finger tap undoes and a
   three-finger tap redoes. Hold a finger still on the canvas to pick a colour
   and keep the tool. The **quick bar** stands in for right-click and the
   modifier keys: **Pick** (**Copy** with Select), **2nd** (the secondary
   colour), **Constrain** (Snap, Square, Circle, Deeper or Everywhere,
   whichever the tool uses), **Peek**, **Swap**, **Undo**, **Redo**, and nudge
   and **Deselect** buttons while a selection exists. Hold a button while you
   draw, or tap it to keep it on. Long-press a palette colour for a menu to use
   it as the secondary colour, isolate it, or edit its slot. The bar shows
   with touch only; **Quick bar** and **Left-handed** in the **Properties** inspector show
   it with a mouse or move it to the right.
Use the compact tool rail to choose a tool; the options beside it change with
the tool. **Hand** (`H`) pans the canvas without painting. Selection commands
appear when needed, and **Properties** holds advanced transforms and view
options. On narrow screens, **List** opens the asset browser as a drawer.

7. **Save** (⌘S / Ctrl+S) writes the PNG. With `make run-live` running, the game
   shows it straight away. Each save is recorded with **Your name**, which you
   set under **Settings** in the header; if it is empty, the studio asks once
   before the first save. In Paint, "Compare with" is in the **before** pane
   beside the canvas, and picks what the before image is: your last save, the
   art in the game now (`main`), a game release, or a recent commit that changed
   art. Review shows it in the header's second row, with the before and after
   refs and the lint totals; those are Review-only, so every other mode has a
   single header row that does not change height when you start editing.

Save commands keep their name and disk icon. The adjacent status distinguishes
Unsaved, Saving, Saved and Save failed. Continuing to edit during a save keeps
the newer changes unsaved; a successful save acknowledges only the submitted
snapshot. Undo and Redo remain separate history commands.

Saved sprites are listed in `assets/slice/source/hand-painted.json`. The polish
recipe never overwrites them. Custom colours work in the live game, but release
builds accept only the 16 palette colours. To ship a new colour, put it in a
palette slot with ✎. Backgrounds are paintable; the font atlas still uses an external image editor.
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

On an iPad, drag a duration handle (a 44 px target) with a finger or the Pencil
to change a frame's milliseconds in steps of 10. On a touch screen, turn on **Fine (1 ms)**, next
to **Durations**, for single milliseconds; it also sets the arrow-key step on a
handle, and Shift while dragging still does the same. Drag along the track to
scrub through the frames, or tap a segment or thumbnail to show that frame. A
frame card's **◀ Move** and **Move ▶** buttons reorder frames without
dragging. The ⓘ buttons explain the controls that otherwise have only a
tooltip. The Creature view stacks into one column when its own width is under
900 px. ([RFC-007](../docs-cms/rfcs/rfc-007-jelli-art-touch-and-pen.md))

**Paint ✎** on a frame card opens that frame in Paint with the **Flip-book**
docked under the canvas. It plays the clip at its real timing, including strokes you
have not saved (**Play**, Shift+Space). Click a thumbnail, or press ← and →,
to paint another frame; the arrows move the keyboard cursor instead while it
shows, and `,` and `.` always change frame. **Onion skin** (O) ghosts the frames
before (amber) and after (blue). **Changes** ghosts only where they differ from
this one; **Silhouette** ghosts their whole shape, to judge arcs and volume
(Shift+O switches). **±1** to **±3** sets how many on each side, and **Ghost**
sets their strength.
The flip-book's Play, Frame, Onion skin (in the **Onion & guides** panel), Changes/Silhouette and range buttons
are the touch equivalents of Shift+Space, ←/→, O and Shift+O, sized for fingers.

The flip-book compares every frame with frame 1 the way the game places it,
on its own ground anchor. It marks a frame ⚠ when it **slides** sideways by
half a source pixel or more on the panel, or when its **eye** (measured at the
white catchlight) sits higher or lower than frame 1's. **Eye & ground** draws
those lines on the canvas and the preview. Onion ghosts still line frames up
by their pivot.

### Behaviour and size

**Behaviour & size**, below **All poses**, edits the form's entry in
`content/creatures.json`. The actor scale sets the preview size; the panel shows
the result against the limits enforced by
[`tools/assets/creature_data.py`](../tools/assets/creature_data.py). Choose the
form's behaviour, or **Copy as new** to change one form without the others.
Order the pose rules; the first rule whose condition holds wins. A rule may pick
a state pose, such as the axolotl's `hug` on `touch_happy`; a form without that
clip plays the state pose's fallback. With no match, the idle schedule plays one
pose per beat. The quiet cycle holds curious and content beats as idle every Nth
cycle, offset by pet ID.

Turn on **Simulate on panel** and pick conditions, or **idle**, to see which
rule and clip the game would choose. The idle timeline plays the beat schedule
with its clips. While simulating, `,` and `.` step one beat. **Save behaviour
& size** validates the file and writes `content/creatures.json`. **Revert** asks
first when there are unsaved edits, and discards only the behaviour and size
edits: the looks you changed in the Behaviour view stay.

State poses such as `study` and `ponder` are listed after the 8 base poses. A
form without its own clip for one uses the fallback pose shown. **Add clip**
gives the form its own clip; **Remove clip** goes back to the fallback.

### Behaviour and reactions

Choose **Behaviour** at the top. Start with **Behaviours**, which explains what the pet does and how it looks.
It sets how long the state lasts and its cooldown, its need and bond changes,
the stimuli that end it early, and an optional request with its bond reward.
It also sets the state's look: pose, upper-case caption of up to 16 characters,
an effect sprite above the head, and a prop. **Reaction rules** chooses which forms
react and which moments they enjoy more. It also edits their reactions: a
stimulus, an optional value, the state, the weight, the chance, and conditions
for place, night, mood and bond.

**Test a reaction** shows what a form would do when a stimulus arrives:
every reaction that fits, the weighted pick, the chance roll and the state it
enters. A running state is only ended by its ends-on stimuli, never replaced.
The odds bar covers the next ten minutes, and the preview shows the resulting
look with an optional potty mess. **Save behaviour** (⌘S / Ctrl+S) checks both
files with the engine's own validator before writing them. **Revert** asks first
when there are unsaved edits, and discards only the behaviour edits (states,
repertoires, rules and looks): the Creature tab's behaviour and size edits stay.

On an iPad, the design views show a word on each icon-only button (for example
**↑ Earlier** and **✕ Remove**), a tap-to-reveal ⓘ beside the groups that
explain themselves with a tooltip on a desktop, and − and + buttons on number
fields. **Test in game** gains **◀ Frame** and **Frame ▶** for the `,` and `.`
keys. In landscape the list sits beside the inspector with the preview above it;
in portrait the view stacks and the preview stays pinned at the top.

Use **Duplicate behaviour** to copy the selected behavior and its appearance
into a new named state. The copy does not change existing triggers. Connect it
under **Reaction rules** when it should run; the **When → Do** controls describe
that connection. Timing and appearance are shown first, while detailed effects,
requests, ending conditions and global rules are in advanced disclosures.

### Unsaved work and Test in game

Every editor (Paint, Creature clips, Behaviour & size, Behaviour and Activities)
shows its edits in the **Unsaved** button (a fixed-width pill beside the git
status, so the page never shifts when it appears) and on its mode tab, and the
page asks before you close it. The studio also keeps those edits in this browser. After a
reload, a crash or a browser that dropped the tab, it restores them and says so;
save to keep them, or use **Revert** (**Reload saved** in Activities) to discard
them. A restored edit whose file changed on disk since can still be saved: Save
asks whether to overwrite the newer file (**Overwrite** or **Cancel**), the same
as for clips. **Revert** and **Reload saved** ask first when there are edits, and
each discards only its own editor's edits.

Behaviour & size (Creature tab) and Behaviour share `content/creatures.json`, but
each view's status line and Save cover only its own part: Save behaviour & size
writes the profiles and behaviours, and Save behaviour writes `behaviors.json` and
the state looks. Neither commits the other view's unsaved edits.

With the studio open in two tabs, a tab does not offer to restore, or discard, a
draft that another open tab is still editing. A draft is handed over when that tab
closes or reloads, or after about five seconds without a sign of life.

**Test in game** renders the files on disk. While any editor has unsaved edits it
says so beside **Render in engine**, with the editors and the number of edits
that are not included; save, then render again.

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

The builder validates the PNG inventory: dimensions, IDs, bounds, palette,
binary alpha, clips, and distinct prize art. It emits the self-contained HTML
sheet, contact sheet, and RGB565/mask exports into ignored `build/assets/`.
Release builds retain the stricter shared-palette and manifest-bounds checks;
update the authored manifest and palette intentionally when accepting a paintover.

The inventory includes creature frames, care and ring icons, health activities,
stat pictograms, celebration sprites, backgrounds, props, font, and presents.
See [the source inventory](../assets/slice/README.md) for stable IDs, provenance,
reproduction, and current asset details.

## Create content in the studio

### Compose background scenes

Choose **Scenes** to assemble a background from shared artwork. Select a layer,
set its position, tilt amplitude and travel percentage, then use **Move back** or
**Move forward** to change its order. **Art library** adds another instance of an
existing background; **Paint artwork** opens its shared pixels in Paint. Editing
that artwork affects every scene that references it. **Duplicate scene** copies
the composition while retaining the shared art references.

Layer thumbnails help identify shared pieces. **Duplicate layer** copies the selected
layer's settings and seed; change its layer seed if you want independent variation.
Reorder and remove controls are at the top of the selected-layer inspector. **Undo**
and **Redo** recover document edits, including layer deletion, within the current tab.
Reloading saved content resets this history. Use Cmd/Ctrl+Z and Cmd/Ctrl+Shift+Z
outside text fields; text fields retain their normal text-editing undo.

**Preview selected layer only** temporarily isolates a layer without changing its
saved visibility. **Show all layers** restores the composition. The preview stays
beside desktop controls when there is enough screen height; phone layouts keep it
in the normal page flow. The pet reference uses the home actor's ground position.
Scene settings are separate from the selected-layer inspector, and **Layer variation**
expands only when needed.

Give each layer a **Layer name** such as “Distant buildings” or “Curbside props”.
The layer list displays these names independently of the shared artwork file.
**Layer variation** offers horizontal/vertical jitter, travel variation in percentage
points, and an appearance chance. These choose one initial variation for the whole
layer; repeated tiles share that choice. Keep the sidewalk at zero vertical jitter
and 100% appearance so it stays under the pet.

Set **Scene seed** or use **Try another variation** to explore another arrangement.
The readout shows the selected layer's resolved position, speed and visibility.
Save the seed along with the composition to reproduce it in **Test in game** and
native builds. Each **Layer seed** stays attached through renames and reordering;
use different layer seeds for independent variation or the same seed for coordinated
choices. New layers receive an unused seed. Variation is resolved at build time,
so frames remain stable while moving; this does not schedule passing traffic.

Use the two tilt sliders and **Play travel** to preview depth and sideways
walking. Negative travel speed reverses direction. **Reset motion** returns to
neutral. Traveling layers repeat horizontally; disabling repeat also clears their
travel speed. The pet checkbox supplies a color and placement reference.

**Save** writes [scene definitions](../content/scenes.json). Scene edits participate
in unsaved warnings, browser draft recovery and stale-save confirmation.
Use **Test saved scene in game** to open the saved composition in the C renderer.
The game preview offers a saved-scene selector, tilt percentages and signed travel
speed. Render several frames to see travel; the first frame starts at zero distance.
With unsaved edits, the button becomes **Save and test in game** and saves before
opening the preview. Edits made while that save is pending remain in Scenes for a
subsequent save. Valid numeric edits take effect while typing; invalid fields block
saving and receive focus. Cmd/Ctrl+S also works while a form field has focus. Flat location backgrounds remain the
default when no scene is selected. The [engine proposal](../docs-cms/rfcs/rfc-011-layered-backgrounds-and-motion.md)
tracks later activity integration, device orientation and forward travel.

In **New**, choose **Background artwork**, then **Reusable scene layer only**
to create paintable art without a gameplay location. Backgrounds default to the
muted earth kit palette. The kit's
[native source recipe](../assets/slice/source/background_kit.py) generates
candidates into `build/assets/background-kit`; it does not overwrite painted art.
The [manifest](../assets/slice/assets.json) owns the current library and palette ramps.

### Create assets and locations

Use **New** to create a pet from an explicit artwork/gameplay template, an icon,
prop, present, or background and location. Each flow validates the full catalog
before saving and opens the result in Paint. Registered presents become choices
in Activities' **Completion prize** control. Background locations become choices
in Activities and **Test in game**. Choose the daily reward policy and any custom
pose explicitly, then save before running the real-engine preview.

New pets currently use an explicitly chosen single-form policy. Use Creature to
add frames, edit timing, and adjust the copied clips. The repository workflow
below also supports custom import specifications and multi-stage evolution.
Bounds and required inputs are enforced by the
[creator](../tools/jelli-art/creation.py) and catalog generators.

**Duplicate creature** in Creature opens New with the selected form's saved
artwork, clips, scales and gameplay template prefilled. Review its new identity
and unlock policy before creating a separate single-form pet. Save source edits
first. **Duplicate frame** in Paint's flip-book copies the saved image to a new
asset and inserts it after the current frame when the clip has room; a full clip
keeps the copy in the library. Creature's timeline also provides frame duplication.

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


## Author celebration effects

Open **Effects** in Jelli Art to coordinate sprite overlays and creature pose
clips on one timeline. Select a track or cue to edit its name, start and duration.
Sprite tracks hold one or more registered frames, with an optional frame loop,
integer scale and linear or eased motion between start and end offsets. Creature
cues restart the selected pose clip and cannot overlap. Use **New → Effect sprite**
and Paint to create additional frames, or **Paint frame** to edit shared artwork.

Default render layers are **Behind pet**, **In front of pet**, and **Foreground**.
Tracks draw in list order within each layer; Move back/forward controls that order.
Foreground draws over routine content, below navigation controls. Sprite tracks
can overlap in time. Ordinary behavior returns between creature cues and when
the celebration ends.

Choose a preview pet, then a named hardpoint. Edit its coordinates or enable
**Place selected hardpoint** and tap the preview. Hardpoint coordinates are source
pixels relative to the pet's ground contact; negative Y goes upward. They are
shared across celebrations for that artwork form. New pets inherit and scale
their template's hardpoints. A track selects a hardpoint and adds display-pixel
motion offsets; **Screen origin** uses absolute display coordinates instead.

Play, Replay, the timeline scrubber and time stepping preview the choreography.
Saving writes [effects.json](../content/effects.json) with stale-file protection;
unsaved edits survive refresh as browser drafts. **Save and test in game** opens
the actual C renderer using saved content. Save any separate Paint or Creature
clip edits too. Effects can also start explicitly with `jelligotchi --effect celebrate`.

Under **Automatic celebrations**, select an effect for **Pet evolves** or
**Present caught**, or leave it **Disabled**. Bindings are global and save with
this document; rebuild the game/firmware to apply them. Present catches wait until
you return home from the collection. Only live events celebrate; offline catch-up
does not. A pending celebration waits for an active effect, expires after 30
animation-clock seconds, and is discarded if you switch pet/form. New eligible
events replace the pending one. The shipped bindings are disabled. The saved-effect
preview tests the choreography directly, not the gameplay trigger path.

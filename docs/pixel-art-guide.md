# Pixel art authoring guide

Rules for Jelligotchi sprites, so new art matches the existing art and stays readable
on the 466×466 round AMOLED panel (about 266 ppi: 466 px across 1.75 in). The house
style is **clean, friendly pop**: chunky silhouettes, a closed dark outline, flat
colour with one shadow tone, and a few bright highlights. These rules apply to
humans, scripts, and image generators alike.

The polish recipe (`assets/slice/source/polish.py`) and the review metrics in
`tools/assets/compare_slice.py` enforce most of this. When a rule here and the
tooling disagree, fix whichever is wrong in the same change.

## 1. Know the real size

Every pixel you draw becomes a square block on the panel. Judge art at its draw
scale, not at 1×, and not only magnified in an editor.

| Kind | Canvas | Draw scale | On panel | Sits on |
| --- | --- | --- | --- | --- |
| creatures | 32×32 | 6× | 192 px | Home or garden scene |
| icons | 16×16 | 6× in rings, 2× on buttons | 96 / 32 px | Ring button `#4a494a` |
| menus | 32×32 | 3× | 96 px | Ring button `#4a494a` |
| health | 32×32 | 3× | 96 px | Ring button |
| meters | 32×32 | 2× | 64 px | Stat tile `#181c18` |
| prizes | 32×32 | 2× | 64 px | Gallery card |
| effects | 16×16 | 1–2× | 16–32 px | Anything; particles fly over all scenes |
| props | 24×24 | varies | — | Scene |
| font | 8×12 cells | 1–4× | — | Dark panels |

The compare page's **On the panel** section draws each asset at its scale. Its
**Physical size** mode approximates the real board once you calibrate your
screen.

## 2. Palette and ramps

Sprites use only the 16 shared colours in `assets/slice/assets.json`, with
binary alpha (fully transparent or fully opaque). Do not anti-alias, use
gradients, dither, or add semi-transparent edges. Backgrounds keep their
separate 16-grey palette.

Colours have roles. Each material is a short ramp: **light**, **base**, **shadow**,
and **deep**.

| Material | Light | Base | Shadow | Deep |
| --- | --- | --- | --- | --- |
| Mint jelly | foam `#cff5cf` | mint `#85e4b6` | sea `#43bca2` | teal `#187b79` |
| Lilac jelly | white `#ffffff` | haze `#d2b8f3` | lilac `#a47bdb` | violet `#7655a3` |
| Purple object | haze | lilac | violet | violet |
| Coral / pink | cream `#fff4cf` | coral `#fa8c99` | rose `#d84f70` | rose |
| Gold | cream | gold `#f5c764` | tan `#ae7855` | tan |
| Sea / teal | mint | sea | teal | teal |

- **Ink `#291b35`** is reserved for outlines and facial features. It is never a
  fill.
- **White** is for catchlights and glints. **Cream** is a material colour (paper,
  tooth, pillow) and the gloss under a white glint.
- Plum and mauve are for recessed or dark interiors (the "more" pill, gear hole).
- Keep each sprite to **six colours or fewer, including ink**. Fewer reads better.
  Creatures may use **eight**, because the face kit adds a white catchlight, a
  cream gloss and coral cheeks to the four-tone ramp and ink. The limits are
  data in `assets/slice/source/lint.json` (`max_colours`,
  `max_colours_by_kind`), and the compare page and Jelli Art flag sprites over
  them.

## 3. Outline

- Every sprite (except the font and backgrounds) has a **closed 1px ink outline**
  outside its fill.
- Make the outline **8-connected**: on curves and slopes, outline pixels step
  diagonally, corner to corner. Do not leave a doubled corner, the extra pixel
  that turns a diagonal step into an L. The **Pixel-perfect** pencil draws
  this way.
- The fill must still be sealed. Every fill pixel needs ink, or more fill,
  directly to its left, right, top and bottom before transparency. A diagonal
  gap in the outline is fine; a gap in a row or column leaks.
- Never use a 2px outline. Thicken the shape instead.
- Separate parts in contact (berries on a bowl, a bow on a box) with ink only
  where the separation must read. Otherwise let the colours meet directly.
- Keep a 1px clear margin inside the canvas so the outline fits. If art must touch
  the canvas edge, the edge pixel itself becomes ink.

The compare page reports fill pixels that touch transparency on the left, right,
top or bottom as **open edges**. The target is zero.

## 4. Light and shading

Light comes from the **top left**, the same for every asset.

1. **Deep**: the bottom row of the silhouette, a 1px contact shadow.
2. **Shadow**: the row above it, plus the right-hand edge of the lower half.
3. **Base**: everything else. Most of the sprite is base.
4. **Light rim** (optional, large rounded forms): top-left edge only, in runs of
   at least 2px.
5. **Gloss**: one small cluster near the top left, made of a 2px white glint over
   a 2px cream underline. Use one cluster, not scattered dots.

Small icons (16px) skip shading. 32px icons get the 1px bottom shadow tone only.

**No lone tone pixels.** A shadow or highlight pixel with no same-colour neighbour
reads as dirt at 6×. Extend it to two pixels or remove it.

## 5. Single pixels

A pixel whose colour appears in none of its eight neighbours is a **speck**.
Specks are allowed only for:

- ink facial features and details (mouth corners, pupils);
- white catchlights and glints.

Everything else must belong to a cluster. The compare page's **Issues** overlay
outlines specks in red. Ink and white are exempt.

## 6. Shapes and curves

- Draw curves with **monotonic run lengths**. A dome steps 4-2-2-1-1-1 as it
  descends, never 2-1-3-1. Jagged runs are the most common reason art looks messy.
- Author shapes as **row spans** (half-widths per row) or with `pixel_kit.disc`.
  `ImageDraw.ellipse` and resampled images produce lumpy, asymmetric edges.
- Centre symmetric art on the pivot. For 32px creatures the axis falls between
  columns 15 and 16, so use even widths.
- Make the silhouette readable on its own. Fill the shape black and check that
  you can still tell what it is at 1×.
- Each icon carries one idea. If it needs a caption to read, simplify it.

## 7. Faces

Every face uses one kit (`creature_art.py`), so all creatures, keepsakes, and
moods read as one family.

| Part | 32px creature | Notes |
| --- | --- | --- |
| Open eye | 2×3 ink, white catchlight in the top pixel toward the gaze | Gap of 6px between eyes |
| Happy eye | `^` arc, 4px | Joy, excitement |
| Closed eye | `u` arc, 4px | Content, asleep |
| Tired eye | 4px lid with 2px pupil below | Unwell |
| Cheeks | 2×1 coral, one row below the eyes, outside them | Unwell uses haze or lilac |
| Mouth | `smile` 4px U, `small`, `o` 2×2, `open` with rose tongue, `wavy`, `chomp` | One mouth per pose |

Poses change the body spans and the face parts only. Keep the eye row and the
spacing consistent so frames do not jitter. Idle B is a squash of idle A: 1–2
rows shorter, 1px wider. The bottom row is always the ground contact. The engine
derives the ground anchor from the bottom three rows.

## 8. Generating art with a model or by hand

Image generators and freehand painting tend to produce noise at this size. Give
the generator, or yourself, the constraints up front:

> 32×32 pixel sprite, binary transparency, no anti-aliasing, no dithering,
> no gradients. Closed 1px outline in #291b35. Light from top-left: one shadow
> tone on the bottom edge, one small white glint. Only these colours: [palette].
> Chunky silhouette readable at 1×, kawaii pop style, centred, 1px margin.

Treat generated output as a **sketch**. Downscaled generator art always needs
redrawing at native resolution: remove specks, rebuild the outline, re-run the
ramp. The original slice creatures came from a generated atlas, and every
messiness issue fixed in the 2026-10 polish pass traced back to that downscale.

## 9. Workflow

Artists: use `make jelli-art` (see [Paint in Jelli Art](artwork.md#paint-in-jelli-art)).
Its live speck and outline counters, **Tidy outline** button, and before/after
views cover sections 3–5. Scripted art uses the recipe:

```sh
# 1. Edit the recipe (creatures, props, keepsakes) or paint a PNG (other icons).
# 2. Render candidates without touching the sources:
./scripts/uv run --python 3.12 assets/slice/source/polish.py
# 3. Review before/after against the current sources:
./scripts/uv run --python 3.12 tools/assets/compare_slice.py --before assets/slice --after build/assets/polish
open build/assets/compare.html
# 4. Apply, validate, and check the game:
./scripts/uv run --python 3.12 assets/slice/source/polish.py --apply
./scripts/uv run --python 3.12 tools/assets/build_slice.py
make test
```

To review committed work instead, use `compare_slice.py` with its default
`--before HEAD`, or pass any git revision.

**Ownership.** `polish.py --apply` regenerates the creatures, props, butterfly,
friendship bow, rainbow seed, and rainbow effect from code, except for anything
listed in `assets/slice/source/hand-painted.json`. The studio adds every sprite it
saves to that list. If you paint one of those PNGs in another editor, add its key
to the list yourself, or the recipe will overwrite your edit. Every other icon is
hand- or script-authored, and the re-ink pass only normalises its outline and
specks. That pass is idempotent: running it twice changes nothing. Assets in
`icon_polish.SKIP` are left exactly as authored.

## 10. Review checklist

Use the compare page (`J`/`K` to step, hold `Space` to peek at before, `I` for
issues) and confirm:

- [ ] Specks: zero, other than ink features and white glints.
- [ ] Open edges: zero.
- [ ] Within the colour limit (six, or eight for creatures), all on-palette.
      Off-palette chips show a red ring, and the header shows `colours n / max m`.
- [ ] The header shows **lint pass**. If a failure is intentional, use
      **Waive…** in Jelli Art and give a reason. Waivers live in
      `assets/slice/source/lint.json`, so reviewers see them in the diff.
- [ ] Reads at device scale on its real backdrop (ring grey, scene, black).
- [ ] Matches its family's face kit, light direction, and outline weight. Use
      sheet view and flip all cards with `Space`.
- [ ] Animation frames keep the eye row and ground row aligned.
- [ ] Marked **Looks good** or **Needs work**. **Copy review notes** exports
      Markdown for a PR or agent.

Hardware is the final check. The desktop and preview approximate the panel's
colour and size, but physical readability needs the board.

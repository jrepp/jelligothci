---
title: Jelli Art Studio review and hit list
author: Jacob Repp
created: 2026-10-10T17:03:49Z
tags: [jelli-art, memo, pixel-art, tooling, ux]
id: memo-043
project_id: jelligotchi
doc_uuid: 1a6c7123-4e5d-420b-a266-6748c9805671
---

# Overview

Two reviews of Jelli Art Studio 0.4.0: one from the point of view of a pixel
artist with long commercial experience, one from a web UX designer who works on
current interactive art tools. This memo merges them into a ranked hit list and
a staged set of pull requests. It is a proposal: nothing here is approved until
the user says so, and the larger items need an RFC first.

# Context

Both reviews ran the studio locally on scratch copies of `assets/slice` and
`content/` (`--assets`, `--content`). They drove the real page with
`agent-browser` at 1600×1000, 1280×800 and 800 px, in the dark, light and
high-contrast themes, and also read the source. Findings marked *browser*
were seen on the page; *code* findings come from reading the source. No
hardware was involved. The live site (`jelli-art.home.jrepp.com`) reported
0.4.0 at the time, the same as the latest `jelli-art-v0.4.0` release.

# What already works

- Painting is locked to the asset's palette. ✎ changes a palette slot in every
  sprite, and paste refuses colours that are not in the palette.
- The rasterisers are correct: lines come out the same from either end, Zingl
  ellipses are symmetric at odd and even sizes, there is a pixel-perfect pencil,
  and the mirror axis sits between columns 15 and 16.
- Art can be checked at device scale, on the panel backdrops, at a calibrated
  physical size, and in **Test in game**, which renders through the real C
  engine.
- The flip-book plays unsaved strokes at real millisecond timing. Onion skin
  shows only the pixels that differ.
- Saving is safe: drafts, stale-file (409) checks, a 100-step history, and
  clear unsaved-edit state in the tab, the header pill and the Save button.
- Accessibility is ahead of most pixel editors: a keyboard cursor for painting,
  live announcements, a high-contrast theme, and reduced-motion and
  forced-colours support.

# Hit list

These are the changes with the most impact for their cost, in order.

1. **First run shows an empty studio.** `changedOnly` defaults to `true`
   (`tools/assets/compare.html:183`). A clean checkout shows "No assets match"
   and every kind chip reads 0 (browser).
2. **The default backdrop hides outlines.** Every sprite starts on black. The
   axolotl's `#000000` outline vanishes and ink `#291b35` barely shows. The
   default ink colour is also invisible on that backdrop, so the first stroke
   seems to do nothing (browser).
3. **Alt-pick changes the tool.** `pick()` switches to the pencil, or to the
   eraser on a transparent pixel (`tools/jelli-art/studio.js:297-303`). Alt
   should borrow the picker for one click and then return to the current tool
   (code).
4. **The canvas is not the focus.** The canvas takes about 26% of a 1600×1000
   viewport, 12% at 1280×800 and starts at y=665 at 800 px. Review-only header
   controls and the asset sidebar stay visible in modes that do not use them
   (browser).
5. **The lint contradicts the style guide.**
   - The guide allows six colours, but `grown-happy` has 8 and is not flagged.
   - The project reports 264 specks and 174 open edges, so the counters are
     always red and get ignored.
   - The guide asks for 4-connected outlines, but the Pixel-perfect pencil
     produces 8-connected strokes, and the shipped art uses diagonal steps
     (browser, code).
6. **Unsaved Activities edits can be lost.** `activities.js` never calls
   `registerDirty`, so neither the header pill nor the leave-page warning sees
   them (code).
7. **Native `confirm()` is used for destructive actions.** `studio.js:732`
   (palette rewrite), `creature.js:302`, `reactions.js:205` and
   `reactions.js:487` bypass the styled `JelliShell.confirm` (code).
8. **No eye-row, ground-row or pivot guides.** The guide's main animation rule
   is to keep the eye row and ground row still. The studio has no guide to
   check them against (code).
9. **The palette has no ramps.** The guide defines light, base, shadow and deep
   per material, but the palette is a flat list, and the axolotl's colours are
   just "axolotl 1…10". A ramp-aware shade tool would enforce the light rules
   better than the lint does (code).
10. **Shortcuts collide.**
    - Space has three meanings, plus a fourth with Shift.
    - O (onion skin) looks like 0 (fit) in the key font.
    - B and G mean backdrop and grid instead of the usual brush and fill.
    - Tool keys appear only in tooltips.
    - The shortcut dialog opens scrolled to the middle (browser, code).

# Proposed pull requests

Each PR is scoped to `tools/jelli-art/` (plus `tools/assets/compare.html`
where the page shell lives) and uses a `fix(jelli-art):` or
`feat(jelli-art):` title, so Release Please bumps only the studio component.
Every PR updates `tools/jelli-art/README.md` and `docs/artwork.md` when
behaviour that artists see changes, and runs `node
tools/jelli-art/test_paint_tools.js`, `test_server.py` and
`browser_smoke.sh`.

## Wave 1: defaults and correctness (small, low risk)

**PR 1. fix(jelli-art): first-run defaults and readable backdrops**
- Default `changedOnly` to off, and turn it off automatically when nothing has
  changed. Kind chips show "creatures 31 · 2 changed".
- Default backdrop per kind: creatures use the scene or panel backdrop, ring
  icons use ring grey `#4a494a`, and meters use the stat tile.
- Give the active colour swatch and the hover pixel a contrast ring when the
  colour is close to the backdrop.
- Add Getting Started text for **Test in game** and **Activities**
  (`shell.js:224`).
- Open the shortcut dialog at the top.
- Dismiss info toasts that belong to a mode when leaving that mode.

**PR 2. fix(jelli-art): Alt-pick, dialogs and unsaved activities**
- Make Alt-click pick temporary: keep the tool and only set the colour.
  Clicking a transparent pixel with the picker tool still selects the eraser.
  Add a test.
- Replace the four native `confirm()` calls with `JelliShell.confirm`, using
  the danger style and a count of what is affected.
- Register Activities with `registerDirty`, and commit field edits on `input`
  as well as `change`.
- Keep a mode parameter in the URL only for modes that use it (no
  `mode=paint` on `#view=activities`).

## Wave 2: style-guide enforcement

**PR 3. feat(jelli-art): lint that matches the style guide**
- Flag colour count against the guide's limit of six including ink. The limit
  comes from asset data, not a JS constant.
- Show pass or fail per sprite, and add waivers stored as data (for example
  `assets/slice/source/lint-waivers.json`) with a reason, so intentional
  specks stop being counted.
- Preview Tidy outline as a diff that the artist accepts or discards before it
  is applied.
- Warn when Flip horizontal is applied to a shaded sprite, since the light
  then comes from the top right.
- Needs a decision on the outline rule (4- or 8-connected). Update
  `docs/pixel-art-guide.md`, the Pixel-perfect help text and the lint together.

**PR 4. feat(jelli-art): animation guides and onion skin modes**
- Add guides for the eye row, ground row and pivot. Positions come from the
  form profile and creature data, and the guides show in Paint and the
  flip-book.
- Add a full-silhouette onion mode next to the existing difference-only mode.
- Add **Fit content**: zoom to the opaque bounds plus a margin (the 48×48
  axolotl wastes 19 rows).

## Wave 3: tools artists expect

**PR 5. feat(jelli-art): colour ramps and shading**
- Declare ramps as data in `assets.json` (shared and named palettes), taken
  from the guide's material table. Draw the palette as ramp rows.
- Add a shade tool that steps a pixel lighter or darker within its ramp.
- Add main and secondary colours with X to swap them. Right-click paints the
  secondary colour, with an option to keep right-click as erase.

**PR 6. feat(jelli-art): drawing tool upgrades**
- Snap Shift-lines to clean ratios: 1:1, 2:1, 3:1, 1:2 and 1:3, as well as
  0° and 90°.
- Magic wand: select by colour, contiguous or global. The selection becomes a
  mask, not only a rectangle.
- Mirror around a movable axis, with a vertical mirror option.
- Add a tiled preview for the Wrap nudge option.

**PR 7. feat(jelli-art): edit several frames at once**
- Apply a stroke or Replace colour to selected frames of a clip, with one
  history step per frame and one undo.

## Wave 4: layout and interaction

**PR 8. feat(jelli-art): canvas-first layout**
- Show compare-with, specks, open edges and the before/after line only in
  Review and Paint.
- Make the asset sidebar a collapsible drawer and add ⌘P quick-open.
- The canvas fills the remaining space. The flip-book stays above the fold at
  1280×800, and the 800 px layout puts the canvas first.

**PR 9. feat(jelli-art): shortcut map and tool rail**
- Add shortcut tables per mode, with a test that fails on conflicts within a
  mode.
- Use a key font that tells O from 0.
- Print key hints on tool buttons and put them in a vertical tool rail.
- Optionally offer Aseprite-style key defaults.

**PR 10. feat(jelli-art): Activities and Test in game polish**
- Activities uses the shared form styles. Read-only names look read-only.
- Test in game gets scenario presets (hungry, asleep, night, potty), a compact
  form and the output first.

## Wave 5: bigger bets (RFC first)

- **One animation workspace.** Paint and Creature merge, with the timeline
  docked under the canvas.
- **⌘K command palette** across modes.
- **Always-on device preview.** A pinned 466×466 round panel that updates
  while painting.
- **A shared form system** for Behaviour and Activities, with inline
  validation, field-level unsaved markers and a diff against what is saved.
- **A sketch layer** that is never exported.

## Polish and nits

These can ride along with the nearest PR above:

- Make palette chip labels at least 11 px; they are about 9 px now.
- In the light theme, the disabled **Saved** button has white text on pale
  teal. Raise the contrast.
- Creature library cards repeat +clip, Duplicate and Retire for each frame.
  Move those into a menu.
- The duration bar spans the full width even for a clip with one frame.
- Tidy outline is disabled for sprites with their own palette. Say why on the
  button, not only in its tooltip.
- Number history labels per stroke: "Pencil (12 px)" instead of "Pencil".
- Name the colours of named palettes, such as axolotl, in data, instead of
  showing "axolotl 1…10".

# Shipping

Merging to `main` does not deploy by itself. Each studio PR lands in the
Release Please `jelli-art` release PR. Merging that PR tags
`jelli-art-vX.Y.Z` and publishes `ghcr.io/jrepp/jelli-art`, and the nuc host
then runs that release. How nuc pulls the image is defined in t1-hosting and
was not checked here.

When this memo was written, the repository did not allow auto-merge, and
`main` had no branch protection or required checks. Auto-merge needs both:
repository auto-merge enabled, and a ruleset that requires at least the
**Jelli Art image** workflow and core CI. Changing those settings needs the
maintainer's approval (memo-005).

# Decisions

The user decided these on 2026-10-10:

- **Outlines are 8-connected.** Diagonal steps are allowed and doubled corners
  are not, which matches the shipped art and Pixel-perfect. Fill must still be
  sealed from transparency on all four sides (open edges). PR 3 updates
  `docs/pixel-art-guide.md` to match.
- **Right-click paints the secondary colour** once main and secondary colours
  exist (PR 5). Erasing uses the eraser tool or a transparent secondary.
- **Studio releases are batched per wave.** The `jelli-art` release PR is
  merged after each wave lands, not after every PR.
- **Auto-merge is enabled,** with a `main` ruleset that requires CI checks,
  including a new studio test job (PR 0). Release Please PRs get their
  validation from dispatched runs that do not report on the PR's head commit,
  so a repository admin merges each wave's release PR by bypassing the
  ruleset.
- Wave 5 items still need an RFC before any work starts.

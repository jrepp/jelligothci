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

Two agent reviews of Jelli Art Studio 0.4.0, one prompted as a pixel artist
and one as a web UX designer. This memo merges their findings into a ranked
hit list and records what the owner decided and what shipped. Nothing beyond
the decisions recorded under Decisions is approved; the layout and workspace
items continue as a draft in RFC-006.

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
4. **The canvas is not the focus.** At fit zoom on a 32×32 creature the canvas
   takes about 26% of a 1600×1000 viewport and 8% at 1280×800, and starts at
   y=665 at 800 px. Review-only header
   controls and the asset sidebar stay visible in modes that do not use them
   (browser).
5. **The lint contradicts the style guide.**
   - The guide allows six colours, but `grown-happy` has 8 and is not flagged.
   - The project reports 264 specks and 174 open edges, so the counters are
     always red and get ignored.
   - The guide asks for 4-connected outlines, but the Pixel-perfect pencil
     produces 8-connected strokes, and the shipped art uses diagonal steps
     (browser, code).
6. **Unsaved Activities edits can be lost.** `activities.js` (on #22) never calls
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

# Outcome

| Item | Status |
|---|---|
| Hit list 1, 2 (first run, backdrops) | Done in #24 |
| Hit list 3, 7 (Alt-pick, styled confirms) | Done in #26 |
| Hit list 5 (lint matches the guide) | Done in #25 |
| Hit list 8 (eye, ground and slide guides) | Done in #27 |
| Hit list 9 (palette ramps, Shade tool) | Done in #29 |
| Line snapping, magic wand, mirror axes, tiled preview | Done in #30 |
| Deterministic Tidy (found in review) | Done in #31 |
| Hit list 6 (Activities unsaved edits) | Waits for #22 |
| Edit several frames at once | Not started |
| Hit list 4, 10 and the layout, shortcut and workspace items | Draft in RFC-006 |
| Nit: 9 px palette chip labels | Open |

Studio tests run on every pull request since #23. Releases follow ADR-011:
changes under `tools/assets/` and `assets/` belong to the game component, so
studio waves can also bump the game version.

# Decisions

The user decided these on 2026-10-10:

- **Outlines are 8-connected.** Diagonal steps are allowed and doubled corners
  are not, which matches the shipped art and Pixel-perfect. Fill must still be
  sealed from transparency on all four sides (open edges).
- **Right-click paints the secondary colour** once main and secondary colours
  exist. Erasing uses the eraser tool or a transparent secondary.
- **Studio releases are batched per wave.** The `jelli-art` release PR is
  merged after each wave lands, not after every PR.
- **Auto-merge is enabled,** with a `main` ruleset that requires CI checks,
  including the studio test job added in #23. Release Please PRs get their
  validation from dispatched runs that do not report on the PR's head commit,
  so a repository admin merges each wave's release PR by bypassing the
  ruleset.
- The layout and workspace items need an approved RFC before work starts.
- **Creatures may use 8 colours;** other kinds keep the guide's limit of 6.

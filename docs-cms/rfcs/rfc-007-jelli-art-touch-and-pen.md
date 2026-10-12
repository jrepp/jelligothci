---
title: Jelli Art Studio on iPad with touch and Apple Pencil
status: Accepted
author: Jacob Repp
created: 2026-10-11T17:03:02Z
tags: [accessibility, design, jelli-art, rfc, tooling, touch, ux]
id: rfc-007
project_id: jelligotchi
doc_uuid: ada7eae4-a823-4389-8785-d3d4c9bc7091
---

# Summary

Make Jelli Art Studio usable on an iPad with fingers and an Apple Pencil, in
Paint, Animation (the Creature timeline and the flip-book) and the game design
views (Creature, Behaviour, Reactions, Activities, Test in game). The proposal
covers four things:

- an **input model** that routes each pointer by type, with a gesture set and
  the owner's "Pencil only" switch
- a **quick bar** that replaces right-click, modifier keys, Space and hover
- **touch sizing and layout rules** for coarse pointers
- **emulated tests**, a physical iPad checklist, and four PRs, the first of
  which unblocks the other three

The owner approved this RFC on 2026-10-11 (see Approval basis). The
unresolved questions at the end take their recommendations as the defaults to
implement, unless the owner decides otherwise.

# Owner decisions (2026-10-11)

- **A hand-operated "Pencil only" switch.** It is off by default. While it is
  on, a finger never paints.
- **All views are in scope:** Paint, Animation and flip-book, Creature,
  Behaviour, Reactions, Activities, Test in game.
- **Staged delivery:** implementation lands as staged PRs that run in parallel.
- **2026-10-12: desktop layout may change.** Layout now follows the design
  point of view in [RFC-006 Decisions](rfc-006-jelli-art-workspaces-and-ui-foundation.md#decisions),
  so desktop layout can change where RFC-006 goals 1 to 3 call for it. Goal 4
  of this RFC still applies to input: every mouse, keyboard and screen-reader
  path keeps working.

# Approval basis

On 2026-10-11 the owner asked to make "the painting, animation and game design
elements much more friendly for ipad/pen usage", chose a "Pencil only" switch
operated by hand and staged parallel PRs, and asked for a design pass first.
After reviewing this design, the owner replied "approve the rfc and continue".
This approves the input model, the quick bar, the sizing rules, the test plan
and the delivery plan. The thresholds stay proposals to tune on a physical
iPad.

# Relationship to existing decisions

- **[RFC-006](rfc-006-jelli-art-workspaces-and-ui-foundation.md) (Draft).**
  Only step F1 has landed: the Studio UI tests job and the type check (#40).
  Tokens (F2), the controls kit (F4) and the workspaces (F5–F11) have not.
  RFC-006 put touch last, as F12. This RFC moves the touch work ahead of F2,
  sized so that later steps can absorb it:
  - Its CSS custom properties become F2 tokens.
  - Its gesture module becomes the input layer of F6's canvas viewport.
  - Its quick bar fits F6's context bar.
  - **It amends RFC-006 §6, "Canvas gestures".** RFC-006 says fingers stop
    painting automatically once a pen is present. The owner chose a switch
    operated by hand instead.
  - It keeps RFC-006's non-goals: no pressure brushes and no layers.
- **[ADR-011](../adr/adr-011-jelli-art-component-and-container.md)
  (Proposed).** [`compare.html`](../../tools/assets/compare.html) belongs to the
  game component. This design leaves it alone. Every rule here lives in studio
  scripts, which [`jelli_art.py`](../../tools/jelli-art/jelli_art.py) injects
  (`PAGE_SCRIPTS`). Its zoom buttons and `setZoom` are called, not edited.
- **[memo-043](../memos/memo-043-jelli-art-studio-review-and-hit-list.md).** Its
  Alt-pick fix (item 3) stays. A long press is the touch form of Alt-click: it
  picks a colour and keeps the tool.

# Motivation

The studio is built for a mouse and keyboard. Line numbers below are at
`9a67e16`.

- **Every pointer paints.** `wireCanvas` in
  [`studio.js`](../../tools/jelli-art/studio.js) (from line 631) sets
  `touch-action: none` and starts a stroke on any `pointerdown`. A resting palm
  or a second finger paints, and nothing ever reads `pointerType`.
- **Navigating the canvas needs a mouse.** Panning is a middle-button drag
  (line 636). Zooming is Ctrl/⌘ + wheel (line 659) or the header buttons.
- **Key actions need right-click or modifier keys:**
  - right-click for the secondary colour (line 638)
  - Alt-click to pick a colour (line 640)
  - Shift to snap, square or go deeper (lines 642, 649)
  - holding Space to peek at the before image (shortcut list, line 886)
- **Help lives in tooltips.** About 50 explanations exist only in `title=`, and
  some buttons show only a glyph, such as Earlier, Later and Remove in
  [`reactions.js`](../../tools/jelli-art/reactions.js) (line 554) and
  [`behaviour.js`](../../tools/jelli-art/behaviour.js) (line 243).
- **Controls are small.** Buttons are about 26 px tall (`button` padding in
  `compare.html`, line 26). The duration handles are 24 px wide
  ([`animation.js`](../../tools/jelli-art/animation.js), `.an-handle`).
- **Layouts switch at fixed window widths** that don't match iPad sizes, as
  listed under Design views below.
- **Nothing is tested for touch.** The studio README notes there are "no pen,
  touch or pinch tests" ([Tests](../../tools/jelli-art/README.md)), and the
  tablet size in [`fixture.json`](../../tools/jelli-art/ui_tests/fixture.json)
  runs in a browser context without touch
  ([`conftest.py`](../../tools/jelli-art/ui_tests/conftest.py), `open_view`).

# Goals and non-goals

**Goals.**

1. **Paint well with a Pencil and get around with fingers.** Pencil users can
   draw, pick colours, constrain shapes, peek, undo, pan and zoom without a
   keyboard.
2. **Finger-only users** can do all of that too, with the switch off.
3. **Every view works on touch.** No action anywhere needs a modifier key,
   right-click or hover.
4. **Mouse and keyboard keep working.** Every mouse, keyboard and
   screen-reader path keeps working unchanged, including on a keyboard-attached
   iPad.

**Non-goals.**

- Pressure, tilt or brush dynamics (RFC-006).
- Phone layouts (RFC-006, F12).
- The RFC-006 workspace redesign.
- The Review tab's swipe and flip modes.
- A native app.

**Target devices.**

- **iPads:** iPad mini (744×1133 CSS px portrait), iPad and iPad Air (820×1180)
  and iPad Pro 13" (1032×1376), in both orientations, in Safari and as a
  home-screen web app.
- **Pencils:** Apple Pencil 1st and 2nd generation, USB-C and Pro.
- **Also:** finger-only use, and an attached keyboard or trackpad.

## What the browser can and cannot see

| Signal | In iPadOS Safari | Use in this design |
|---|---|---|
| `pointerType` | `pen` for Pencil, `touch` for a finger, `mouse` for a trackpad | Routes every pointer |
| `pressure`, `tiltX/Y` | Reported for a Pencil | Not used (non-goal) |
| Pencil hover (M2 and later iPads with Pencil 2 or Pro) | `pointermove` with `buttons === 0` and no contact | Brush ghost and readout, as for a mouse |
| Pencil double-tap and squeeze | Not exposed to the web | Not available; use the quick bar |
| Scribble | Handwriting in editable fields | Leave it on in inputs; the canvas isn't editable |
| Long-press callout and text selection | On by default | Off on the canvas and controls (`-webkit-touch-callout`, `user-select`) |
| Double-tap to zoom | On unless `touch-action: manipulation` | `manipulation` on controls; `none` on the canvas and handles |
| Pinch zooming the page | Ignores `user-scalable=no` | Stopped on the canvas by `touch-action: none`; allowed elsewhere |
| `gesturestart/change` | Proprietary to Safari | Not used; pointer events are enough |
| `(any-pointer: coarse)`, `(hover: none)` | Match on touch iPads | Set sizing and hover fallbacks |
| Palm rejection | Done by iPadOS for the Pencil, not guaranteed for web content | Defended against; see below |

# Detailed design

## 1. Input model

A new studio module, `touch.js`, injected right after `shell.js`, owns pointer
classification, gestures and long press. Paint and the timeline use it. Its
constants are proposals to tune on the device, and the tests use the same
constants.

**Routing by pointer type.**

| Pointer | Pencil only **off** (default) | Pencil only **on** |
|---|---|---|
| `mouse` | Today's behaviour | Today's behaviour |
| `pen` | Paints with the current tool | Paints with the current tool |
| one `touch` | Paints | Drags to pan; a tap shows that pixel in the readout |
| two `touch` | Pan and pinch zoom | Pan and pinch zoom |

**The Pencil only switch.**
- It is a toggle button (`aria-pressed`) on the quick bar, kept per viewer in
  `store` with the other paint settings.
- A tooltip can't explain it, so its label says what it does.
- It changes routing only. The tool, the colours and the history are untouched.

**Multi-touch arbitration.**
- **A second finger cancels a fresh stroke.** If a second touch lands within
  **250 ms** of the first, a finger stroke in progress is cancelled through the
  existing `cancel()`, which restores the snapshot and leaves no history
  entry. Then the gesture starts. A second touch after that is ignored until
  the stroke ends, so a long stroke is never lost.
- **No delay before painting.** A finger paints at once, so painting stays
  responsive. If a gesture turns out to be starting, the first pixel is
  undone.
- **Palm defence.** A `touch` that arrives while a pen stroke is in progress,
  or within **300 ms** after a pen lifts, never paints. It can still take part
  in a two-finger gesture.
- **Pen takes over.** A `pen` down during a finger gesture ends the gesture,
  commits the zoom, and starts the stroke.

**Gestures on the canvas.**

| Gesture | Action | Recognised when | Why |
|---|---|---|---|
| Two-finger drag | Pan the scroller | The midpoint moves more than 8 px | Every drawing app does this |
| Pinch | Zoom, anchored at the pinch midpoint | The finger distance changes more than 12% | Replaces Ctrl/⌘ + wheel |
| Two-finger tap | Undo | Both fingers down within 100 ms and up within 350 ms of the first, each moving at most 12 px | The Procreate convention; undo is the most frequent recovery |
| Three-finger tap | Redo | As for two fingers | Pairs with undo; see open question 5 |
| Finger long press | Pick a colour and keep the tool (memo-043 item 3) | Held 500 ms, moving at most 8 px | Replaces Alt-click; the iOS long-press time |

- **Pinch zoom.** `setZoom` re-renders the stage, and its zoom is a whole
  number from 1 to 32 (`compare.html`, line 548). So while pinching, the canvas
  is scaled with a CSS transform. On release, it snaps to the nearest whole
  zoom and calls `setZoom` once, using `studio.js`'s existing `anchor` logic.
  Panning and zooming happen together.
- **Long press is for fingers only.** Artists hold a Pencil still while
  thinking, so a pen never picks by dwelling. With the switch on, "Pencil draws,
  finger holds to pick" is the intended pairing.
- **Mouse, keyboard and accessibility don't change.**
  - Mouse buttons, modifier keys and the keyboard cursor (`canvasKey`) behave as
    today.
  - Gestures register in the shell's shortcut registry, so the help dialog
    lists them in a new **Touch and Pencil** section.
  - Every gesture has an on-screen button, so nothing depends on fine motor
    control.

## 2. Replacing modifiers, right-click and hover

**The quick bar.** A strip of 44 px buttons beside the canvas. Every modifier
works two ways:
- **Hold it** with a finger: it applies while held. This is the two-handed
  Pencil grip: a thumb on the bar and the Pencil on the canvas.
- **Tap it:** it latches until tapped again. One-handed and finger users need
  this.

It is shown by default on `(any-pointer: coarse)`, and can be turned on for
desktops.

| Today | Code | Replacement |
|---|---|---|
| Right-click paints the secondary colour | `wireCanvas`, `e.button === 2` (studio.js line 638) | **2nd** modifier |
| Alt-click picks and keeps the tool | `wireCanvas`, line 640; `pick()` | Finger long press; **Pick** modifier |
| Alt + right-click picks the secondary colour | line 640 | **Pick** with **2nd** |
| Alt-drag with Select copies | `pressSelect` | The **Pick** button relabels to **Copy** with Select |
| Shift snaps a line, makes a square or circle, shades deeper, or selects a colour everywhere with the wand | `press`, `drag` (lines 642, 649); `TOOLS` | One **Constrain** modifier, labelled for the tool: Snap, Square, Circle, Deeper or Everywhere |
| Middle-drag pans | line 636 | Two-finger drag (one finger with the switch on) |
| Ctrl/⌘ + wheel zooms | line 659 | Pinch; the header −, + and Fit buttons stay |
| Hold Space to peek at before | `state.peek`; shortcut list line 886 | **Peek**: shows before only while held |
| X swaps the colours | `S.keydown`; ⇄ Swap in `renderPalette` | **Swap** on the quick bar too |
| Alt + arrows nudge; Esc deselects | `S.keydown`; Nudge and Deselect under More tools | A selection row on the quick bar, shown while there's a selection: ← ↑ ↓ →, Deselect |
| Esc cancels a stroke | `cancel()` | A second finger within 250 ms, or Undo |
| Right-click, Alt-click or Shift-click on a colour chip: secondary or isolate | `renderPalette` `chip()`: `onclick`, `oncontextmenu` | Long-press a chip for a menu: Use as secondary, Isolate, Edit slot (✎) |
| Hovering shows the brush ghost and readout | `draw()` hover, `renderReadout` | Pencil hover where supported; otherwise the readout shows the last touched pixel |

**Help that isn't hidden in tooltips.**
- **Glyph-only buttons get visible words** on coarse pointers. For example, ▲
  and ▼ become "▲ Earlier" and "▼ Later" (reactions.js line 554, behaviour.js
  line 243, the Nudge and Mirror-axis glyphs in studio.js). Their
  `aria-label`s stay.
- **Longer explanations become a tap-to-reveal ⓘ** next to their group label.
  `touch.js` provides one handler for every `[title]`: a 500 ms long press on a
  control shows its title as a popover. It is dismissed by a tap elsewhere and
  announced through `JelliShell.announce`.
- **Keyboard hints hide until a keyboard is used.** The `kbd` hints in buttons
  (such as `flipbook.js`'s `kbd('←')`) are hidden until the first `keydown`.
  `touch.js` then sets `html[data-keys]`, so a keyboard-attached iPad shows them
  again.
- **Toasts** also pause while touched: `pointerdown` joins the existing
  `mouseenter` and `focusin` (shell.js line 62).

## 3. Sizing and layout

- **Tokens.** Until RFC-006 F2 lands, `touch.js` defines a small set of custom
  properties on `:root` that F2 later absorbs:
  - `--tap: 44px`, the minimum target size (WCAG 2.5.5 AAA; RFC-006 §5 sets
    44 px for coarse pointers)
  - `--tap-gap: 8px`
  - `--hit-pad: 10px`, invisible extra hit area
- **Coarse pointers.** Under `@media (any-pointer: coarse)`, these controls
  get `min-height: var(--tap)` and `min-width: var(--tap)`:
  - buttons, `.seg` buttons, chips, selects and inputs
  - `[role=slider]` and range thumbs
  - `.an-handle`
  - flip-book and timeline cards

  Thin handles keep their look and grow an invisible `::before` hit area by
  `--hit-pad`. The query is `any-pointer`, so attaching a trackpad doesn't
  shrink the targets while fingers still work.
- **Hover-only styles** go under `@media (hover: hover)`. On touch, a hover
  never acts as a state, and nothing appears only on hover.
- **Layout follows each view's own width.** Views switch layout on container
  queries on their root element instead of the window width. Today they use
  fixed window widths:
  - `studio.js` line 74: 1100
  - `creature.js` line 34: 1100
  - `behaviour.js` line 26: 1100
  - `reactions.js` lines 42–43: 1300 and 800
  - `activities.js` line 20: 1000
  - `game_preview.js` line 29: 900

  The asset list (290 px, `compare.html` line 21) can collapse into a drawer
  from a shell **List** button. A drawer is a CSS override that `touch.js`
  injects, with no change to `compare.html`.

## 4. Per-view design

### Paint

Landscape, iPad Air 1180×820. The list is collapsed and the quick bar is on the
left. Right-handed is the default; a Left-handed setting mirrors the layout.

```text
+--------------------------------------------------------------------------+
| header: tabs  compare-with  save state                   [List] [Help]   |
| Tool [Pencil][Eraser][Fill][Pick][Shade][Line][Rect][Ellipse][Sel][Wand] |
+----+-----------------------------------------------+---------------------+
|Undo|                                               | colours (44px chips)|
|Redo|                                               | main/2nd   [Swap]   |
|----|                 canvas                        |---------------------|
|Pick|          (pinch zoom, 2-finger pan)           | flip-book / before  |
|2nd |                                               | history             |
|Snap|                                               |                     |
|Peek|                                               |                     |
|Swap|                                               |                     |
|----|                                               |                     |
|Pen |  <- "Pencil only" switch                      |                     |
+----+-----------------------------------------------+---------------------+
```

Portrait, 820×1180. The side panels go below the canvas, as they already do
under 1100 px. The quick bar is a row at the bottom of the canvas, where a thumb
reaches.

```text
+--------------------------------------+
| header                 [List] [Help] |
| Tool row (wraps)   More tools ▸      |
+--------------------------------------+
|                                      |
|               canvas                 |
|                                      |
+--------------------------------------+
|Undo Redo|Pick 2nd Snap Peek Swap|Pen |
+--------------------------------------+
| colours · flip-book · before · hist. |
+--------------------------------------+
```

- **The selection row** (nudge arrows, Deselect, Copy, Cut, Paste, Delete)
  replaces the modifier group while there's a selection.
- **The paint hint** (`#paint-hint`, line 610) shows the touch and Pencil
  wording on coarse pointers, and today's wording otherwise.

### Animation: timeline and flip-book

- **Duration handles.**
  - The hit area grows to 44 px.
  - A **Fine (1 ms)** toggle next to "Durations" replaces holding Shift while
    dragging (`wireHandle`: `m.shiftKey ? 1 : 10`).
  - The number field on each card stays, for exact values (iPadOS opens a
    numeric keypad).
- **Scrubbing.** Dragging a finger or Pencil along the duration track moves
  the playhead and seeks (today a tap on a segment seeks, `renderTrack`).
- **Reordering.** The ◀ Move and Move ▶ buttons are the touch path for
  reordering frames. HTML drag-and-drop (`wireDrag`) stays for mice; its
  behaviour on iPadOS is on the device checklist.
- **The flip-book needs only sizing.** Every key action already has a button:
  Play, Frame ◀ and ▶, Onion skin, Changes or Silhouette, frames each side
  (flipbook.js lines 266–274). Its `kbd` hints follow the keyboard rule above.
- **Creature transport.** Space, `,` `.` `J` `K` and 1–9 (creature.js, from
  line 399) already have buttons and pose cells. They only need sizing.

```text
Durations  [Fine 1 ms]                    ▶ Play  ◀ Frame  Frame ▶
|== 1 · 120 ==|=== 2 · 180 ===|= 3 · 80 =|==== 4 · 240 ====|
              ^ 44 px handle hit area (drag), track body scrubs
[thumb][◀ Move][Move ▶][Paint ✎]  [thumb][◀ Move][Move ▶] ...
```

### Game design views (Creature, Behaviour, Reactions, Activities, Test in game)

- **Inspectors and lists** use the coarse token sizes, with 8 px spacing.
  Number fields get +/− steppers where the field takes whole steps.
- **Glyph-only row actions** get words (§2). Destructive actions, such as
  Remove rule and Remove reaction, keep their confirmation.
- **Two columns in landscape, one in portrait.** At 1180 px landscape with the
  list collapsed, each view shows list and inspector side by side. At 820 px
  portrait it stacks them, with the preview pinned at the top of the
  inspector.
- **`prompt()` stays for now** (behaviour.js lines 290 and 295). It works on
  iPad. RFC-006 F4 replaces it.

```text
Landscape                              Portrait
+-----------+----------------------+   +------------------------+
| list      | preview              |   | preview (sticky)       |
| (44px     |----------------------|   |------------------------|
|  rows)    | inspector sections   |   | list ▾ (collapsible)   |
|           | [▲ Earlier][▼ Later] |   | inspector sections     |
+-----------+----------------------+   +------------------------+
```

## 5. Testing and verification

**Emulation in Playwright.** This runs in the existing Studio UI tests job,
which uses Chromium.

- **Contexts.** New `ipad-portrait` (820×1180) and `ipad-landscape` (1180×820)
  cases with `has_touch=True` and `device_scale_factor=2`. The existing
  desktop, tablet and phone screenshot cases don't change.
- **Pencil input:** CDP `Input.dispatchMouseEvent` with `pointerType: "pen"`.
- **Finger input, including several fingers:** CDP `Input.dispatchTouchEvent`
  with several `touchPoints`, which gives real `touch` pointers with distinct
  `pointerId`s.
- **Shared helpers** (`pen_stroke`, `touch`, `pinch`, `tap`, `long_press`) in a
  `ui_tests/touch.py` module, so every test drives input the same way.
- **The tests must assert that:**
  - a pen stroke paints the expected pixels, and a finger paints only while the
    switch is off
  - a second finger within the window leaves the pixels and history unchanged
  - a pinch changes `state.zoom` to a whole number and keeps the midpoint pixel
    under the midpoint
  - a two-finger tap undoes one step, and a three-finger tap redoes it
  - a long press picks a colour and keeps the tool, and with Select, Copy
    copies
  - each quick-bar modifier matches its mouse or keyboard equivalent, pixel for
    pixel
  - a duration handle drag with Fine on moves in 1 ms steps
  - track scrubbing seeks
  - every interactive control in each view measures at least 44×44 px at both
    iPad sizes, as an assertion rather than a report
  - pages never scroll sideways
  - the existing axe and aria checks pass at the iPad sizes

**On a physical iPad.** Emulation can't verify these, so they're checked by
hand on a real iPad after each release that touches input. The results go in a
memo.

1. A palm resting while drawing with a Pencil leaves no marks, with the switch
   off and on.
2. Pencil hover shows the brush ghost (M2-class iPad and Pencil only).
3. Pinch, two-finger pan and two-finger undo feel immediate, and Safari never
   zooms or scrolls the page from the canvas.
4. iPadOS's own three-finger gestures (the undo HUD, copy and paste) don't fire
   instead of redo.
5. A long press shows no callout, magnifier or text selection.
6. Scribble still works in name, rename and number fields.
7. HTML drag-and-drop reordering in the timeline either works or stays out of
   the way.
8. With a Magic Keyboard attached: shortcuts, `kbd` hints and trackpad
   right-click all work.
9. Rotating the iPad re-lays out each view without losing unsaved edits.
10. The home-screen web app behaves like Safari.

## 6. Delivery plan

There are four PRs. **T0** goes first, because it owns every shared file. Then
**T1–T3** run in parallel, each owning its files outright. Each PR updates its
own section of `docs/artwork.md` and the studio README.

| PR | Owns | Contents |
|---|---|---|
| **T0: touch foundation** | new `touch.js`; `jelli_art.py` (`PAGE_SCRIPTS`); `shell.js`; `ui_tests/conftest.py`, `fixture.json`, new `ui_tests/touch.py`; the README Tests section | Pointer classification and gesture recogniser with tests; `--tap` tokens and coarse-pointer rules; `html[data-keys]`; long-press title popover; List drawer; toast pause; help section API; iPad contexts and helpers |
| **T1: Paint** | `studio.js`, `paint_tools.js`, `lint_ui.js`, new `ui_tests/test_touch_paint.py`, Paint baselines | Routing and switch; quick bar and selection row; pinch preview and commit; long-press pick and chip menu; touch hint text |
| **T2: Animation** | `animation.js`, `flipbook.js`, `creature.js`, new `ui_tests/test_touch_animation.py`, Creature baselines | Handle hit areas; Fine toggle; scrubbing; transport and flip-book sizing; container queries for Creature |
| **T3: design views** | `behaviour.js`, `reactions.js`, `activities.js`, `game_preview.js`, `simulator.js`, new `ui_tests/test_touch_design.py`, their baselines | Words on glyph buttons; ⓘ help; steppers; container-query layouts |

- **Studio release.** T1–T3 ship together as one studio release, so artists
  don't see half the touch model. With the auto-update timer on nuc (t1-hosting
  memo-039), the release reaches `jelli-art.home.jrepp.com` within minutes.
- **After the release,** the owner runs the physical checklist and the findings
  go in a memo.

# Drawbacks

- **Touch work before the foundation.** Landing touch before RFC-006's
  foundation means some CSS and markup will be redone in F2–F11. The tokens and
  the gesture module are meant to carry over.
- **A quick bar takes width.** In landscape that's about 52 px of canvas.
- **Emulation isn't Safari.** Chromium emulation can't prove how iPadOS Safari
  behaves. The physical checklist is the real gate, and it depends on the owner.
- **Two systems of modifiers.** Keyboard modifiers and quick-bar modifiers
  coexist. Their state must stay consistent; for example, a held Shift and a
  latched Constrain combine as "or".

# Alternatives

- **Switch automatically once a pen is seen** (RFC-006 §6). The owner declined
  it in favour of a switch operated by hand.
- **Safari gesture events for pinch.** They're proprietary, also fire for macOS
  trackpads, and need their own test path. Pointer events cover iPad and
  desktop touchscreens alike.
- **Wait for RFC-006 F12.** That leaves the iPad unusable for weeks of
  foundation work.
- **A native iPad app or a canvas library.** Out of proportion for a no-build
  studio (RFC-006 Alternatives).

# Adoption strategy

1. The owner approved this RFC on 2026-10-11. The unresolved questions'
   recommendations are the defaults until the owner says otherwise.
2. T0, then T1–T3 in parallel, through the usual author-and-reviewer PRs.
3. One studio release, then the physical checklist and a memo.
4. RFC-006 is amended to point its §6 and its F12 step here.

# Unresolved questions

Each recommendation below is the default to implement unless the owner decides
otherwise.

1. **Offer the switch on the first Pencil?** When a `pen` is first seen, show a
   one-time hint "Using a Pencil? Turn on Pencil only" with a button.
   *Recommendation: yes.* It keeps the switch manual, as decided, and makes it
   easy to find.
2. **Quick bar on desktop?** *Recommendation: hidden on fine pointers, with a
   setting to show it.*
3. **Which side is the quick bar on?** *Recommendation: left by default in
   landscape, with a Left-handed setting that moves it right.*
4. **Long-press timing.** *Recommendation: 500 ms for fingers, and never for the
   Pencil.* Revisit after the physical check.
5. **Three-finger redo.** iPadOS's own three-finger gestures may take it first.
   *Recommendation: build it behind the Redo button and keep it only if
   checklist item 4 passes.*
6. **A WebKit test run?** Playwright's WebKit isn't iPadOS Safari, but it would
   catch `-webkit-` CSS and `touch-action` mistakes. *Recommendation: add it
   later as a non-blocking layout-only check, if the pinned test image can run
   it.*
7. **Order against RFC-006.** Do T0–T3 before F2? *Recommendation: yes.* The
   owner asked for iPad use now, and T0's tokens and gesture module are built to
   be absorbed into F2 and F6.

# Amendments

**2026-10-12: header chrome (UX M2-A; RFC-006 Decisions D2 and D4).** The
header wireframes in §4 show "tabs, compare-with, save state" on one row in
every mode. Now:

- Row 1 is the same in every mode: tabs, the save state (live badge, repository
  status, **Unsaved**), then **Settings**, **Shortcuts**, **Guide** and **Theme**
  (plus **List** on a coarse pointer). It keeps its height whether or not
  anything is unsaved.
- Row 2, with the before and after refs, **Detail** or **Sheet**, compare-with
  and the lint totals, is Review-only. In Paint, compare-with is in the
  **before** pane beside the canvas, bound to the same setting.
- "Your name" is in the **Settings** popover instead of the header. It is still
  asked for once, before the first save of a visit, when it is empty.
- The **Unsaved** pill, the tab dots, the repository status and Paint's title
  row reserve their space on every pointer, not only coarse ones, so the canvas
  does not move when they change.

# References

- [RFC-006](rfc-006-jelli-art-workspaces-and-ui-foundation.md),
  [ADR-011](../adr/adr-011-jelli-art-component-and-container.md),
  [memo-043](../memos/memo-043-jelli-art-studio-review-and-hit-list.md).
- Studio sources at `9a67e16`: [`studio.js`](../../tools/jelli-art/studio.js),
  [`animation.js`](../../tools/jelli-art/animation.js),
  [`flipbook.js`](../../tools/jelli-art/flipbook.js),
  [`creature.js`](../../tools/jelli-art/creature.js),
  [`shell.js`](../../tools/jelli-art/shell.js),
  [`compare.html`](../../tools/assets/compare.html),
  [UI tests](../../tools/jelli-art/ui_tests/conftest.py).
- [Artwork guide](../../docs/artwork.md) and
  [studio README](../../tools/jelli-art/README.md).

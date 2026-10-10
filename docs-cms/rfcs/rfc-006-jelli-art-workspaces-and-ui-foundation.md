---
title: Jelli Art Studio workspaces, zen chrome and UI foundation
status: Draft
author: Jacob Repp
created: 2026-10-10T19:31:38Z
tags: [accessibility, design, jelli-art, responsive, rfc, tooling, ux]
id: rfc-006
project_id: jelligotchi
doc_uuid: 5dac9d8d-43c8-4c4f-a5f9-8d1cf8e10e2c
---

# Summary

Rebuild the Jelli Art Studio front end around two things:

- **Workspaces.** Each workspace serves one task: art (Paint, Animate, Review),
  game content (Creature, Behaviour, Activities) or Test in game. Each one
  shows only what its task needs, at three chrome levels (Full, Focus and
  Zen), and adapts to the space it has, from a wide desktop down to a phone.
- **A sustainable foundation.** The studio keeps the current no-build setup,
  adding design tokens, CSS cascade layers, light-DOM custom elements and a
  small in-house controls kit on native browser primitives. It also gets
  JSDoc type checks and browser tests in CI.

The work lands as a series of small PRs, each of which ships. Existing
behaviour, deep links and save safety keep working throughout.

This RFC is a proposal. Nothing in it is approved until the user approves it.

# Motivation

The owner wants a studio that can be supported and used in production, with
responsive layouts and "art task specific and game content specific views
with a more zen approach, auto hiding of toggles so that we have clean
information priority". memo-043 recorded the first symptoms (hit list items
4 and 10, and wave 5). A design and research pass on 2026-10-10 measured the
rest. It had three parts: a code inventory, external research, and a workspace
design checked against the live studio at four sizes.

## Evidence

- **The canvas is a guest on its own page.** In Paint it takes about 26% of
  the viewport at 1600×1000, 8% at 1280×800 and 5% at 1024×768. On a 390×844
  phone the first canvas pixel is about 1,100 px down. At 1280×800, 92% of the
  screen is chrome.
- **Every mode carries every other mode's chrome.** Behaviour, Test in game
  and Activities show the 290 px asset sidebar and the compare and lint header
  row, which they never use. On the Activities branch (PR #22) the sidebar
  reads "No assets match".
- **Content views are long, flat forms.** Each reaction in a repertoire is
  two rows of 11 controls. The Activities preview scrolls out of view while
  you edit.
- **There is no design system.**
  - About 370 CSS rules sit in about 10 `<style>` blocks, appended in load
    order.
  - The only tokens are colours, and four of them are unused.
  - There is no scale for spacing, type or radius. Font sizes come in nine
    values (9–20 px), and seven unrelated width breakpoints are in use.
  - The cascade relies on specificity hacks such as `:root button.primary`.
- **There are no shared controls.** Each module defines its own escaping,
  option lists and number fields. There are three tab implementations, two
  palette-chip components, two dialog systems and a `prompt()`.
- **Keyboard support has gaps.** Every toggle group is a set of `aria-pressed`
  buttons without arrow-key navigation, so the Tool group alone is eight tab
  stops.
- **Layout depends on the window size.** Sizes are computed from
  `window.innerWidth/innerHeight` and magic offsets at render time, so hiding
  chrome or changing width leaves canvases the wrong size.
- **Editing breaks focus and is unsafe.** Most edits rebuild panels with
  `innerHTML`, which drops focus in Behaviour, Reactions and Activities after
  every change. `innerHTML` is also an injection risk wherever an escape is
  missed.
- **Views are wired to each other.** New views hard-code their siblings' ids
  and extend the page by reassigning globals (`renderView`, `select`,
  `S.keydown`).
- **Nothing tests layout, appearance or accessibility.** Node tests cover
  only the pure modules.

The full reports are summarised under References at the end.

# Goals

1. **Task-first layouts.** Every workspace gives its main surface the
   **chrome budget** (the share of the viewport that surface keeps) set out
   below. Anything a task only needs on demand gets no permanent pixels.
2. **Zen without lost access.** Chrome recedes while you work, but keyboard,
   screen-reader and touch users never lose a control. Save state and errors
   never hide.
3. **Responsive by default.** Layouts follow the space actually available,
   including when a dock is open. Touch and pen are first-class on a tablet,
   and the phone scope is deliberate.
4. **One controls layer.** Every module uses the same tokens, controls,
   keyboard models and dirty-state contract.
5. **Sustainable code.**
   - No build step and no runtime CDN.
   - Few dependencies, and every one vendored and pinned.
   - Types checked in CI.
   - Browser tests for layout, accessibility and keyboard behaviour.
6. **No regressions.** Painting stays palette-locked; drafts, stale-save
   (409) checks, the real-engine render, deep links, high contrast and reduced
   motion keep working.

# Non-goals

- Changing game data formats or the engine. Data-driven content stays the
  rule.
- Real-time collaboration, accounts or a server rewrite.
- Layers, pressure-sensitive brushes or vector tools.
- A new visual brand. The existing plum, cream and mint theme stays.

# Detailed design

## 1. Foundation

**Stay no-build.** At about 8,000 lines and a handful of users, a bundler
would add a Node toolchain, dependency churn and audit surface, and that costs
more than it returns. Revisit if the front end passes about 25,000 lines or
gains dedicated front-end contributors.

**CSS architecture.**
- One stylesheet, `tools/jelli-art/ui/ui.css`, declared as
  `@layer reset, tokens, base, controls, workspaces, utilities;`.
- **Module styles.** Each module's styles move into the `workspaces` layer in
  its own file. They stop being `<style>` strings inside JavaScript.
- **Tokens:**
  - colour, kept as it is
  - space (4 px steps)
  - radius (3 steps)
  - the type scale (below)
  - density (28, 32 and 44 px controls)
  - z-index (named levels)
  - motion (duration tokens, set to zero under reduced motion)
- **Themes.** Each theme (system, dark, light, contrast) is defined once.
  The duplicated media-query and attribute blocks go.
- Container queries do layout. `:has()` reads shell state from the DOM.

**Components.**
- Light-DOM custom elements (`<jelli-palette>`, `<jelli-timeline>`,
  `<jelli-inspector>`…) give each panel a mount and unmount lifecycle and a
  clear boundary. They use the light DOM so global tokens, `:has()` and test
  locators keep working.
- Shadow DOM is not used.

**Rendering.** A small tagged-template helper, `html`…``, escapes by default
and patches named nodes instead of replacing whole panels with `innerHTML`.
Focus, selection, scroll and the text cursor in input fields then survive
edits.

**Platform primitives instead of libraries.**
- `<dialog>` for modals, drawers and sheets.
- The `popover` attribute for menus, pickers and tooltips.
- Invoker commands (`commandfor`/`command`) for declarative toggles.
- CSS anchor positioning, with a centred fallback for older browsers.
- WAI-ARIA Authoring Practices patterns for composite widgets: toolbar,
  radio group, listbox, tabs, grid and tree.

All of these work in current evergreen browsers as of 2026.

**State.**
- One studio store. Workspaces subscribe to the parts they read. Each
  document (sprite, clips, creatures, behaviours, activities) has one owner
  module that does its saving and dirty tracking. The existing drafts and
  409 contracts carry over unchanged.
- Views register through the shell. They no longer reassign globals or
  hard-code each other's ids.

**Types.**
- Plain `.js` with JSDoc types, checked by `tsc --noEmit --checkJs` in CI
  only. The browser still loads the files untouched.
- TypeScript is pinned through the existing `toolchain.env` and `scripts/uv`
  pattern, or through a CI-only `npx` with a lockfile.

**Escape hatch.**
- If hand-written patching gets painful for the two most stateful widgets
  (Timeline and the Behaviour editor), vendored Lit (`lit-core.min.js` 3.3.x,
  about 6 KB, BSD-3) is the one framework pre-approved for them.
- Adopting it needs an ADR.

## 2. Shell

The page frame shared by every workspace:

- **App bar (44 px).**
  - Logo mark.
  - Workspace switcher, grouped as **Art: Paint · Animate · Review |
    Content: Creature · Behaviour · Activities | Test**.
  - Breadcrumb.
  - Save-state pill.
  - ⌘K.
  - Overflow menu: theme, guide, shortcuts, artist name. Compare-with also
    lives here, in the art workspaces only.
- **Breadcrumb.** Shows the subject as segments you can pick from, for example
  `Animate › Axolotl ▾ › idle ▾ › frame 3 ▾`. It replaces the asset sidebar
  as the main way to navigate everywhere except Review.
- **⌘P quick-open.** One search across every subject: assets (with
  thumbnails), forms, clips, states, repertoires and activities. Each result
  opens in the workspace that owns it, and ⌘↵ opens it in the other relevant
  one. An empty query shows recent and unsaved items.
- **⌘K command palette.** Fed by one command registry, which also drives
  menus and key bindings. Every hidden function is reachable here.
- **Save-state pill.** One place for save state: Saved, ● N unsaved, Saving…,
  Commit queued, Conflict and Offline. Its menu groups unsaved items by
  workspace, with Open, Save and Revert for each.
  - ⌘S saves the current workspace and ⇧⌘S saves everything.
  - Switching workspace never discards edits and never prompts.
  - Every document registers with the shell's dirty tracking, including
    Activities (memo-043 #6).
- **Return chip.** A jump between workspaces (for example "Edit art" from a
  behaviour effect) leaves `← Back to Behaviour: curious` until it is used or
  dismissed.
- **Deep links.** Old hashes (`#paint`, `#creature`, `#behaviour`, `#review`,
  `#view=…`) redirect to the matching workspace and subject. Draft keys are
  migrated once.

## 3. Workspaces

Each workspace remembers its own subject, panel sizes and chrome level. These
are conveniences kept in browser storage, guarded so missing storage never
breaks the page.

| Workspace | Subject | Primary surface | Persistent | Contextual / on demand |
|---|---|---|---|---|
| **Paint** | asset | canvas | tool rail with key hints; HUD chip (tool, main/secondary colour, zoom, x,y); lint verdict chip; right panel (palette ramps, view toggles, history) | tool-options bar only for tools with options; selection bar only while a selection exists; Tidy diff; before-image peek; waiver |
| **Animate** | form › clip › frame | canvas with onion skin + flip-book at game scale | timeline dock (frame cards, duration track, shared playhead, transport) | slide/eye ⚠ on frame cards; per-card ⋯ menu (duplicate, blank, retire); frame library drawer; "All poses" sheet |
| **Review** | asset or change set | compare viewer | change list (where the asset list belongs); compare-with; footer with verdict, note, counter | lint delta only when it changed; sheet view; physical size; copy notes |
| **Creature** | form | round panel preview with limit boxes | profile scales; pose rules with the rule that fires highlighted; idle beat timeline | condition chips; quiet-cycle detail; shared-profile banner with "Copy as new" |
| **Behaviour** | state / repertoire | inspector | master list (states, repertoires, global rules); **consequence pane** (look preview + simulator verdict for the selection) | full simulator table; odds over 600 s; conditions per reaction row |
| **Activities** | activity | inspector in four collapsible sections: When, Who & where, Economy, Look | list grouped by kind; **day strip** of every activity's time window; preview pane | per-pet bonus table; prerequisite graph |
| **Test in game** | scenario | rendered frames and what the engine accepted | scenario presets (Fresh, Hungry, Night asleep, Potty urgent, Low mood) | full scenario form; build log |

**Test in game in any workspace.** Test in game is also a dock that any
workspace can open with ⌘J. It renders the current subject in its natural
scenario. It never re-renders on keystrokes: it shows "stale" with the age of
the last save and renders on request or after a save.

**Reactions as a table.** Reactions become one row each: stimulus, value,
→ state, weight, chance, and a summary chip such as `night · bond ≥ 300`.
Selecting a row opens its conditions in the inspector. That cuts 22 controls
per reaction to 6 visible cells.

**Animate merges two modes.** It combines today's Creature timeline with
Paint-and-flip-book, the "one animation workspace" bet in memo-043. Creature
keeps sizing and pose rules. Paint stays available for any sprite, including
a clip frame.

## 4. Chrome levels and auto-hide

| Level | Art workspaces | Content workspaces |
|---|---|---|
| **Full** | every panel docked | list + inspector + consequence pane |
| **Focus** (default) | panels docked; contextual bars only when relevant | sections collapsed except the one in use |
| **Zen** (`\`; ⇧`\` pins) | panels auto-hide; canvas fills; HUD only | inspector only; the list collapses into the breadcrumb picker |

In content workspaces the calm comes from progressive disclosure and a pinned
consequence view. Hiding a form's labels or list while someone compares
numbers would get in the way.

### Hiding rules (art workspaces, Zen)

1. **During a stroke.** Overlay panels under the stroke's bounds fade to 15%
   and let pointer events through until 400 ms after the stroke ends. Docked
   panels never move or resize the canvas mid-stroke.
2. **Idle timer.** Applies when the pointer is on the canvas, no button is
   held and focus is not in a panel. After 2.5 s without movement over the
   panels, the side panels slide out. Moving over the canvas doesn't reset the
   timer; touching a panel does.
3. **Focus holds panels open.** Any focus inside a panel (`:focus-within`)
   keeps it open, and an open popover blocks the timer.
4. **Contextual bars follow their condition,** not the timer.

### What never hides

- the save pill and dirty dots
- errors, conflicts and offline state
- the HUD chip
- the playhead while playing
- an open modal
- the return chip
- anything holding keyboard focus

### Bringing panels back

- **Edge hover.** A 24 px zone with a 250 ms delay before opening and 600 ms
  grace before closing. It applies only on `pointer: fine`, never on the top
  edge.
- **Keys.** `\` toggles Zen. F6 or Tab moves through the regions (rail,
  canvas, panel, timeline, app bar), and the focused region reveals itself.
  Esc returns focus to the canvas.
- **Touch.** Visible 12×48 px grab handles. Hover is never required.

### Accessibility

- **Hidden panels stay reachable.** Auto-hidden panels stay in the tab order
  and the accessibility tree: they are moved with transforms and opacity,
  never `display: none` or `inert`. Only panels a person explicitly closes
  leave the tree, and their toggle remains.
- **Announcement.** Zen is announced once per session through the existing
  live region.
- **Reduced motion** makes changes instant. **Forced colours and more
  contrast** keep the grab handles visible and give the HUD a solid border.
- **Auto-hide setting:** On, Only while drawing, or Never. The default is
  **Only while drawing**, so the idle timer is opt-in until the artist has
  tried it.
- **WCAG.** This follows WCAG 2.2 success criteria 1.4.13 (content on hover
  or focus), 2.4.11 (focus not obscured) and 2.2.1 (timing adjustable).

## 5. Information priority

**Type scale.**

| Token | Size and weight | Use |
|---|---|---|
| `t-title` | 18/24 semibold | titles |
| `t-section` | 14/20 semibold | section headings |
| `t-body` | 13/18 | labels, list items, values |
| `t-meta` | 12/16, monospaced for numbers | counts, IDs, ms, coordinates |
| `t-key` | 11/14, monospaced, O and 0 clearly distinct | key hints |

Nothing is smaller than 11 px. Uppercase is used only for section group
labels.

**Density.** Controls are 28 px in art workspace rails and bars, 32 px in
content inspectors, and 44 px touch targets whenever `pointer: coarse`,
whatever the width.

**Colour.**
- **Accent.** Cream means "selected or active" and nothing else.
- **Status colours.** Mint (ok), amber (warning), coral (error) and blue
  (info) are reserved for status. They always appear as a dot plus text.
- **Aggregate counts.** Totals such as "41 failing" are neutral; only changes
  are coloured (`+2` coral, `−3` mint).
- **Canvas surround.** The area around the canvas is a neutral mid-grey that
  doesn't depend on the theme. The sprite's backdrop sits inside it.

**Chrome budget.** The share of the viewport that the primary surface keeps:

| Workspace | 1600×1000 | 1280×800 | 1024×768 | Zen |
|---|---|---|---|---|
| Paint canvas | ≥ 75% | ≥ 70% | ≥ 65% | ≥ 92% |
| Animate canvas + preview | ≥ 60% | ≥ 55% (timeline ≤ 140 px) | ≥ 50% (96 px strip) | ≥ 85% |
| Review compare area | ≥ 75% | ≥ 70% | ≥ 70% | — |
| Behaviour / Activities inspector + consequence | ≥ 75% | ≥ 75% | list becomes a drawer | — |

The browser tests enforce these numbers.

## 6. Responsive behaviour

Layout classes come from **container queries on the workspace body**, not the
viewport, so opening the Test dock reflows the workspace as if the screen were
narrower. Pointer type, not width, decides density and gestures.

| Class | Body width | Shape |
|---|---|---|
| XL | ≥ 1400 | all panels docked |
| L | 1080–1399 | right panel 240 px; history and library become tabs inside it |
| M | 720–1079 | only the tool rail stays docked; right panel and lists become overlay drawers; timeline becomes a 96 px strip |
| S | < 720 | primary surface full-bleed; everything else in a bottom sheet with three heights: a 72 px peek, half and full |

Below 700 px tall, Animate drops to the 96 px strip, and the Review footer
merges into the breadcrumb row.

**Canvas gestures.**
- **Drawing and cancelling.** One finger or a pen draws. A second finger
  landing within 120 ms cancels the stroke, leaving no history entry.
- **Pan and zoom.** Two fingers pan and pinch-zoom; zoom snaps to whole
  scales when you let go.
- **Undo and redo.** A two-finger tap undoes and a three-finger tap redoes.
- **Colour pick.** A 450 ms long press opens a magnifier for picking a
  colour, and the current tool stays selected.
- **Pen detected.** Fingers stop drawing, and one finger pans, which serves
  as palm rejection.
- **Page gestures.** `touch-action: none` applies to the canvas only, never
  the whole page.
- **Device pixels.** The canvas follows `devicePixelRatio`, zooms in whole
  device pixels and never smooths.

**On a phone,** the studio is for browsing and review (verdicts and notes),
light touch-ups (pencil, eraser, picker, undo), playing a clip and nudging a
duration, single-number edits, and Test presets. Other actions show "Needs a
larger screen" with Copy link.

## 7. Controls kit

All controls live under `tools/jelli-art/ui/`, are exposed through
`JelliShell`, and work with keyboard, screen reader, touch and all four
themes.

**Shell and layout**
- `WorkspaceShell`: regions as landmarks, layout persistence, and a
  container-query class attribute.
- `ZenController`.
- `Panel`, `Drawer` and `BottomSheet`.
- `EdgeReveal` and `GrabHandle`.

**Navigation and commands**
- `Breadcrumb` with searchable segment pickers.
- `QuickOpen`, through `registerSubjects(kind, search, open)`.
- `CommandPalette`, `ShortcutRegistry` and `KeyHint`. Shortcuts are scoped
  per workspace and checked for conflicts in tests.

**Save state**
- `SaveStatePill`, `DirtyDot` and `ReturnChip`.

**Art tools**
- `ToolRail`, `ContextBar` and `HUDChip`.
- `CanvasViewport`: zoom and pan, telling strokes from gestures, palm
  rejection, the long-press magnifier and the neutral surround.
- `PaletteGrid`: ramp rows, main and secondary colours, and the contrast
  ring.
- `Timeline` and `Transport`.
- `CompareViewer`.
- `PreviewDock`: the round 466 px panel, with a stale badge.

**Content editing**
- `MasterList` and `Inspector`, with a `DisclosureSection` that shows a
  summary when closed.
- Fields: `NumberField` (units, range, drag to change the value, step keys,
  inline validation, a marker when changed), `TimeField`, `Select`,
  `Combobox`, `ChipSet`, `SpritePicker` and `ConditionChip`.
- `DataTable`, `NetBar` and `DayStrip`.

**Status and messages**
- `StatusChip` and `Banner`.
- `Toast`, `Dialog` and `Confirm`, the last replacing every native
  `confirm()` and `prompt()`.
- `EmptyState` and `LargerScreenNotice`.

## 8. Testing

- **Browser tests.** A new **Studio UI tests** CI job runs `pytest-playwright`
  with the browser version and fonts pinned through the official Playwright
  image. It starts the studio on scratch copies, the way `test_server.py`
  does.
- **Checks per workspace,** each at desktop (1440), tablet (820) and phone
  (390) widths:
  - one screenshot compared against a stored baseline (pixel art is
    deterministic with smoothing off)
  - one accessibility-tree snapshot (`to_match_aria_snapshot`)
  - an axe-core pass that fails on serious or critical issues, run in light
    and dark themes and with reduced motion
  - a keyboard round trip: every control reachable, Esc closes the top layer,
    ⌘K runs a command, and focus never lands in a hidden panel
- **Input and layout checks.**
  - Pen, touch and pinch tests on the canvas.
  - Chrome-budget assertions measured from the DOM.
- **Existing suites stay.**
  - The pure-logic node tests.
  - `test_server.py`.
  - The shortcut-conflict test from `ShortcutRegistry`.
- **Required check.** The job is added to the `main` ruleset once it is
  stable, which is its first PR plus a week of green runs.

## Migration strategy

Each step below is its own PR and ships on its own. Every PR keeps the old
paths working until the step that removes them.

| Step | PR | Contents |
|---|---|---|
| F0 | `docs` | This RFC; ADR for the foundation once approved |
| F1 | `ci(jelli-art)` | Studio UI tests job: Playwright fixture, baseline screenshots and aria snapshots of today's views, axe report (warn only); `tsc --checkJs` with a baseline |
| F2 | `refactor(jelli-art)` | `ui.css` with layers and tokens; move every `<style>` block into files; one theme definition; no visual change beyond token rounding |
| F3 | `refactor(jelli-art)` | Shell registries (workspaces, subjects, commands, shortcuts, dirty); remove global reassignment and sibling id lists; `html` helper; shared `esc`, fields, options |
| F4 | `feat(jelli-art)` | Controls kit part 1: toolbar and radio groups with roving tabindex, segmented control, toggle, chip, dialog/confirm/prompt, popover menu, status chip; replace `prompt()` and the second dialog system |
| F5 | `feat(jelli-art)` | App bar, breadcrumb, ⌘P, ⌘K, save pill, return chip; asset list moves into Review; deep-link redirects |
| F6 | `feat(jelli-art)` | Paint workspace: tool rail, HUD, context bars, right panel, container-query layout, canvas viewport (gestures, DPR), Focus level |
| F7 | `feat(jelli-art)` | Zen controller and auto-hide setting; chrome-budget tests become required |
| F8 | `feat(jelli-art)` | Animate workspace (timeline dock, flip-book, frame menus); Creature becomes the profile workspace |
| F9 | `feat(jelli-art)` | Review workspace and Test in game dock + presets |
| F10 | `feat(jelli-art)` | Content kit part 2 (inspector, disclosure, fields, data table) and Behaviour workspace |
| F11 | `feat(jelli-art)` | Activities workspace (after PR #22 lands) |
| F12 | `feat(jelli-art)` | Phone scope (bottom sheets, LargerScreenNotice), touch polish |

Steps F1–F4 change no layout and can start straight away. memo-043 PRs 7–10
(edit several frames at once, canvas-first layout, shortcut map and tool rail,
Activities and Test in game polish) are absorbed into F4–F11 and should not
be done separately.

# Drawbacks

- **Several weeks of front-end work,** which overlap with art and content
  changes and cause merge friction in `studio.js` and `compare.html`.
- **Accessibility depends on us.** In-house controls need their own
  accessibility discipline; the APG patterns and the axe and aria-snapshot
  tests are the guard.
- **No reactive framework,** so state flow needs discipline: one store, with
  updates through explicit subscriptions.
- **Habits change.** Moving keys (see the unresolved questions) breaks muscle
  memory.
- **Some browsers lose polish.** Anchor positioning is newly available; older
  Firefox and Safari get centred popovers.

# Alternatives

## Preact + htm, no build

A good component model at about 5 KB. But it means rewriting all rendering in
a React style, the stable 10.x line is about to change major version, and
signals bring a second copy of Preact unless imports are managed carefully.
It is a reasonable second choice if the in-house approach fails.

## Web Awesome (Shoelace's successor) or another component library

Web Awesome has about 70 MIT-licensed components plus a paid Pro tier, styled
through shadow-DOM parts and documented around a CDN. Vendoring it would add a
dependency tree and theming friction, mostly for things the platform now
provides. The other options are worse: Material Web has been in maintenance
mode since 2024, petite-vue is inactive, and Spectrum is heavy and built
React-first.

## A build step (Vite + TypeScript + Svelte or React)

This would give the best types and component model. But it adds Node to every
contributor's loop and to CI, in a C and Python repository with a small team.
It becomes worth it if the front end passes about 25,000 lines or gains
dedicated front-end staff.

## Patch the current layout without a foundation

The cheapest option, and it was memo-043's plan for PRs 8–10. But it would
spread the duplication and specificity problems that already make every
change to the studio expensive, and it gives no tests to stop regressions.

# Adoption strategy

1. **Approval.** The user approves or amends this RFC, and the foundation
   decision is recorded as an ADR.
2. **Delivery.** Each step goes through the existing pipeline: an authoring
   agent, an independent review, then auto-merge behind the required checks.
3. **Releases.** Studio releases are batched by wave: F1–F4, F5–F7, F8–F9,
   then F10–F12.
4. **Trial.** Before the idle auto-hide becomes the default, the artist uses
   Zen for a week, and the result is recorded in a memo.
5. **Docs.** `docs/artwork.md` and the studio README change in the same PR as
   each behaviour they describe.

# Unresolved questions

1. **Space key.** Should Space-drag pan the canvas as in other art tools, with
   the before-image peek moving to `` ` ``? Or should we keep today's keys and
   offer an Aseprite-style keymap as an option?
2. **Phone.** Is phone use real (for example review on the hosted studio), or
   should S ship as Review and Test only?
3. **Animate.** Should painting a clip frame always open Animate, or can an
   artist choose plain Paint?
4. **Review's audience.** Is Review for an artist checking their own work, or
   for a maintainer approving a range of commits? The second needs change sets
   by commit range as the primary surface.
5. **Auto-hide default.** Should "Only while drawing" become "On" after the
   trial?
6. **Test dock.** Is the dock enough for sizing a creature, or does the
   Creature workspace need the clip preview inline?

# Future possibilities

- Optional keymap presets (Aseprite, Pixelorama).
- Commit-range change sets in Review, with exportable notes.
- Vendored Lit for the timeline if hand-written patching becomes the
  bottleneck.
- Shared document state across browser tabs.

# References

- memo-043: Jelli Art Studio review and hit list.
- **Code inventory (2026-10-10):**
  - The architecture, every style block and token, the control catalogue, the
    layout, and how events are dispatched.
  - Pain points for responsive layout, Zen and shared controls, with file and
    line evidence.
- **External research (2026-10-10):**
  - How Aseprite, Pixelorama, Procreate and Procreate Dreams, Figma UI3,
    Photoshop, Krita, Blender and Rive handle focus.
  - Responsive patterns for canvas apps.
  - The 2026 status of front-end libraries and browser platform features.
  - Testing with Playwright, aria snapshots and axe.
- **Workspace design (2026-10-10):** the task inventory, the workspace model,
  rules for hiding chrome, the type and colour hierarchy, the responsive spec,
  wireframes for each workspace and the controls list.

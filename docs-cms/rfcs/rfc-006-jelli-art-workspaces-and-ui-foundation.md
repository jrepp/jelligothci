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

Rebuild the Jelli Art Studio front end around **task workspaces**:

- art workspaces: Paint, Animate, Review
- game-content workspaces: Creature, Behaviour, Activities
- Test in game

Each workspace shows only what its task needs, at three chrome levels (Full,
Focus, Zen), and adapts to the space it has. Underneath, keep the no-build
setup and add design tokens, CSS cascade layers, light-DOM custom elements, a
small in-house controls kit and browser tests. The work lands as small PRs that
each ship.

This is a draft proposal. Nothing in it is approved until the owner approves
it.

# Relationship to existing decisions

- **[ADR-011](../adr/adr-011-jelli-art-component-and-container.md)
  (Proposed): the studio as its own component and container.** This RFC keeps
  the studio a no-build Python-served app inside the existing image.
  - `tools/assets/compare.html` and `compare_slice.py` belong to the game
    component, so every step that edits them also bumps the game version. They
    stay there unless an amendment to ADR-011 moves them under
    `tools/jelli-art/`.
  - The `.dockerignore` allowlist keeps type-check configuration and any Node
    files out of the image.
- **[memo-021](../memos/memo-021-pixel-art-polish-and-review-tooling.md):
  pixel-art polish and review tooling.** It established the compare page,
  including the static page `compare_slice.py` writes, which must keep working
  without a server. It also made review-notes export the feedback path. Review
  keeps both.
- **[ADR-004](../adr/adr-004-repository-local-pinned-toolchains.md): pinned
  tools inside the repository.** Type-check and browser-test tools are pinned
  through `toolchain.env` and the repository scripts, not global installs. The
  Playwright image is pinned by digest, and workflow actions by SHA (AGENTS.md).
- **[ADR-012](../adr/adr-012-data-driven-creature-species-and-presentation.md)
  (Proposed) and [RFC-005](rfc-005-stimulus-driven-creature-behaviour.md).**
  Content stays data-driven. The Behaviour workspace presents RFC-005's
  states, repertoires and stimulus simulator. The simulator's odds over the
  next 600 behaviour seconds come from `reactions.js` and the studio README.
- **[memo-043](../memos/memo-043-jelli-art-studio-review-and-hit-list.md):
  studio review.** This RFC takes its hit-list items 4 and 10 and its layout and
  workspace items. It departs from memo-043 in two places:
  - It rejects the proposed sketch layer (see Non-goals).
  - The Test in game dock renders on save or on request, not while painting,
    because each render is a real engine build and render.

# Motivation

The owner asked for responsive layouts and "art task specific and game content
specific views with a more zen approach, auto hiding of toggles so that we have
clean information priority", in a codebase that can be supported in production.
Evidence from studio 0.6.0 on `main` (4eab889):

- **The canvas is small.** In Paint at fit zoom on a 32×32 creature, the canvas
  covers 26% of a 1600×1000 viewport, 8% at 1280×800 and 6% at 1024×768.
  Measured as canvas area over viewport area, dark theme, guide hidden.
- **Every mode carries every other mode's chrome.** Behaviour and Test in game
  still show the 290 px asset sidebar and the compare and lint header.
- **Content views are long, flat forms.** Each reaction in a repertoire is two
  rows of controls, and the Activities preview (on #22) scrolls away while you
  edit.
- **There is no design system.**
  - About 5,000 lines of front-end JavaScript and HTML, with styles in
    `<style>` blocks that are added in load order.
  - Colour is the only design token.
  - Font sizes take eight values from 9 to 20 px, and there are five unrelated
    width breakpoints.
  - The cascade relies on specificity hacks such as `:root button.primary`.
- **There are no shared controls.** Modules each define their own escaping,
  option lists and number fields. There are several tab and dialog
  implementations, plus `prompt()`.
- **Toggle groups are hard to use from the keyboard.** They are `aria-pressed`
  buttons without arrow-key navigation, so the Tool group alone is ten tab
  stops.
- **Layout depends on the window size.** Sizes come from
  `window.innerWidth/innerHeight` and fixed offsets at render time, so hiding
  chrome leaves canvases the wrong size.
- **Edits rebuild panels.** Most edits rebuild a panel with `innerHTML`, which
  loses focus in Behaviour and Reactions and risks injection wherever an
  escape is missed.
- **No tests cover layout, appearance or accessibility.**

# Goals

1. **Task-first layouts.** Each workspace gives its primary surface most of the
   screen; things needed only on demand get no permanent space.
2. **Zen without lost access.** Chrome recedes while you work. Keyboard,
   screen-reader and touch users never lose a control, and save state and
   errors never hide.
3. **Responsive layout.** Layout follows the space actually available, and touch
   and pen work on a tablet.
4. **One controls layer.** Shared tokens, controls, keyboard models and a single
   dirty-state contract.
5. **Sustainable code.** No build step, no runtime CDN, pinned tools, types
   checked in CI, and browser tests.
6. **No regressions.** These keep working:
   - palette-locked painting
   - drafts
   - stale-save (409) checks
   - the engine render
   - deep links
   - high contrast and reduced motion
   - the static compare page

# Non-goals

- Changing game data formats or the engine.
- Collaboration, accounts or a server rewrite.
- Layers (including memo-043's sketch layer), pressure brushes or vector
  tools.
- A new visual brand.

# Detailed design

## 1. Foundation

- **No build step.** A bundler would add `node_modules` and a dependency tree
  to every contributor's loop in a C and Python repository. Revisit only if the
  front end grows several times larger or gains dedicated front-end
  contributors.
- **CSS.**
  - One `ui.css` with `@layer reset, tokens, base, controls, workspaces,
    utilities`.
  - Tokens for colour, space, radius, type, density, z-index and motion.
  - Each theme defined once.
  - Module styles move out of JavaScript strings into files.
  - Container queries for layout.
- **Components.** Light-DOM custom elements per panel give each panel a
  lifecycle and boundary. Light DOM keeps tokens and test locators simple.
- **Rendering.** A small escaping `html` template helper patches named nodes
  instead of rebuilding panels, so focus and the text cursor survive edits.
- **Platform primitives.** `<dialog>`, `popover`, invoker commands and anchor
  positioning, with a centred fallback where anchoring is missing. Composite
  widgets follow the WAI-ARIA Authoring Practices patterns.
- **State and modules.**
  - One studio store. Each document keeps a single owner module that handles
    its saves and dirty tracking.
  - Views register through the shell instead of reassigning globals.
- **Types.**
  - JSDoc in plain `.js`, checked by `tsc --noEmit --checkJs` in CI only.
  - TypeScript is pinned in `toolchain.env` (ADR-004).
- **Escape hatch.** If hand-written patching becomes the bottleneck for the
  timeline or the Behaviour editor, a vendored rendering library may be used
  for them, after an ADR.

## 2. Shell

- **App bar.**
  - Workspace switcher, grouped Art | Content | Test.
  - Breadcrumb.
  - Save-state pill.
  - Command palette (⌘K).
  - Overflow menu. Compare-with is offered there in art workspaces only.
- **Breadcrumb.** The subject, as segments you can pick from, for example
  `Animate › Axolotl ▾ › idle ▾ › frame 3 ▾`. It replaces the asset sidebar
  outside Review.
- **⌘P quick-open.** Searches assets, forms, clips, states, repertoires and
  activities, and opens each in the workspace that owns it.
- **⌘K command palette.** Fed by one command registry, which also drives menus
  and key bindings. Every hidden function is reachable from it.
- **Save-state pill.** The single place for save state: Saved, N unsaved,
  Saving, Commit queued, Conflict, Offline.
  - It lists unsaved items by workspace. ⌘S saves the current workspace and ⇧⌘S
    saves everything.
  - Switching workspace never discards anything. Every document registers its
    unsaved state, including Activities (memo-043 item 6).
- **Return chip.** Shown after a jump between workspaces.
- **Deep links.** Old hashes redirect to the matching workspace, and draft keys
  migrate once.

## 3. Workspaces

| Workspace | Subject | Primary surface | Persistent | On demand |
|---|---|---|---|---|
| Paint | asset | canvas | tool rail with key hints; HUD (tool, colours, zoom); lint chip; palette, view and history panel | tool-option bar; selection bar; Tidy diff; before peek |
| Animate | form › clip › frame | canvas with onion skin + flip-book | timeline dock (frames, durations, playhead, transport) | slide/eye warnings on frames; frame menu; library drawer; editing several frames at once (memo-043) |
| Review | asset or change set | compare viewer | change list; compare-with; verdict and note footer | sheet view; physical size; review-notes export |
| Creature | form | round panel preview with limit boxes | profile scales; pose rules; idle beats | condition chips; shared-profile banner |
| Behaviour | state / repertoire | inspector | master list; consequence pane (look preview + RFC-005 simulator verdict) | full simulator table; odds over 600 behaviour seconds |
| Activities | activity | inspector in collapsible sections | list by kind; day strip of time windows; preview | per-pet bonuses; prerequisites |
| Test in game | scenario | rendered frames and accepted state | presets | full scenario form; build log |

- **Test in game as a dock.** Any workspace can open it as a dock (⌘J). It
  shows when its render is out of date and renders on save or on request.
- **Reactions as table rows.** Reactions become one table row each, with their
  conditions summarised in a chip.
- **Animate.** It merges today's Creature timeline with Paint and the
  flip-book. Plain Paint stays available for any sprite.

## 4. Chrome levels

| Level | Art workspaces | Content workspaces |
|---|---|---|
| Full | every panel docked | list, inspector, consequence pane |
| Focus (default) | contextual bars only when relevant | sections collapsed except the active one |
| Zen (`\`) | panels auto-hide; canvas fills | inspector only |

The content workspaces stay calm through progressive disclosure. They never
hide labels or lists.

**Zen rules.** The numbers below are initial targets, tuned in the trial.

- **During a stroke.** Overlay panels fade and let input through. Docked panels
  never move or resize the canvas.
- **Idle hiding.** Side panels hide after about 2.5 s of idle pointer on the
  canvas.
- **Never on focus.** Nothing hides while it has focus or an open popover.
- **Never hidden at all:**
  - the save pill and unsaved dots
  - errors and conflicts
  - the HUD
  - the playhead while playing
  - modals
  - focused elements
- **Bringing panels back:**
  - edge hover, on fine pointers only
  - `\`
  - F6 or Tab through the regions
  - visible grab handles on touch
- **Accessibility.** Auto-hidden panels stay in the tab order and the
  accessibility tree. Reduced motion makes changes instant. WCAG 2.2 success
  criteria 1.4.13 and 2.4.11 apply.
- **Default.** Auto-hide only while drawing. The idle timer is opt-in until the
  artist has tried it.

## 5. Information priority

- **Type.** A five-step type scale (title, section, body, meta, key hint) with
  a minimum of 11 px. Uppercase only for group labels.
- **Density.** 28 px controls in art rails, 32 px in inspectors, 44 px touch
  targets on coarse pointers.
- **Colour.**
  - The accent means "selected" only.
  - Status colours mean status only, always with text.
  - Totals are neutral; only changes are coloured.
- **The canvas surround** is a neutral grey that doesn't depend on the theme.
- **Chrome budget.** These are initial targets, tuned in the trial and
  measured by the browser tests:
  - the Paint canvas covers ≥ 70% of a 1280×800 viewport (≥ 90% in Zen)
  - the Review compare area ≥ 70%
  - the Behaviour and Activities inspector plus consequence pane ≥ 75%

## 6. Responsive behaviour

Layout classes come from container queries on the workspace body, so opening
the dock reflows the workspace. Pointer type, not width, sets density and
gestures. The breakpoints are initial targets:

| Class | Body width | Shape |
|---|---|---|
| XL | ≥ 1400 | all panels docked |
| L | 1080–1399 | narrower right panel; secondary panels become tabs |
| M | 720–1079 | tool rail docked; other panels become drawers; timeline strip |
| S | < 720 | primary surface full-bleed; the rest in a bottom sheet |

**Canvas gestures.**
- **Fingers and pen.** One finger or a pen draws. Two fingers pan and zoom, and
  a second finger cancels a stroke that has just started. When a pen is
  present, fingers never draw.
- **Picking a colour.** A long press picks a colour and keeps the tool.
- **Scope of touch handling.** Touch handling is set on the canvas only. The
  canvas follows `devicePixelRatio` and zooms in whole device pixels.

**On a phone,** the studio supports review, light touch-ups, playing clips and
Test presets. Other actions show a "needs a larger screen" notice with a
link.

## 7. Controls kit

The kit lives under `tools/jelli-art/ui/` and is exposed through
`JelliShell`, in these groups:

- **Shell and layout:** workspace shell, zen controller, panel, drawer, sheet.
- **Navigation and commands:** breadcrumb, quick-open, command palette,
  shortcut registry with a conflict test.
- **Save state:** save pill, unsaved dots, return chip.
- **Art surfaces:** tool rail, context bar, HUD, canvas viewport, palette grid,
  timeline, compare viewer, preview dock.
- **Content editing:** master list, inspector with collapsible sections, form
  fields, data table, day strip.
- **Feedback:** status chip, banner, toast, dialog and confirm (replacing
  `confirm()` and `prompt()`), empty state.

## 8. Testing

A **Studio UI tests** job, step F1:

- **Tool.** `pytest-playwright` in a Playwright image pinned by digest, against
  studios started on scratch copies.
- **Per view, at desktop, tablet and phone widths:**
  - a screenshot baseline
  - an aria snapshot
  - an axe-core pass that fails only on new serious or critical issues
  - a keyboard round trip
- **Measurements.** Gesture tests and chrome-budget measurements.
- **Existing suites** (studio, `test_server.py`, node) stay as they are.
- **Required check.** The job joins the `main` ruleset once it is stable.

# Migration strategy

Each step is one PR that ships on its own. Older paths keep working until the
step that replaces them.

| Step | Contents |
|---|---|
| F1 | Studio UI tests job, today's baselines, `tsc --checkJs` baseline |
| F2 | `ui.css` with layers and tokens; styles out of JavaScript; one theme definition |
| F3 | Shell registries (workspaces, subjects, commands, shortcuts, dirty); `html` helper; shared helpers |
| F4 | Controls kit 1: toolbar and radio groups, segmented, toggle, chip, dialog/confirm/prompt, menu, status chip |
| F5 | App bar, breadcrumb, ⌘P, ⌘K, save pill, return chip; asset list into Review; redirects |
| F6 | Paint workspace and canvas viewport |
| F7 | Zen controller; chrome-budget tests become required |
| F8 | Animate workspace, including editing several frames at once; Creature becomes the profile workspace |
| F9 | Review workspace; Test in game dock and presets |
| F10 | Controls kit 2 (inspector, fields, table) and the Behaviour workspace |
| F11 | Activities workspace, after #22 |
| F12 | Phone scope and touch polish |

F1–F4 change no layout. memo-043's open layout, shortcut and multi-frame items
are delivered by F4–F11.

# Drawbacks

- **Weeks of front-end work,** with merge friction in `studio.js` and
  `compare.html`. Each wave also bumps the game version (ADR-011).
- **Accessibility depends on us.** In-house controls need their own discipline,
  guarded by the axe and aria-snapshot tests.
- **No reactive framework,** so state flow needs discipline.
- **Habits change.** Moving keys breaks muscle memory.

# Alternatives

- **A vendored component framework, no build:** a full rewrite of rendering
  for a gain the platform now mostly provides.
- **A component library:** a vendored dependency tree and shadow-DOM theming
  friction for controls we can build on native primitives.
- **A build step (bundler and a typed framework):** the strongest types, but
  `node_modules` in every contributor's loop.
- **Patching the current layout:** the cheapest option, but it spreads the
  duplication and leaves no regression tests.

# Adoption strategy

1. **Approval.** The owner approves or amends this RFC, and the foundation is
   then recorded in an ADR.
2. **Releases.** Studio releases batch per wave: F1–F4, F5–F7, F8–F9,
   F10–F12.
3. **Trial.** The artist tries Zen for a week before idle auto-hide becomes the
   default, and the result goes in a memo.
4. **Docs.** `docs/artwork.md` and the studio README change in the PR that
   changes the behaviour they describe.

# Unresolved questions

1. **Space key.** Should Space-drag pan the canvas and the before peek move to
   `` ` ``, or should today's keys stay with an optional alternative keymap?
2. **Phone use.** Is it real, or should the S class ship Review and Test only?
3. **Clip frames.** Should painting a clip frame always open Animate?
4. **Review's audience.** Is Review for the artist, or for a maintainer
   reviewing a commit range? memo-021's notes export serves the second.
5. **Auto-hide default.** Should "only while drawing" become "on" after the
   trial?
6. **Sizing creatures.** Is the Test dock enough for it?

# References

- memo-043.
- The code inventory and measurements can be reproduced from `main` at
  4eab889.

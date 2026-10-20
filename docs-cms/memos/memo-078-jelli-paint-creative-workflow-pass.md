---
title: Jelli Paint creative workflow and duplication pass
author: Codex
created: 2026-10-20T06:21:39Z
tags: [jelli-art, memo, ux, validation]
id: memo-078
project_id: jelligotchi
doc_uuid: 8e805553-dca1-4b44-9d26-e820fb7d4df9
---

# Scope and delivered design

After [creation delivery](memo-077-jelli-art-creation-delivery.md), the user requested a professional creative UX pass, familiar Save operations and SVG icons, creature/frame/behavior cloning, and a more understandable Behaviors view. This increment reorganizes existing pixel-editing capabilities without introducing an unrelated layer/compositing engine.

[Paint](../../tools/jelli-art/studio.js) now presents a compact SVG tool rail, a Hand tool, tool-specific options, selection commands, stable history/save commands, and a Properties inspector. Large foreground/background swatches identify the active colors. The canvas uses a neutral mat and remains the primary working surface. Intermediate widths retain a canvas/inspector split; narrow windows use an asset drawer. [Shell navigation](../../tools/jelli-art/shell.js) exposes mode overflow controls and collapses the initial guide. Shared [SVG icons](../../tools/jelli-art/icons.js) are local, accessible through their accompanying labels, and need no external library.

Save remains an action with a stable label and familiar icon. An adjacent live status reports unsaved, saving, saved or failed state. Clips, creature size, behavior and activities use the same convention. Error messages retain actionable detail.

[Behaviours](../../tools/jelli-art/reactions.js) is organized around Behaviours, Reaction rules and Test a reaction. A When-to-Do explanation separates what the pet does from what triggers it. Appearance and timing come first; effects, requests, ending conditions and global rules use advanced disclosures. The simulator explains its unsaved working-document scope versus the saved real-engine preview.

# Duplication contracts

- **Creature:** [Duplicate creature](../../tools/jelli-art/creation.js) opens a prefilled creation flow using the selected form's saved frames, clips, scales and gameplay template. It proposes distinct artwork and display identities, checks unsaved source edits, and requires review of a separate single-form pet's unlock policy. It does not pretend to clone an entire multi-stage family.
- **Frame:** [Duplicate frame](../../tools/jelli-art/animation.js) is directly available in Paint's [flip-book](../../tools/jelli-art/flipbook.js) and in Creature's timeline/library. It allocates a new identity and keeps the source unchanged. If a clip is full the new frame remains in the library rather than exceeding the engine limit.
- **Behavior:** duplicate a state and its matching presentation with a new validated name, appended to preserve existing IDs. Existing triggers remain unchanged; the copy becomes active after the artist connects it in Reaction rules. The duplicate dialog revalidates current document state and capacity before editing.

# Adversarial findings and remedies

| Observation | Remedy |
| --- | --- |
| Painting while a save was pending could mark newer unsent pixels saved | Immutable submitted snapshot, per-asset pending guard, saved-base update, and retention of newer edits/drafts |
| Reload captured dirty buffers before network/image loading, losing later edits | Capture newest buffers immediately before publication; discard stale publications superseded by newer load/save epochs |
| Activities and clips had the same acknowledgement race | Acknowledge submitted documents only; retain later changes and current hashes; serialize shared creature-document writes |
| Paint shortcuts could affect hidden paint or fall through into unrelated Review actions | Route shortcuts to the active editor and suppress modifier fallthrough |
| Releasing Space outside the window could leave before-image peek stuck | Reset peek on blur and hidden document |
| Zoom/rebuild could detach an unfinished pointer stroke | Finalize the gesture before replacing the canvas binding |
| New swatches had an accessible-name/content mismatch | Match visible names and accessible labels; verify with axe |
| Narrow screens devoted most of the viewport to the asset browser | Drawer on narrow viewports independently of pointer type, smaller header, horizontal tool rail, and a visible-canvas regression |
| Asynchronous frame duplication could modify the newly selected clip instead of the source clip | Mutate the captured clip key, recheck capacity after the await, and avoid stealing focus after navigation |
| Dimming unchanged Review cards also reduced label contrast | Apply reduced opacity only to artwork, keeping labels fully opaque; pinned-renderer axe comparison found no new serious/critical violations and removed 12 existing violations |
| The pinned browser-test container lacked CMake for real authoring validation | Add the project's pinned CMake package to the hashed UI dependency lock and expose its scripts in the isolated runner PATH; retain real generator validation |
| A long comparison label expanded the inspector's implicit grid column and clipped Swap | Constrain the grid track and comparison controls to the inspector; viewport regressions check containment, functioning Swap and touch target size |
| Header tests still required the previous single-row intermediate layout | Luna updated the checks for the deliberately separate mode row, preserving tight height limits, clean/dirty stability, visible navigation and nonoverlapping controls |
| Tablet layouts lost too much canvas area | Place the quickbar beside the canvas in coarse-pointer landscape and stack the inspector below it in portrait; both original area and fit-zoom tests pass without reducing their requirements |

# Validation

Sol implementation and independent adversarial reviews covered the workspace, save transactions, cloning and runtime changes. Deferred-network browser tests exercise continued editing during saves and reloads, failed retries and late responses. Browser cloning tests compare copied pixels, verify new IDs and source preservation, and cover full clip capacity. Workspace tests cover desktop/phone layout, keyboard and Hand navigation, accessible names, mode overflow and asset-drawer focus restoration.

The first complete run against the pinned Linux AMD64 Playwright image passed **267/271** cases with updated visual baselines. The four remaining failures were the obsolete intermediate-header expectations and two tablet canvas-space regressions. All other screenshot, ARIA, axe, creation, duplication and deferred-transaction cases passed. Header checks subsequently passed at all three target sizes; the canvas checks retain their original area and fit-zoom requirements.

The corrected tablet layout measured a 758 × 356 canvas box in landscape, 27.9% of the viewport with 7× fit zoom, and 782 × 588 in portrait, 47.5% with 12× fit zoom. Landscape keeps the flipbook and complete palette visible without scrolling the main workspace. These are browser fixture measurements, not device-display measurements.

All nine focused geometry and palette cases passed, including the five original desktop/tablet canvas goals. Final baseline generation passed all 28 views in the pinned AMD64 image, with no new serious/critical axe violations. Desktop and phone Paint and desktop Behaviours screenshots were visually reviewed; the final desktop Paint screenshot confirms Swap is fully visible.

[PR #71](https://github.com/jrepp/jelligothci/pull/71) carries the implementation and final acceptance checks. Device builds are separate from these browser checks; no physical hardware UX claim follows from a host test or firmware build.

---
title: Jelli Art creation workflow review
author: Codex
created: 2026-10-20T05:37:30Z
tags: [jelli-art, memo, tooling, ux, validation]
id: memo-076
project_id: jelligotchi
doc_uuid: 77b218fc-2bd1-48c4-bb81-6fbd5f1f1ac7
---

# Overview

Jelli Art can paint existing sprites, create frames for existing forms, edit
activity recipes, and render saved content through the game engine. It does not
yet provide a complete browser workflow for creating pets, presents, or
backgrounds. This review records those boundaries and a reproducible activity
copying problem. Recommendations below are proposed increments, not approved
architecture or gameplay defaults.

The review used source inspection and actual Safari through the local
`~/d/safari-mcp` CLI, version 2.15.12. The browser served Jelli Art 0.11.0 at
`http://127.0.0.1:8879/`, using copied assets and content under the ignored
`build/jelli-art-review/` directory. Browser saves changed those copies, not the
production art or content.

# Workflow findings

| Workflow | Available today | Remaining boundary | Evidence |
| --- | --- | --- | --- |
| New pet | Existing form selection, clip editing, behavior and size controls | Initial frames/import specification, pet catalog entry, evolution set, and profile require repository authoring | [Artist instructions](../../docs/artwork.md#add-a-creature), [content save allowlist](../../tools/jelli-art/jelli_art.py), [frame creation](../../tools/jelli-art/animation.py) |
| New frame | Named blank or duplicated frame, stable ID allocation, insertion into a clip, timeline durations and ordering | Requires an existing form; new dimensions, pivot, palette, and form registration are not a creation flow | [Timeline controls](../../tools/jelli-art/animation.js), [frame planning and validation](../../tools/jelli-art/animation.py) |
| New activity | Add from selected recipe; eligibility, time window, costs, gains, prerequisites, locations, existing icon/prop, reusable motion | Custom pose, collectible prize, and daily reward policy are hidden; creation inherits selected recipe semantics | [Activities editor](../../tools/jelli-art/activities.js), [activity validation](../../tools/jelli-art/activities.py) |
| Present or prize | Paint existing prize sprites and select existing sprites as activity props | No generic asset creation endpoint or complete collectible authoring flow; prize progression also has engine contracts | [Server endpoints](../../tools/jelli-art/jelli_art.py), [prize definitions](../../include/jelli/prizes.h), [progression](../../core/prizes.c) |
| Background and location | Review existing backgrounds; choose existing locations in activities | Background painting is explicitly blocked; location and background catalog creation require repository edits | [Paint restriction](../../tools/jelli-art/studio.js), [server restriction](../../tools/jelli-art/jelli_art.py), [location catalog](../../content/locations.json) |
| Game preview | Real engine renders saved art/content with pet, form, behavior, moment, needs, screen, and time controls | No explicit location or present-state scenario control; activity thumbnail is an approximate separate preview | [Engine preview UI](../../tools/jelli-art/game_preview.js), [activity thumbnail](../../tools/jelli-art/activities.js) |

The API only permits content writes for `behaviors`, `creatures`, and
`activities`; catalog files such as `pets.json` and `locations.json` are not
editable through that endpoint. The frame API creates creature frames for an
existing form rather than arbitrary asset kinds. The manual new-creature
workflow is documented and validated, but it is not a simple browser creation
workflow.

# Safari observations

## Existing form and new frame

The Creature view offered four existing form buttons and no new-pet control.
Using **New frame name** and **Add blank frame** created `review-blank` in the
copied library. Inspection of the saved manifest confirmed asset ID 1025,
32×32 geometry, pivot `[16, 30]`, and insertion into `baby.idle`, which then had
two frames totaling 900 ms. These are observations from this scratch run, not
recommended defaults. The inherited geometry and stable allocation agree with
the [frame implementation](../../tools/jelli-art/animation.py).

The ignored screenshot artifact is
`build/jelli-art-review/creature.png`.

## Activity copy hides a daily policy

Selecting SNUGGLE, adding an activity, and saving persisted a recipe with
`daily_reward: true` but without `pose` or `prize`. The form did not expose
these fields. The [Add activity handler](../../tools/jelli-art/activities.js)
explicitly removes `pose` and `prize` from its copy and retains the other
properties, including `daily_reward`.

The [activity extras contract](../../cmake/JelliActivityCosts.cmake) defines
`daily_reward` as charging costs and awarding gains only on the first run of
the local day; later runs play without either. Thus the copied activity has a
material hidden policy. The Safari run verified the persisted fields, not the
runtime effect of completing the activity twice. That effect follows from the
source contract and still needs an engine scenario test if this behavior is
changed.

Some Safari click automation repeated actions, producing two copied recipes
and a stale-save modal, plus a rejected retry of the already-created frame.
This is not evidence of an application double-click or
duplicate-creation defect. Remaining checks used a single DOM click issued
through Safari evaluation.

The ignored screenshot artifact is
`build/jelli-art-review/activity-copy.png`.

## Background painting is blocked

Selecting `backgrounds.home` in Paint displayed:

> The font atlas and backgrounds are not paintable here yet; edit those PNGs in an image editor.

This agrees with both the [browser restriction](../../tools/jelli-art/studio.js)
and the [server rejection](../../tools/jelli-art/jelli_art.py).
Ignored artifacts are `build/jelli-art-review/background-locked.png` and
`build/jelli-art-review/background-snapshot.txt`.

## Present editing and real-engine preview

In Safari, selecting `prizes.teddy-bear` in Paint exposed the palette, pencil,
eraser, fill, shape, selection, history, and save controls for that existing
asset. There was no new-present or collectible-registration control. Source
inspection confirms the lack of a generic asset-creation endpoint.

The Test in game form exposed pet, form, behavior, moment, needs, screen, and
time controls, but no explicit location or present-state selector. Pressing
**Render in engine** successfully produced 12 frames from the copied art/content.
The page reported 1.8 seconds including a rebuild, and displayed engine state
for MINT, idle pose, no active moment, and a 09:00 clock. This establishes a
working real-engine preview; it does not test a new pet or collectible flow.
The screenshot is `build/jelli-art-review/game-preview.png`.

Source inspection shows that the separate activity thumbnail selects one pose sprite, draws the actor and
prop at fixed sizes, and applies a table of approximate motion offsets. It
does not render the selected location background or establish engine placement.
**Test in game** builds and renders the real engine from saved files and warns
about unsaved edits. Its scenario form has no explicit location or present-state
control. See [activity preview](../../tools/jelli-art/activities.js) and
[engine scenario form](../../tools/jelli-art/game_preview.js).

# Prioritized recommendations and acceptance checks

Priorities express impact on the requested authoring workflows. They do not
authorize implementation of all the changes below.

1. **P1: make activity-copy semantics visible and intentional.** Expose existing
   custom-pose, prize, and daily-policy fields, or reset omitted fields through
   a clearly described new-recipe template. Keep existing SNUGGLE behavior
   intact unless separately approved. Acceptance: copy both an ordinary recipe
   and SNUGGLE; every retained policy is visible, can be edited, survives reload,
   and passes the shared generator. Verify first and repeated daily completion
   in the engine rather than inferring behavior from a thumbnail.
2. **P1: provide a validated pet creation increment.** Collect the required
   catalog/form identity, artwork geometry, palette, portrait, clips, and size
   profile through a guided draft with actionable validation. Follow existing
   stable-ID and save contracts. Acceptance: create a distinct pet from the
   browser without editing JSON, paint its first frame, save/reload it, and
   render it in the engine. Invalid references and unavailable ID ranges must
   leave existing content intact. Evolution and unlock choices need explicit
   product decisions rather than silently invented defaults.
3. **P1: add game-aware asset creation and background authoring.** Introduce
   creation for supported icon/prop/present artwork kinds with palette,
   dimensions, identity, and placement metadata, and a separate background and
   location workflow. Acceptance: create an asset, paint it, use it in a recipe,
   save/reload, and verify engine rendering; create a background and location,
   select it in an activity, and verify the actual scene. Keep valid existing
   IDs stable and reject wrong-kind background references.
4. **P1 scope boundary: separate present artwork from collectible mechanics.**
   Editing or creating a PNG does not define prize progression, collection
   slots, save compatibility, or pet unlocks. The current contracts live in
   [prizes.h](../../include/jelli/prizes.h),
   [prizes.c](../../core/prizes.c), and
   [pets.json](../../content/pets.json). Acceptance for a future collectible
   increment must include award, collection, save/reload, and unlock behavior,
   with the required engine changes scoped explicitly.
5. **P2: preview the game context required by each workflow.** Retain the quick
   activity thumbnail but label it as approximate. Add real-engine scenarios
   for location, present state, and completion policies as their authoring
   controls become available. Acceptance: scenario changes produce the
   selected saved scene, display relevant engine state, and clearly distinguish
   saved content from unsaved drafts. Include keyboard and Safari interaction
   checks for creation, validation recovery, reload, and cross-editor use.

# Verification and limits

The focused host check
`./scripts/uv run --python 3.12 tools/jelli-art/test_creatures.py ActivityContentTest`
passed both tests in 2.567 s. It exercises real activity content saving, stale
rejection, and invalid recipes leaving files unchanged. It does not exercise
the browser Add activity handler or its hidden policy inheritance.

Safari evidence above concerns browser interaction and copied-file persistence.
The review-owned Safari session and local server were closed afterward. A Git
status check confirmed no changes under the production `assets/` or `content/`
trees. Screenshots and copied trial files remain under ignored build output.
No firmware build, deployment, physical display/touch, or iPad/Pencil behavior
was validated by this review. No production application fix was made. Follow-up
implementation should record the remedy and its browser/engine verification
here or in a linked delivery memo.

# References

- [RFC-006: Workspaces and UI foundation](../rfcs/rfc-006-jelli-art-workspaces-and-ui-foundation.md): proposed design goals and remaining draft scope.
- [RFC-007: Touch and pen](../rfcs/rfc-007-jelli-art-touch-and-pen.md): accepted input behavior and physical-device verification limits.
- [Memo-066: Touch and Pencil delivery](memo-066-jelli-art-touch-and-pencil-delivery.md): prior delivery and browser evidence.
- [Artwork workflow](../../docs/artwork.md) and [Jelli Art implementation guide](../../tools/jelli-art/README.md).

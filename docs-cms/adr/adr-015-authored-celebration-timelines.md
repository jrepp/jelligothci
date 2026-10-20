---
title: Authored celebration timelines and pet hardpoints
status: Proposed
created: 2026-10-20T20:00:00Z
deciders: Jacob Repp
tags: [animation, architecture, authoring]
id: adr-015
project_id: jelligotchi
doc_uuid: 2faf1ab4-674e-4a9d-a3fd-d6a277b8a021
---

# Context

The user requested an effects creation and orchestration workflow in Jelli Art,
a runtime consuming that content, coordinated creature animation, named render
layers, and authorable per-pet attachment points. Existing creature clips and
immediate particle bursts do not describe a reusable multi-track choreography.

# Decision

The implementation uses bounded declarative timelines compiled to immutable C
asset tables, with a separate allocation-free sampler. Sprite tracks describe
frame holds, placement, attachment, render layer, timing and endpoint motion;
creature cues select and restart existing pose clips. Sampling absolute elapsed
time handles seeking and skipped frames without an event queue or catch-up loop.
This record proposes retaining that contract; its detailed policy is not a
claim of owner approval of automatic gameplay triggers.

Hardpoints are named source-pixel offsets from a creature's ground anchor,
authored per artwork form. They are shared by all celebrations; pets sharing a
form share its hardpoints. The authoring document owns stable point identities.
Track offsets use display pixels so choreography can extend beyond the body.
Rendered layers have fixed insertion points around the actor and ordinary UI.

The runtime holds one explicit presentation sequence. Starting another replaces
it; unknown keys preserve existing playback. The animation clock supplies pause
semantics. End, cancellation, pet/form change, menu opening, and backward time
restore ordinary presentation. Effects do not mutate saved simulation state.

# Consequences

- The editor reuses the shell, guarded saves, drafts, touch number controls,
  creature clip/anchor drawing, and Paint. The engine preview compiles saved data.
- Fixed capacities and interval validation bound time and storage; no JSON,
  scripting language, heap allocation, or randomness runs in the frame loop.
- Creature cues cannot overlap; sprite tracks can. Layer then authored track
  order resolves compositing. Blank cue intervals resume normal creature poses.
- Active effects redraw the full surface to avoid stale overlays across restoration
  paths. Physical display cost needs measurement before automatic gameplay use.
- Forward playback is finite. General keyframe curves, audio, particle emitters,
  cue callbacks and care/reward bindings remain separate increments. A follow-up
  adds opt-in evolution/present-catch bindings and one bounded pending slot;
  shipped bindings stay disabled pending gameplay-default selection.

# References

- [Runtime contract](../../include/jelli/effect.h)
- [Pet playback contract](../../include/jelli/pet_effect.h)
- [Shared content validator/compiler](../../tools/assets/effect_data.py)
- [Implementation evidence](../memos/memo-085-celebration-authoring-and-runtime.md)
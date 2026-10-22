---
title: Jelli Art shared form layout and validation
author: Codex
created: 2026-10-22T04:56:08Z
tags: [jelli-art, memo, responsive, validation]
id: memo-102
project_id: jelligotchi
doc_uuid: a391b252-05d0-4e81-bb77-9904222753eb
---

# Overview

Persistent Jelli Art editors now use an opt-in form contract for field width,
labels, actions, focus, motion, and coarse-pointer sizing. The styles live in
[studio.js](../../tools/jelli-art/studio.js); New, Activities, Scenes, Effects,
Test in game, and Music opt in through their view markup. Paint tools, the
Effects timeline, and Music tracker cells retain their compact sizing.

# Findings and remedy

The initial browser check found that Activities number inputs remained at their
old height. [JelliDesign](../../tools/jelli-art/behaviour.js) wraps those inputs
in a `.jd-step` span, so direct-child field selectors did not reach them. The
shared rule now styles wrapped number inputs and lets the span fill its label.

Screenshot review then found that the Activities preview covered its pet picker
at phone width. The wider layout's `align-self:start` rule still applied after
the view switched to a column, shrinking the sticky aside to the preview's
intrinsic width. The [Activities layout](../../tools/jelli-art/activities.js)
now stretches the aside and gives the picker and canvas explicit grid columns.
The [form regression tests](../../tools/jelli-art/ui_tests/test_form_system.py)
assert a usable picker width and a gap before the preview, in addition to field
alignment and horizontal overflow.

# Verification and limits

The pinned browser run covering shared form cases, touch design, scene and effect
layout, Music layout, New creation, and view accessibility snapshots passed 94
cases before the final Activities preview correction. The final shared-form and
touch-design run passed 56 cases after that correction. The shared form cases
also cover desktop, tablet,
phone, 320 px stress width, and emulated iPad portrait and landscape. They check
44 px touch controls, visible focus, hover and reduced-motion behavior, plus
compact Paint, tracker, and timeline sizing. Source-defined touch targets and
container layout rules follow [RFC-007](../rfcs/rfc-007-jelli-art-touch-and-pen.md).

The Studio JavaScript type hook, container layout hook, and `git diff --check`
passed. These are browser and source checks; physical iPadOS Safari behavior and
CI screenshot comparison remain unverified for this branch.

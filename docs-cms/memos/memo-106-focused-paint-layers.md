---
id: memo-106
title: Focused Paint and authoring layers
author: Codex
created: 2026-10-23
tags: [artwork, authoring, validation]
project_id: jelligotchi
doc_uuid: 8c3d94e4-9c6e-4ef1-ad9f-890ca5a81898
---

# Increment

[ADR-016](../adr/adr-016-asset-focused-paint-layers.md) records the layer and
attachment separation. The [workspace](../../tools/jelli-art/paint_workspace.js)
adds contextual entry/return, an asset layer stack and progressive disclosure.
Attachment placement uses the existing effects metadata rather than baking pixels.

# Validation observations

Initial browser checks exposed excess phone toolbar height; canvas options moved
into a disclosure and duplicate asset headings were removed. An initial DOM move
selected the entire toolbar instead of its control group; narrowing the selector
restored Save and tool options. Existing transaction tests now inspect the full
layer draft rather than the former flat byte string.

A temporary experimental request-size increase failed the existing oversized-body
regression; the original limit was restored. The layer document has its own lower
validated bound. During verification the host briefly refused new processes with
`Resource temporarily unavailable`; interrupted checks must be rerun before their
results count. The intermittent event-handler failure came from a canvas refit detaching the
shared backdrop/overlay controls before boot wiring finished. Stage rebuilds now
restore and remount those controls explicitly.

The macOS audit traced process spikes to a separate recursive Go-test workload:
its environment sanitizer removed the child-mode markers, making children spawn
more children. The audit changed no files and terminated no jobs. The workload
subsequently exited and process counts returned to normal.

The server suite passed its save/validation cases, including layered PNG and
metadata-only stale conflicts. Creation integration passed, and a focused storage
check covers hidden-layer palette replacement and resized clone metadata. The
image built successfully; its real entrypoint, health endpoint, HTML bundle and
asset data API passed a smoke check against a read-only checkout. The local Podman
VM had first needed starting. Browser and final PR results are recorded below as
they complete.
# Browser evidence before publication

The initial broad pinned-browser run passed 295 cases. Its eleven failures were
fixed by updating tests for disclosed history/help and the focused asset header,
repairing asset-drawer focus selection, and matching the colour-picker accessible
name to visible text. Targeted reruns passed (including a corrected test import).
The new desktop/phone layer, draft and attachment workflows passed; the pinned
view/accessibility refresh passed all its cases and removed the former custom
colour-label violations. Linux/amd64 screenshot references remain to be reviewed
from CI; local ARM screenshots are evidence, not substituted reference pixels.

# PR remediation, 2026-11-12

The full-clip duplication browser case exposed a startup race: data became
available before boot selected a valid asset, so clicking Paint could dereference
an absent asset in its new navigation. The workspace now defers that navigation
until boot has selected the asset. The fixture retains browser exception stacks
to make any future failure actionable.

The game phone accessibility reference captured transient Loading text. The view
fixture now waits for the pet selector after the asynchronous catalogue loads;
the reviewed reference matches the settled desktop/tablet form.

Visual review also found overlapping flip-book controls in the narrower tablet
canvas column. A container query on the dock arranges its controls and frame
strip according to their available width. Screenshot references must come from
the fresh Linux/amd64 CI artifact after those layout checks complete.

Verification: the frame-copy regressions passed both full and non-full cases;
the settled game view passed all four focused view/accessibility cases. Both
contextual layer cases and all five existing canvas/dock geometry cases passed.
The two new narrow-dock cases assert disjoint transport/guide rectangles and a
usable frame strip at phone and tablet widths. Documentation validation and
repair passed without repairs. The interrupted broad local run is not counted
as a suite pass; current-head CI and its screenshot review remain required.

The clean broad rerun passed its first 16 cases, then exposed the same premature
readiness assumption in the creation fixture: the numeric guard test entered a
field while boot could still replace its editor. The exact case passed on main.
Waiting for the existing `body[data-ready=true]` contract before starting editor
interactions made all four numeric validation cases pass with their original
guard assertions. The explicit reload in the full-clip case continues to test
early Paint entry before that readiness boundary.

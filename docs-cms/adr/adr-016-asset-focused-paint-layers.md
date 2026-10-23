---
title: Asset-focused Paint layers and attachment references
status: Proposed
created: 2026-10-23T12:00:00Z
deciders: Jacob Repp
tags: [architecture, artwork, authoring]
id: adr-016
project_id: jelligotchi
doc_uuid: 9d5b36fb-190a-47e1-9875-54cd000f7609
---

# Context

The owner requested contextual Paint entry, a quieter asset-focused workspace,
and Photoshop-style layers for painting, onion skinning and attachment placement.
The owner explicitly selected separate attachment metadata rather than flattening
attachments into artwork. This extends the earlier layer exclusion in
[RFC-006](../rfcs/rfc-006-jelli-art-workspaces-and-ui-foundation.md).

# Decision

The implementation keeps binary-alpha paint layers in a compressed `jelli.paint`
iTXt field inside the PNG. Ordinary PNG readers and the game still receive the
flattened visible artwork. This record proposes retaining that storage contract;
it does not claim owner approval of the specific serialization choice.

[The layer validator](../../tools/jelli-art/paint_layers.py) owns version,
size/count limits, names, visibility/lock flags, dimensions, and agreement between
the visible composite and submitted PNG pixels. A metadata-only edit changes the
same PNG hash used for stale-save detection. Older clients cannot silently flatten
a layered PNG through the save API. [Storage](../../tools/jelli-art/storage.py)
preserves this field during studio writes; palette replacement includes hidden
pixels, and creature cloning resizes every layer with nearest-neighbour sampling.

[The browser model](../../tools/jelli-art/paint_layers.js) keeps independent pixel
buffers. Painting targets the selected visible, unlocked layer. Undo, redo and
browser drafts retain the full layer document. Save acknowledges the submitted
snapshot, preserving edits made while the request is in flight. Legacy flat drafts
remain readable. Selecting a layer alone does not make an asset dirty.

Onion skins and attachment artwork are reference overlays, excluded from PNG
pixels and paint history. Attachment placement edits the existing per-form
hardpoints in `content/effects.json`, with that editor's own drafts, history and
stale-save checks. Its separate save action identifies the effects document it
writes. Coordinates retain the source-pixel, ground-relative contract in
[ADR-015](adr-015-authored-celebration-timelines.md).

# Consequences

Layers travel with saved PNG duplication without a sidecar path or new runtime
resource. External tools that strip PNG metadata also strip editable layers;
ordinary pixel import starts a single artwork layer. Reference opacity is a view
setting; paint opacity and blending modes are outside this increment, preserving
binary-alpha artwork. Runtime memory and attachment playback contracts are unchanged.
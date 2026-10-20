---
title: Scene authoring affordance and editing recovery pass
author: Codex
created: 2026-10-20
tags: [authoring, memo, scenes, validation]
id: memo-083
project_id: jelligotchi
doc_uuid: 69d3709a-fbdf-4115-b81c-bebdee5fae24
---

# Findings and changes

The owner requested another editing/authoring affordance pass. Review of
[Scenes](../../tools/jelli-art/scenes.js) found the main layer actions buried below
advanced properties, no scene edit history or layer duplication, stale saved-scene
testing during edits, and a pet reference drawn below the actual walking surface.

The layer list now includes thumbnails and updates names/visibility immediately.
Scene settings sit separately below the library; the selected-layer inspector starts
with naming and duplicate/reorder/remove actions. Advanced variation folds away, and
the desktop preview remains beside the inspector where viewport height permits.
Mobile and short viewports retain normal scrolling. Duplicate layer preserves its
settings and seed so it initially matches the source; a different seed can be chosen
explicitly. Solo-layer preview leaves authored visibility and dirty state unchanged.

Bounded document undo/redo includes structural edits and retains selection context.
History is local to the tab and resets when saved content is reloaded. Saving updates
the disk baseline without dropping edit history. Existing immutable save snapshots
and draft recovery remain in use. Live numeric edits update the document as valid
input arrives; invalid fields block saving and receive focus rather than silently
saving their previous value. Scene/layer labels also reject blank names.

The engine action becomes “Save and test in game” for changed/new scenes. It enters
the engine view only after the saved snapshot matches the current document. Edits
made during a pending save remain unsaved in the authoring view. The shared keyboard
router skips form fields, so Scenes now handles Cmd/Ctrl+S at its own root to support
saving from a focused input. Document undo shortcuts leave native text-field undo
alone and appear in the shortcut guide.

The pet reference now uses opaque ground contact and the home reference scale and
position instead of the bottom of a padded image canvas. This aligns it with the
sidewalk for placement review; the real engine preview remains the rendering check.

# Validation

TypeScript passed without new baseline exceptions; all eight schema/compiler tests
passed. The final focused browser scene run passed twelve cases. A separate keyboard
and accessibility case passed, including save from a focused numeric field, document
undo/redo and no serious/critical WCAG A/AA violations in Scenes. The duplicate-layer
regression was rerun after preserving its seed. Cases cover removal recovery, solo
preview isolation, invalid input, save-and-test publication, continued edits during
save, drafts, stale-write cancellation, persistence and desktop/phone geometry.
Desktop and phone screenshots were visually inspected. No screenshot, accessibility
or type-check baseline was changed in this pass.

`make docs-check` and `make docs-fix` passed with no repairs or unresolved findings. No C, asset or firmware
behavior changed, and no device build or physical testing was performed.

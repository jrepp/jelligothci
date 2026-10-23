---
id: memo-105
title: Behavior editor numeric-input polish
author: Codex
created: 2026-10-22
tags: [authoring, behaviors, validation]
project_id: jelligotchi
doc_uuid: 38c08d79-9f43-4cc6-aea9-d4659da6d922
---

# Findings and remedy

The polish review found inconsistent treatment of unfinished numeric input:
activity recipes converted blank values to zero and rebuilt their controls;
behavior fields could put non-finite values into the working document; creature
simulation accepted a pet ID beyond its displayed maximum.

The shared [editor design helper](../../tools/jelli-art/behaviour.js) now guards
invalid changes and navigation, reports the affected field, and keeps its input
available for correction. Revert/reload and mobile number steppers retain their
recovery paths. Required recipe and behavior values use native constraints;
optional inheritance/condition fields remain optional. Save entry points also check
fields, including saves invoked outside button clicks. The simulator honors its
existing pet-ID bounds. The [guide](../../docs/artwork.md#behaviour-and-reactions)
describes the authoring behavior.

# Validation

Focused browser tests cover blank required values, unchanged on-disk data, retained
state/recipe selection, stepper recovery, optional bonus inheritance and simulator
bounds. Existing creation, draft/recovery, save-isolation and view/accessibility
checks are included in validation. Type checking retains the existing baseline.
Runtime behavior selection, authored catalogs and firmware are unchanged.

# Integration observation

The local SSH signing agent returned an error while committing the integration
merge. The incomplete local merge was aborted; GitHub's branch-merge API created
commit `0ceb707b6ad9156c22144554ca6e974535651e71`, reported verified with reason
`valid`, and the checkout fast-forwarded to it. Signing settings were not changed.
The local agent's availability remains unverified; the combined branch has its own
CI run before landing.

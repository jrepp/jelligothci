---
title: Pet roster navigation and validation
author: Codex
created: 2026-11-15T22:49:28Z
tags: [gameplay, input, validation]
id: memo-120
project_id: jelligotchi
doc_uuid: 253dd309-0495-4e36-9474-4b4bd39bcaa9
---

# Scope

The pet roster provides a direct Settings entry, paged pet cards, form progress,
state labels, and staggered idle movement. Its presentation and hit testing use
[the collection UI](../../core/pet_collection.c), while the
[capacity test](../../tests/test_pet_collection_capacity.c) exercises the full
collection and navigation to the last entry. State labels are a presentation
of existing sleep, health and mood values, rather than new persistent stats.

# Validation and worktree setup

The first firmware build in this isolated worktree stopped because its local
SDK path was missing: formatting had already created a separate `.tools`
directory, so linking the directory itself did not expose the SDK. Linking the
pinned SDK and tool directories inside that worktree repaired discovery. Build,
host tests and physical display/touch results are separate evidence; no physical
roster verification has been performed.

The first size check also reported that the shared render-key function exceeded
its source-size limit. Moving roster timing into the collection key restored the
limit; the dedicated size check then passed. The remaining verification is
rerun against the final revision.

The final targeted roster test initially fabricated sleep without its required
nap deadline, making the game invalid and preventing rendering. The fixture now
sets a valid nap deadline and maps each pet through its collection identity.

The first Settings layout replaced Rest with Pets. Host debug tests exposed the
regression; Pets now has its own control and the existing Rest path is retained.

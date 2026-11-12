---
id: memo-111
title: Recover local format 20 checkpoints
author: Codex
created: 2026-11-12T10:14:02Z
tags: [firmware, save, validation]
project_id: jelligotchi
doc_uuid: 16822748-692a-4e7d-a1ed-13fd2572dd8c
---

# Recover local format 20 checkpoints

After firmware from main revision `f17b7df` was installed, startup refused an
existing checkpoint written by a local development build. Main supported save
version 19; the local build wrote version 20. Storage was preserved while the
record was investigated.

The local version 20 writer used the same serialized fields as version 19, but
its unlimited-food gameplay permitted an ordinary meal in progress with zero
inventory. The [save reader](../../core/save.c) accepts version 20 and reserves
enough food, when required by the current engine, to finish ordinary meals
already in progress before validating the
game. Earlier versions keep their existing validation. Version 20 values that
remain meaningful to the current game are preserved. The
[save tests](../../tests/test_save_v20.c) exercise the compatibility case, version
19, and rejection of unknown versions and damaged records.

The extracted game-only checkpoint was 4,436 bytes and had valid storage chunk
checksums, save checksum and commit marker. Its SHA-256 was
`d112fecc7b4cb2e4fb1e24756cd8eb734e5d411e2fb6ffd30aabaf0925d6d436`.
The patched host decoder loaded nine pets, with active pet ID 5 and form 2,
matching the observed state before the firmware change. The checkpoint and raw
storage backup remain local; neither is included in the repository.

This host decode does not establish successful firmware startup or physical
display and touch behavior. Those remain to be verified after deployment.

During desktop validation, the unrelated Jelli Art server test timed out while
waiting for a local HTTP response in its push test under concurrent host load.
The same timeout was observed on the base revision. The broad local suites and
an isolated rerun were stopped; current pull request checks provide separate
validation in CI. The cause of the local timeout remains uncertain.

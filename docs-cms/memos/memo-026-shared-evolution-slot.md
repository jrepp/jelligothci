---
id: memo-026
title: Shared evolution slot and reversible forms
author: Codex
created: 2026-10-09
tags: [collection, evolution, saves]
project_id: jelligotchi
doc_uuid: 710334b5-f6e4-4e5f-b01b-6ab121220a79
---

# Behavior and migration

Mint grows into Lilac in the same authored Jelli collection slot. New games
start with one pet. The second prototype slot is now Friend, unlocked by the
Friendship Bow. All nine slots and their unlock rules remain authored data.

An unlocked form can be selected on an active or stored pet, including while
asleep or busy. Switching only changes its form; care state, identity, progress,
and reached forms are retained. Automatic evolution only unlocks Lilac once,
so selecting Mint later does not immediately revert it to Lilac. The collection
grid labels families and forms separately from ownership badges.

Save v8 retains the v7 byte layout but adds semantic migration. Versions 1–7
merge the two duplicate starter records: retain the active starter, or the
first starter if another family is active. Union reached forms, infer Mint
ancestry for a grown Lilac, remap collectible origin/offering IDs and the sleep
journal, compact storage, and preserve other families. The inactive duplicate's
separate care state is discarded; its unlocked forms and present ownership are
preserved. Missing families can unlock from existing discoveries afterward.
Migration validates before and after changing the candidate save. The full
nine-pet save remains 3569 bytes, the game 3992 bytes, and each pet 392 bytes.

# Validation

Tests cover fresh single-family initialization, unlocks, nine-pet capacity,
legacy save consolidation with the second starter active/asleep, present
provenance, sleep linkage, locked-form rejection, reversible forms without
repeat growth, persistence, and corrupt-save rejection. Existing tests that
require two owned families now use an explicit two-family fixture rather than
assuming every new game has two starters. `make test` passed 41 tests, core-only passed 20, and sanitizers passed 41.
Additional collection, gallery, and debug-client regressions passed after the
final UI tests. The firmware build and C checks passed. Grid and evolution
screenshots were inspected. Device save migration remains pending until the
board can be flashed and queried.

# References

- [Authored catalog](../../content/pets.json)
- [Migration](../../core/collection_migrate.c)
- [Collection tests](../../tests/test_collection.c)
- [Playing guide](../../docs/playing.md)

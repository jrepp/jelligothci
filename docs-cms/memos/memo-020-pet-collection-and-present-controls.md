---
id: memo-020
title: Pet collection and present controls
author: Codex
created: 2026-10-09
tags: [collection, content, evolution, persistence, validation]
project_id: jelligotchi
doc_uuid: bac31519-e6f1-481d-b059-821b37ec5a26
---

# Scope

The user requested implementation and eventual device flashing of the collection
and present controls in [RFC-004](../rfcs/rfc-004-pet-collection-and-evolution-data.md).
Settings now opens a nine-slot Pets grid, with locked details, explicit activation,
evolution viewing, and NEW badges. Present selection opens a large Give / Put away
panel. Back preserves selection; Put away keeps inventory and works when Give is
blocked. A panel-opening tap cannot also give because the targets are separate.

# Content and persistence

[The catalog](../../content/pets.json) is authored JSON, validated and compiled by
CMake on both hosts. Two existing starters remain; seven testing companions unlock
from present discovery. All use the current two-form Mint/Lilac artwork and growth
rule. New artwork, branching forms, and general predicate expressions remain
future content/engine work. The compiler rejects unsupported form sets. This is
a concrete bounded subset of the broader draft RFC, not its generic pack loader.

Collection entry IDs differ from saved instance IDs. Evolution retains identity,
slot, and provenance. Catching a qualifying present creates one stored companion
and sets its NEW bit in the same checkpoint as the catch. Ownership itself latches
the grant. Stored pets do not advance. Discovery remains after gifting.

Save v4 appends entry bindings, reached forms, and NEW bits. v1–v3 records retain
all existing IDs, state, active selection, and gift origins. Legacy records map
in storage order to slots; only the current form is inferred. Existing discoveries
can grant missing entries during migration. A malformed/newer save is preserved.

Measured desktop bounds: a nine-pet checkpoint is 3523 bytes within the unchanged
4096-byte buffer; game state is 3920 bytes, each pet 384 bytes, and the pet engine
11352 bytes. Fixed-workspace assertions remain intact. Both full RGB565 buffers
remain unchanged. No dynamic allocation was added to the core.

# Validation and corrections

Host tests cover once-only grants, arbitrary legacy IDs, all nine records,
stored-clock freezing, evolution history, provenance, legacy migration, duplicate
bindings, invalid NEW bits, and canonical save round trips. UI tests cover the
collection hierarchy, blocked activation during linked sleep, Put away while
asleep, Back retaining selection, and cross-pet giving through the explicit panel.
The debug CLI recognizes all added pages. Catalog tests reject missing capacity,
bad references, invalid labels, and unsupported evolution graphs.

Earlier validation caught tests that assumed direct two-pet swapping or direct
Give, plus future-version fixtures that now referred to the current version.
Those expectations were updated. Static analysis caught an absent held-index
bound and unchecked drawing helpers; bounds and return checks fixed them. The
menu availability function exceeded the complexity limit, so ordinary menu
checks were split from collection routing. No compiler, analysis, or size limits
were relaxed.

Final checks passed: `make test` and `make sanitize` (38 tests each),
`make core-test` (17 tests), `make lint-c`, `make esp-build`, and documentation
check/repair. The sanitizer session test completed successfully despite an
unusually long host wall-clock duration; no timeout or assertion was relaxed.
SDL snapshots of the grid, locked detail, evolution view, and present panel were
generated; the grid/detail/panel were visually inspected for round-screen fit.

# Device preparation

The connected ESP32-S3 was discovered at `/dev/cu.usbmodem2101`, USB serial
`90:70:69:FE:21:DC`. Pre-deployment state was captured in ignored build output.
The full 24576-byte NVS partition was backed up from offset `0x9000` before any
flash. The existing active pet was ID 1, form 1, asleep. Raw device state and NVS
remain local under ignored `build/collection-*` files. No credentials were read
into the transcript or configured.

Flashing is deferred until the user's added food, hydration, and exercise work
is complete. Firmware compilation does not establish physical display/touch or
network operation. The final deployment will record its own evidence.

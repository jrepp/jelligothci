---
title: Bounded memory foundations and staged engine adoption
status: Draft
author: Codex
created: 2026-10-19
tags: [engine, memory, planning, portability, testing]
id: rfc-009
project_id: jelligotchi
doc_uuid: bda23a1d-1ce4-4273-a169-f571ddc7f1b7
---

# Summary

Build and test the underlying bounded-memory contracts before introducing
compressed-resource residency or replacing existing ownership paths. Reuse the
same portable implementation in desktop and device adapters. This plan proposes
small foundations with demonstrated consumers, then adoption in dependency order.
It does not implement an allocator, approve capacities, or authorize every stage.

[RFC-008](rfc-008-jellipak-resource-store.md) owns the proposed pack, residency,
and worker design. This RFC supplies its prerequisite memory trajectory and
explicit acceptance gates. [Memo-070](../memos/memo-070-compressed-resource-payload-measurements.md)
records compression evidence; payload savings alone cannot select RAM budgets.
The source allocation audit and strategy review are recorded in
[memo-071](../memos/memo-071-allocation-audit-and-memory-trajectories.md).

# Motivation and current boundary

[ADR-001](../adr/adr-001-portable-c-core-and-host-adapters.md) and
[ADR-007](../adr/adr-007-bounded-c-and-enforced-quality-checks.md) require a shared,
allocation-free portable core with host-owned storage. The
[engine contract](../../include/jelli/engine.h) and
[development guide](../../docs/development.md#boundary-between-engine-and-host)
remain authoritative. Existing fixed arrays and caller-owned workspaces are
useful precedents, not migration debt that automatically needs an allocator.

The host audit distinguishes unlike lifetimes:

| Existing owner | Recommendation |
| --- | --- |
| [ESP32 display output](../../ports/esp32/main/display_output.c) and [transfer](../../ports/esp32/main/display_transfer.c) | Retain checked startup allocations with the required PSRAM, internal RAM, DMA and alignment capabilities; never move graphics buffers onto task stacks |
| [SDL reload banks](../../ports/sdl/asset_reload.h) and [decoder](../../ports/sdl/asset_pack.c) | Replace whole-pack expansion only after common residency passes rollback and device-capacity tests |
| [ESP32 sound](../../ports/esp32/main/sound_output.c) | Retain suitable existing static state and buffers; introduce no allocator merely for consistency |
| [Network worker](../../ports/esp32/main/network_worker.c) and [debug USB](../../ports/esp32/main/debug_usb.c) | Consider caller-owned/static small queue storage only after measuring internal-RAM cost and checking SDK lifetime requirements |
| [Save session](../../ports/esp32/main/session.c) | Preserve bounded workspace ownership; reuse checked span arithmetic when relevant, without coupling save lifetime to resource arenas |

Current capacities belong to these sources. Initial review should record their
actual startup, steady-state and transition costs in a dated memo, rather than
copying a changing allocation catalog here. Removing a bounded startup heap call
is not itself evidence of lower peak memory or better behavior.

# Foundation contracts

The names below describe proposed responsibilities, not implemented symbols.
Add only what the next real consumer needs. Public contracts belong in proposed
`include/jelli/memory.h`; implementation belongs in focused `core/` modules.
Keep host capability flags and synchronization primitives out of that interface.

## Caller-owned spans and checked layout

Represent a byte span with a pointer and byte capacity. The owner retains storage;
views never free it. Define validity for an empty span explicitly: a null pointer
is acceptable only with zero capacity, and empty operations perform no pointer
arithmetic on it. Document overlap and source/destination lifetime restrictions.

Provide checked addition, multiplication and alignment/layout operations needed
by pack tables, planes and reservation sizing. Reject zero or unsupported
alignment, including alignments the underlying allocation cannot satisfy. Check
both padding and final size before forming pointers; establish the offset bound
before subtracting remaining capacity. Never assume pointer width or desktop
alignment on ESP32. Return a distinct invalid-input, overflow or exhausted status.
On failure, leave outputs and allocator state unchanged unless an individual API
explicitly documents a different cleanup contract.

## Monotonic transient arena

Use an arena only for phase-local metadata or scratch whose lifetime ends
together. Its storage and alignment come from the caller. Marks are tied to the
arena identity and epoch; an old mark or a mark beyond the current cursor is
invalid. Reset/rewind is allowed only when no live view, lease or worker can
reference the reclaimed range. Reset does not free backing storage.

The owning thread controls allocation and reset. Passing exclusive scratch to a
worker suspends owner access until acknowledgement. An arena is not a concurrent
allocator and cannot retire arbitrary resident images. Track used and high-water
bytes without wrapping counters. Failed allocation must preserve the cursor.

## Resident metadata and payload storage

Use fixed metadata slots with index/generation handles for independently retiring
resources. Resolve handles only after validating index, occupancy and generation.
Reject stale handles and double release. Define generation exhaustion: retire a
slot at the generation limit or require a quiescent reset that invalidates all
external handles; never silently wrap and accept an old handle again.

Choose the simplest payload layout that passes measured transition budgets:

- Two scene arenas are acceptable if the full old/new peak, duplicated shared
  resources, padding and worker scratch fit. Commit swaps whole generations;
  the old arena resets only after every renderer/worker user retires.
- Otherwise use a fixed contiguous-page payload pool with bounded metadata.
  Reserve all required runs transactionally before decode; rollback restores
  every reservation if any run or metadata slot is unavailable. A resource view
  requires contiguous storage unless the renderer explicitly supports segments.

A page pool can have sufficient total free bytes and still lack a sufficiently
large contiguous run. Report both total free and largest run, account for page
rounding, and test fragmentation. Pins prevent reclamation while a view/job uses
a resource. Distinguish reserved, loading, ready, committed and retired ownership;
only the engine thread mutates residency bookkeeping. Avoid implicit compaction,
which would invalidate pointers. No general-purpose heap is proposed.

## Dedicated worker scratch and host bootstrap

Give the worker its own bounded decode/read scratch, supplied at startup. Scratch
must include buffers that coexist, codec intermediates and alignment padding;
resident RGB565 output is separate. Do not put block-sized temporary arrays on
the render or worker stack. Worker stack sizing is an independently measured
host responsibility.

Hosts allocate capability-specific backing storage once, check each result and
unwind partial initialization in reverse ownership order. Portable code receives
only validated spans/capacities. Hosts publish a job through synchronization that
makes descriptors visible, and publish completion only after all output writes
are visible. The worker alone writes reserved destinations; render reads only
committed views. `volatile` is insufficient. Cancellation does not return storage
until the worker acknowledges it; shutdown joins before freeing scratch/sources.

# Test matrix before adoption

All tests below are implementation requirements, not claims of tests already run.
Use small capacities that expose exhaustion and exact boundaries, then repeat
integration cases with the selected device capacities on native hosts.

| Foundation | Required adversarial cases and invariant |
| --- | --- |
| Span/layout | Null/zero combinations, exact fit, offset at/end/beyond capacity, multiplication/addition overflow, zero/non-power-of-two/unsupported alignment, misaligned base, padding overflow, overlap policy; failure leaves outputs unchanged |
| Arena | Empty/full/exact fit, alignment wastage, failed allocation cursor stability, invalid/foreign/stale marks, rewind with live leases, repeated reset, high-water accounting; no access after rewind |
| Handles | Empty/full slot table, out-of-range index, stale handle, double release, reuse, generation exhaustion; a released identity never resolves to new content |
| Payload pool | Exact run, one-page shortage, fragmented free space, pin-protected release, partial multi-run failure, rollback after decode error, repeated reserve/abort/commit cycles; no leak or overlapping live spans |
| Worker ownership | Cancel before start/during read/between blocks/after success, stale completion, full transport, short reads, failed submit, source replacement, shutdown in flight; no early reclaim or publication |
| Host bootstrap | Failure at each allocation/start step, incompatible capability/alignment, repeated cleanup; all acquired storage released exactly once |

Use deterministic schedules and fault injection to exercise state transitions.
Also execute the same worker body through actual native threads, stress repeated
supersession and shutdown, and run ThreadSanitizer where supported. Record an
unsupported sanitizer as a validation gap rather than treating deterministic
simulation as equivalent evidence. ASan/UBSan and guard regions cover bounds;
canaries alone do not establish synchronization correctness.

# Adoption sequence

Implementation status: the first checked-storage/arena increment is now present
in [memory.h](../../include/jelli/memory.h) and [memory.c](../../core/memory.c),
with [arithmetic/span](../../tests/test_memory.c) and
[arena](../../tests/test_memory_arena.c) tests.
[Memo-072](../memos/memo-072-memory-foundation-implementation.md) records its
validation. This implements the foundation increment only; the later consumer
and host-adoption stages remain proposed.

Each stage is separately reviewable. Tests and ownership contracts land before
production consumers switch to a foundation. Proposed new paths below are not
current files.

| Stage | Implementation surface and recommendation | Gate and rollback |
| --- | --- | --- |
| 0. Baseline | Inventory host startup allocations, fixed state, scratch and transition lifetimes using existing source; extend memo evidence | Reproducible current memory/build baseline and unresolved capacities recorded; no runtime change |
| 1. Checked storage | Add minimal span/layout helpers and transient arena under `include/jelli/` and `core/`, plus focused `tests/` and CMake registration | Foundation edge matrix passes core-only builds and sanitizers; no production adoption until contract review |
| 2. Resident storage | Prototype two-arena budgeting first; add fixed handles and contiguous-page pool only if the measured resource lifetime/peak requires them | Transactional reservation, pins, stale handles, fragmentation and failure rollback pass; keep storage policy behind the residency boundary |
| 3. Pack consumer | Build RFC-008 reader/decoder over checked spans and dedicated scratch; route memory/file fixtures through the same code | Entire catalog fidelity, malformed input and exact-capacity tests pass; retain current renderer as oracle |
| 4. Residency and worker | Add portable planning/commit/retirement, deterministic executor and actual native threaded adapter; use foundation contracts throughout | Full transition-union budget, no stale publication, cancellation/shutdown and thread stress pass; current asset path remains selectable for comparison |
| 5. Host adoption | Replace SDL reload-bank ownership through common residency; supply ESP32 startup spans and FreeRTOS execution without changing display ownership | Shared device-capacity host tests, relevant firmware profiles and allocation-failure tests pass; switch back at build configuration boundary if integration fails |
| 6. Production acceptance | Measure matched old/new firmware and physical interaction under separately authorized device work; remove superseded raw artwork path when accepted | Net flash and peak RAM evidence, warm frames with zero reads/decodes, load/input/render tails and physical display/touch verified; preserve reproducible comparison revision |

Follow [CONTRIBUTING](../../CONTRIBUTING.md#make-and-review-a-change) for each
changed layer: shared C requires `make test`, `make core-test`, `make sanitize`
and C quality checks; firmware adapters also require the relevant firmware builds.
Keep desktop tests, firmware builds, device measurements and physical evidence
separate. Do not claim an approved capacity or timing threshold before the owner
reviews the evidence.

After the first consumer validates an API, reuse it for subsequent matching
ownership paths: layout validation, resource decode, transition staging, reload,
and host bootstrap checks. Do not mechanically wrap every fixed array or force
persistent engine state, DMA buffers and save workspaces into one arena.

# Tradeoffs and unresolved choices

Two arenas simplify rollback but can duplicate shared resources. Page pools
support independent retirement but add fragmentation and metadata costs. Choose
using actual worst-case view unions and contiguous allocation needs. Arena marks
and generation handles add misuse surfaces; tests and narrow ownership boundaries
are prerequisites, not optional polish.

Resolve page size, metadata capacity, generation representation, initial span
alignment and worker scratch from the compiler's resource report and device
measurements. A static queue can reduce startup allocation calls while increasing
reserved internal RAM; adopt only with an explicit measured benefit. A generic
allocator framework, moving graphics onto stacks, and a general job scheduler
are outside this proposal.

# References

- [Jellipak proposal](rfc-008-jellipak-resource-store.md)
- [Compression measurements](../memos/memo-070-compressed-resource-payload-measurements.md)
- [Safe C ownership guidance](../../.agents/skills/jelli-safe-c/SKILL.md)
- [Embedded memory and timing guidance](../../.agents/skills/jelli-embedded-development/SKILL.md)
- [CMake build and test registration](../../CMakeLists.txt)

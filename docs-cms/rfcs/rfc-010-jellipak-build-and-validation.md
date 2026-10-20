---
title: Jellipak compiler and validation contracts
status: Draft
author: Codex
created: 2026-10-19
tags: [assets, build, memory, portability, validation]
id: rfc-010
project_id: jelligotchi
doc_uuid: bca0f008-7ca3-4938-9c97-b05e4beb02c1
---

# Summary

Specify the build tools and evidence needed to implement
[Jellipak](rfc-008-jellipak-resource-store.md) after the
[bounded-memory foundations](rfc-009-bounded-memory-systems-and-adoption.md).
This is a design proposal. The interfaces, fixtures, targets, and gates below
are not implemented commands or approved production defaults. Each implementation
increment requires its own review; this document does not authorize a runtime
migration or hardware deployment.

Implementation has begun with the bounded-memory foundation and a provisional
block-container reader/native verifier. [Memo-073](../memos/memo-073-jellipak-block-reader.md)
records that narrower implemented boundary and its checks. The complete compiler
and runtime contracts described below remain proposed.

The subsequent [artwork compiler increment](../memos/memo-074-jellipak-artwork-compiler.md)
implements a provisional binary image schema, shared palettes, grouped raw/LZ4
blocks, and explicit desktop build verification. Its single-pack publication
and Python art validation do not yet satisfy the full generation transaction,
C image reader, or runtime migration contracts below.

# Existing evidence and missing capabilities

[The measurement experiment](../../tools/assets/measure_jellipak.py) already
checks Python codec round trips, reconstructed opaque pixels and coverage,
grouped subrange extraction, sample RLE boundaries, and repeatability of its
report. [Memo-070](../memos/memo-070-compressed-resource-payload-measurements.md)
records its evidence and exclusions. Those assertions do not qualify a serialized
pack, shared C reader, asynchronous manager, firmware build, or physical display.

[Current asset generation](../../cmake/JelliPetAssets.cmake) declares source
dependencies and generates embedded arrays and IDs. [Existing tests](../../CMakeLists.txt)
cover the current asset/reload paths; they do not implement the Jellipak suite.
Preserve those tests as migration comparisons while adding the contracts here.

# Feature checklist

| Capability to implement | Acceptance evidence |
| --- | --- |
| Strict authoring validation and stable IDs | Invalid source fixtures fail before output publication |
| Canonical shared palettes and indexed coverage | Independent RGB565/mask comparison for every image |
| Bounded independently decoded blocks | Exact capacity, oversized-image, and strip-boundary fixtures |
| Versioned directory and dependency closure | Shared C reader accepts valid fixtures and rejects malformed references |
| Deterministic raw/codec selection | Golden bytes and cold/warm/cache-disabled equality |
| Complete serialized and linked budgets | Full pack accounting and matched firmware/map comparison |
| Transactional generation and incremental rebuild | Failure injection, per-input mutations, unchanged-output timestamps |
| Bounded residency and worker integration | Foundation edge cases plus real-thread and shutdown evidence |
| Shared desktop/device path | Same pack and portable runtime under device capacities |
| Candidate reload and recovery behavior | Corrupt candidate retains committed generation and source |

# Proposed tool interface

Use one compiler frontend with explicit subcommands. The name `jellipak` in this
table describes a future interface; no executable or Make target is established
by this RFC. Inputs, output directories, profile, and limits must be explicit.

| Proposed operation | Required behavior |
| --- | --- |
| `validate` | Check schemas, references, source images, palette compatibility, geometry, and dependency closure without publishing artifacts |
| `build` | Validate, compile one consistent artifact set, run required fidelity checks, enforce selected limits, then publish atomically |
| `inspect` | Read a pack without source artwork; report format, capabilities, tables, blocks, groups, and complete byte accounting |
| `verify` | Validate a pack and decode every block through the same portable C runtime shipped to both hosts; optionally compare with source artwork |
| `self-test` | Run bounded synthetic known-answer, malformed-input, capacity, and tool-transaction tests without network access or a board |

All operations return nonzero on failure and stable machine-readable error codes.
Human diagnostics identify source-relative path, asset/group ID, block index,
field or offset, expected bound, actual value, and suggested remedy when known.
Separate structured JSON from human progress output. Unknown options, missing
required profile data, or an unavailable C verifier fail visibly; never silently
substitute Python-only verification or a smaller fixture subset.

Produce a pack, stable-ID/dependency metadata, provenance manifest, and JSON size
report together. The report names format/schema versions, compiler and codec
versions, normalized input hashes, selected limits, complete pack hash, and
per-asset/block decisions. Deterministic artifacts exclude wall-clock time,
absolute paths, host identity, and unordered enumeration. Optional run timing
belongs in a separate evidence artifact so it cannot perturb pack equality.

# Build transactions and cache validity

Validate every output before publication. Write into a private temporary
generation, then atomically publish a generation manifest referencing complete
files. Readers resolve that manifest once and retain its immutable generation;
independent file renames are insufficient to publish a consistent set.
Interrupted builds, full disks, invalid sources, verifier failure, and competing
writers must leave the previous generation readable and clean up abandoned
temporary output. Serialize writers or reject a concurrent publisher explicitly.

Content-identical outputs retain their timestamps, including the published
manifest. Stable output names may use content-addressed files plus that manifest.
Test no-op behavior by recording file contents and timestamps before rebuilding,
without relying on a filesystem's timestamp granularity alone to detect writes.

Cache keys include normalized image bytes, canonical palette mapping, block
membership/order, geometry and coverage rules, format/schema version, compiler
modules, codec version/options, relevant profile limits, and metadata inputs.
Cache entries must validate their recorded identity and content hash before use.
Missing or corrupt entries rebuild; a cache hit cannot bypass validation.
Cold, warm, cache-disabled, reordered-directory, and relocated-checkout builds
must produce equal logical artifacts. Change each dependency class separately,
including tool pins and a newly added/removed PNG, and prove its dependent output
updates. Editing one frame must not recompress unrelated blocks; a shared palette
change legitimately invalidates every block using that palette.

# Validation contracts

## Authoring and wire-format rejection

Reject duplicate JSON object keys while parsing; validating an already-collapsed
dictionary cannot detect them. Cover missing/unknown required fields, duplicate
asset IDs, dangling references, invalid clip order, empty required groups,
unsupported schema/format versions and codecs, and nonfinite or out-of-range
numeric fields. Define forward-compatible optional fields explicitly rather than
accepting arbitrary unknown fields by accident.

Exercise zero and maximum counts, odd image widths, row/nibble/mask padding,
all-transparent images, sixteen opaque colors plus transparency, RGB565 color
collisions, and incompatible palette unions. Define whether an empty palette is
valid for all-transparent art before fixing its fixture; do not inherit the
experiment's nonempty-palette assertion as an unreviewed format rule.

Mutate stored and decoded lengths, offsets, table counts, palette lengths and
indices, checksums, dependency cycles/duplicates, overlapping forbidden ranges,
block subranges, strip coverage, and truncated fields. Test integer limits before
addition/multiplication, unaligned input, short reads at every boundary, excess
decoder output, incomplete output, trailing data rules, and unavailable codecs.
Rejected candidates expose no partial decoded views and preserve committed data.

## Independent fidelity and shared runtime

Keep hand-authored known-answer byte fixtures for endianness, nibble order,
row padding, masks, palette references, and each shipping codec. Expected bytes
must not be generated by the encoder under test. Compare against the existing
[exporter's conversion](../../tools/assets/embed_slice.py) and independently
specified vectors; encoder/decoder agreement alone can preserve the same bug.

Decode every compiled block through the production portable C implementation,
then compare every opaque RGB565 pixel and coverage bit with the source oracle.
Also compare dimensions, anchors, bounds, asset IDs, dependency closure, and clip
references. Python inspection and C inspection must agree on every table and
error category for the shared valid/malformed corpus. Memory and file adapters
consume identical pack bytes, including deliberate short-read injection.

Use seeded malformed-input fuzzing and retained minimized regression cases under
the project's sanitizer configuration. Bound input size, decoded output, steps,
and test duration; require clean failure without out-of-bounds access or hangs.
Keep a small deterministic corpus in ordinary tests and longer fuzz jobs separate.
Record the seed and exact reproducer with every failure.

## Residency, worker, and render boundaries

Reuse foundation storage and transport tests rather than reproducing their internals.
Add Jellipak-specific exact-fit and one-byte-short scratch, metadata, staging,
and expanded residency cases. Exercise common/current/next resource unions,
shared resource counting, fragmented capacity, every visible gallery closure,
superseded requests, and generation invalidation even when addresses are reused.

Run the same worker body inline and through real native threads. Force full
submission/completion capacity, late completion after cancellation, reload while
reading, source retirement, failed decode/commit, and shutdown with work active.
Do not reclaim spans or close sources before acknowledged completion. Verify
output visibility after synchronization, preserve completions until consumed,
and exercise supported native adapters, including Windows. Use ThreadSanitizer
where available, alongside deterministic interleavings and repeated stress.

Instrument Jellipak's source, decoder, and storage boundary: warm
animation/render/present frames must perform zero resource reads, zero decodes,
and zero Jellipak allocations. Audit library/SDK allocation separately rather
than infer its absence from those counters. Snapshot checks
cover pets, scenes, gallery, overlays, loading/recovery views, and reload rollback.
Timing claims require device measurements, including frame/input tails during
loading; host speed and firmware compilation provide separate evidence.

# Budgets and evidence artifacts

Keep limits in a versioned profile consumed by compiler and runtime tests.
Require explicit ceilings for complete pack bytes, metadata, largest stored and
decoded block, intermediate codec scratch, expanded working sets, transition
peak, worker storage, and per-service work. The RFC selects no numeric defaults.
Tests cover exact limit and first failing value; diagnostics name the dominant
asset/group/block and required versus available capacity.

Account for every serialized byte: header, directories, palettes, dependency
tables, payload, integrity fields, and padding sum to actual file length. Report
raw/indexed/compressed payload separately. Compare matched old/new release images
and linker maps with the same catalog, SDK, toolchain, and configuration; include
codec text and metadata and prove old pixel arrays are absent from the shipping
compressed build. Report free application-partition bytes separately from net
linked savings. Never use payload-only savings as the linked flash gate.

Archive the pack hash, source revision/dirty state, tool versions, commands,
profile, test results, complete size report, linker attribution, and failures.
Keep hardware latency/heap/stack and physical display/touch reports distinct.
Threshold changes require review and rationale; tests cannot rewrite their own
expected budgets or accept missing measurements as a passing zero.

# Repository integration and review gates

| Existing source | Proposed extension |
| --- | --- |
| [Asset generation](../../cmake/JelliPetAssets.cmake) | Declare all compiler/schema/content dependencies and output/byproduct manifest; consume one generation in SDL and firmware |
| [CMake tests](../../CMakeLists.txt) | Add independent C fixtures, tool self-test, full-catalog parity, and scratch-tree incremental rebuild tests with explicit timeouts |
| [Make workflow](../../Makefile) | Expose thin wrappers for the reviewed tool interface; retain existing test/sanitizer commands |
| [Core CI](../../.github/workflows/core.yml) | Build portable reader and fixtures without SDL/SDK on the existing platform matrix |
| [Desktop CI](../../.github/workflows/desktop.yml) | Exercise compiler, full-catalog C verification, snapshots, sanitizer and real-thread tests |
| [Firmware CI](../../.github/workflows/firmware.yml) | Build normal/power/factory profiles and extend isolation checks to Jellipak linkage |
| [Firmware component](../../ports/esp32/main/CMakeLists.txt) | Embed one pack in game profiles and preserve factory exclusion of game/assets |

Foundation tests and independent format fixtures come first. Then qualify the
compiler transaction and full-catalog C sweep, followed by residency/worker
integration, matched firmware budgets, and authorized device evidence. Keep
experimental codec and profiling dependencies out of normal firmware. Core-only
tests must remain independent of artwork tools and SDK setup; full-catalog tests
belong in their explicit integration lane. Factory builds must retain their
existing early return and asset exclusion. CI should publish useful diagnostics
on failure and fail if a required verifier, corpus, report, or test lane is absent.

# Remaining decisions

Review the wire schema, optional-field policy, codec subset, generation manifest
layout, C verifier invocation, profile limits, and acceptable device timing
before implementation acceptance. A separate review must decide which expensive
catalog, stress, fuzz, and firmware comparisons run per change versus scheduled
or release validation. Deterministic self-test is a fast gate; it does not replace
full-catalog, threaded, firmware, or physical evidence.

---
title: Jellipak artwork compiler and local desktop verification
author: Codex
created: 2026-10-19
tags: [assets, compression, implementation, testing]
id: memo-074
project_id: jelligotchi
doc_uuid: 3492b69c-4f4f-48f5-b95b-d1dc4d9bb506
---

# Implemented increment

The owner requested continued implementation and verification in a local desktop
build. The [artwork compiler](../../tools/assets/jellipak.py) now produces a
complete provisional image pack accepted by the
[native block reader](memo-073-jellipak-block-reader.md). It adds `build`,
`inspect`, and `verify` commands, with pinned Python, Pillow, and LZ4 dependencies.
The source manifest and PNG files remain authoritative.

The compiler shares sorted RGB565 palettes within compatible creature-form,
background, and UI groups. Authored palette fingerprints constrain grouping;
each valid source image uses a subset of its authored palette, so these groups
cannot exceed the source palette limit. RGB565 collisions collapse to one
index. Transparent pixels use zero indices plus separate coverage, preserving
all opaque indices. Odd rows have canonical nibble and mask padding. Fonts
retain the existing glyph-major mask layout.

Clip keys are sorted for deterministic traversal; frame order within a clip is
preserved. Frames shared by multiple inferred form scopes, or unreferenced
creature frames, get singleton locality groups. This is a compression heuristic,
not the future scene dependency-closure system. Backgrounds sharing a palette
occupy the same decoded block when capacity permits. Each image remains wholly
within one block; oversized images and metadata fail explicitly rather than
silently increasing the requested limit.

The provisional binary `JART` metadata contract is described by
`FIELDS`, `compile_art`, and `parse_metadata` in the compiler. It records stable
image IDs and keys, dimensions, formats, palette references, block subranges,
strides, bounds, ground anchors, and centroids. It uses explicit little-endian
fields and fixed palette slots. The metadata block and artwork blocks each
choose raw or LZ4 independently. Clips and provenance remain in the report;
animation tables and dependency closure are not yet runtime pack contracts.

Before publication, the compiler reconstructs all source images and compares
opaque RGB565 pixels, coverage, geometry, and glyph masks. The native verifier
then decodes and checks every container block. **JART semantic validation and
pixel comparisons run in Python; the C reader currently validates only the
container and decoded block bytes.** Neither renderer consumes this pack yet.

# Publication and limits

The candidate pack is private until validation succeeds, then atomically replaces
the destination. Identical output preserves its timestamp. The optional report
is published separately and identifies its pack by SHA256; it is informational,
not a transactional generation manifest. Consumers must not treat the two files
as an atomically published bundle. Concurrent writer serialization, generation
manifests, cache invalidation, and incremental recompression remain planned.

Resolved output paths cannot overwrite the source-art or compiler directories.
The Python inspector bounds file reads and aggregate decoded storage before
decompression, and native verification has a subprocess deadline. Authoring JSON
duplicate keys are rejected before the existing source validator runs.
The policy values and dependency versions are maintained in the compiler,
rather than duplicated as production memory budgets here.

# Reproduce the desktop build

```sh
make jellipak
./scripts/uv run --python 3.12 tools/assets/jellipak.py verify \
  build/desktop/generated/jellipak.jpak \
  --verifier build/desktop/jelli_jellipak_verify --source-fidelity
make test
./build/desktop/jelligotchi --pet --headless --demo --frames 780 \
  --snapshot build/desktop/jellipak-desktop-smoke.bmp
```

The explicit [CMake target](../../cmake/JelliPak.cmake) builds the native verifier
and compiles the source artwork. It revalidates on every invocation. It is not
part of the default renderer asset generation. The [Make entry](../../Makefile)
selects the desktop preset, and [CTest](../../CMakeLists.txt) registers the
[independent artwork tests](../../tests/test_jellipak_build.py).

# Recorded size evidence

Measured locally on 2026-10-19 with the target's explicit decoded-block limit:

| Component | Bytes |
| --- | ---: |
| Complete serialized pack | 25,016 |
| Stored artwork blocks | 20,248 |
| Stored metadata block | 4,352 |
| Container header and directory | 416 |
| Decoded artwork indices and masks | 98,952 |
| Decoded metadata | 12,141 |
| Existing RGB565 and mask payload baseline | 333,672 |

The pack contains 121 images, 8 palette records, and 12 blocks including metadata.
Its largest decoded block is 16,000 bytes. The six backgrounds occupy one
15,360-byte decoded block stored in 3,955 bytes. Metadata includes image records,
palette slots, and keys; its accounting is available in
`build/desktop/generated/jellipak-report.json`.

Pack SHA256:
`bf2b4d5395b3848bbea8e9041069a396b2198ddbcafe40aef56b09d3295ad8b7`.
The report's normalized source digest is
`3c8b0d1e57de3f9b5187b460be31ef7a27964be30ae1a9fcf924cd171d3363b7`.
Compiler SHA256:
`6ab17a842b059efc1aea41bae5dcec70468c9626a137247dea0d0d052723867c`.
The report also records helper source digests and dependency versions.

The complete pack is approximately 92.5% smaller than the existing pixel/mask
payload baseline. That comparison excludes existing C lookup/animation tables,
new decoder code, and linker placement. It is **not net firmware flash savings**.
The [earlier experiment](memo-070-compressed-resource-payload-measurements.md)
also excluded serialized metadata; its payload totals are not interchangeable
with this complete pack measurement.

# Review and verification

Sol implemented the compiler and independent test oracle; Luna reviewed
correctness, publication, and measurement boundaries. Synthetic tests exposed
an eager geometry calculation for wholly transparent images. The compiler now
uses explicit empty geometry and an empty palette for that case; the existing
production source validator still imposes its own authoring constraints.
Review found that arbitrary output paths could overwrite source artwork, and
that inspection lacked an aggregate decompression limit. Both were corrected.
Tests verify rejection before source publication, bounded expansion, and timeout
rollback.

The independent oracle parses wire records and compares pixels without using the
compiler's parser or fidelity functions. It covers the full catalog, RGB565
collisions, all opaque indices plus transparency, odd padding, shared ranges,
empty palettes, exact capacities, oversized inputs, no-op timestamps, malformed
art metadata with repaired CRCs, and verifier failure cleanup.

Local macOS validation:

| Check | Result |
| --- | --- |
| `make jellipak` | Desktop target built and verified the complete source catalog |
| Repeat `make jellipak` | Pack and report bytes and modification timestamps unchanged |
| `verify --source-fidelity` with desktop verifier | Native block sweep and Python image comparisons passed |
| `make test` | All 79 desktop tests passed, including the new artwork suite |
| `make core-test` | All 37 tests passed |
| Artwork suite against ASan/UBSan verifier | All 10 tests passed |
| Existing sanitizer Jellipak C/CLI suites | Both passed |
| SDL headless demo | Ran 780 frames; captured scene visually inspected |
| `make docs-check`, `make docs-fix`, `git diff --check` | Passed; no unresolved findings |

This increment changes Python tooling and build integration, not C source.
The sanitizer checks above reused the sanitizer build qualified in
[memo-073](memo-073-jellipak-block-reader.md); a complete sanitizer suite was
not rerun for this tooling increment. The code and documentation remain local,
uncommitted working-tree changes.

# Remaining runtime work

Add a bounded C JART reader and image expansion API, then connect caller-owned
residency and frame-boundary publication to both hosts. RLE selection, threaded
loading, ESP32 task integration, and matched firmware/map measurements remain
subsequent increments. The local SDL smoke uses the existing embedded artwork;
it establishes a desktop regression check, not Jellipak rendering or physical
device behavior.

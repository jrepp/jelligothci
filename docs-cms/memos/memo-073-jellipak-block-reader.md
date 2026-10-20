---
title: Jellipak portable block reader and native verification
author: Codex
created: 2026-10-19
tags: [assets, compression, implementation, portability, testing]
id: memo-073
project_id: jelligotchi
doc_uuid: d93f8299-f195-4929-89ec-360fc551f60b
---

# Implemented boundary

Following the owner's instruction to continue implementation, this increment
adds a portable block-container foundation for
[Jellipak](../rfcs/rfc-008-jellipak-resource-store.md). Its provisional wire
contract and ownership rules live in the
[public header](../../include/jelli/jellipak.h). It is not the complete artwork
schema: palette groups, image geometry, stable asset IDs, dependencies, residency,
RLE selection, and worker dispatch remain subsequent work.

The container uses explicit little-endian fields, a checked directory, ordered
block identifiers, contiguous independent payloads, and header, directory,
stored-payload, and decoded-payload CRCs. Caller limits bound pack size, directory
scans, and stored/decoded block sizes. Raw and independent LZ4 blocks are supported.
The LZ4 decoder is project code, rather than a vendored third-party implementation;
its reference is the [official LZ4 block specification](https://github.com/lz4/lz4/blob/v1.9.4/doc/lz4_Block_format.md).
CRC detects accidental corruption; it is not authentication.

Mounting validates metadata. Decoding fetches and verifies only the requested
block through an exact-read callback. A caller can supply memory, flash, or file
storage through that boundary. The mounted source must remain immutable and
alive; file-backed callers must retain one consistent generation. The core owns
no heap allocations. The caller supplies separate stored and decoded spans;
failed decoding may modify these spans, and callers publish output only after
success. No thread synchronization is implied.

The [native verifier](../../tools/assets/jellipak_verify.c) applies its own
bounded host policy and sweeps every block through the same C reader. It reports
stored, decoded, and complete container byte totals. These totals distinguish
payload from directory overhead; they do not measure linked firmware savings.
The active SDL and firmware renderers still use their existing asset path.

# Reproduction and validation

Build the native targets using `make core-test`. To verify a container:

```sh
build/core/jelli_jellipak_verify path/to/container.jpak
```

The [CLI fixtures](../../tests/test_jellipak_cli.py) independently assemble wire
bytes with Python's standard library. They cover an empty pack, mixed raw/LZ4,
an overlapping match, every truncation of a small pack, payload corruption,
trailing bytes, unsupported codecs, invalid streams, and command-line failures.
They need no codec package or board.

The optional [reference-encoder corpus](../../tests/test_jellipak_lz4.py) uses
a pinned Python LZ4 package with deterministic repetitive, patterned, low-entropy,
and random input at boundary sizes:

```sh
./scripts/uv run --python 3.12 tests/test_jellipak_lz4.py \
  build/core/jelli_jellipak_verify
```

This separate interoperability run may install its pinned dependency. It is
not required by the offline fixture suite. It validates decoding and decoded
CRCs, not image reconstruction or production pack generation.

Independent Sol implementation/test passes and Luna review checked the reader
boundary. Review caught a test that mutated a still-mounted source and a test
comparison involving struct padding; the test now remounts after mutation and
compares fields. Static analysis could not establish nonnull decode buffers
through the mounted-pack invariant, so decoding also checks those pointers
explicitly before use. Cppcheck requested const context pointers for two
read-only callback implementations; narrow documented suppressions retain the
shared callback signature, which also supports mutable file/transport contexts.

Native macOS checks:

| Check | Result |
| --- | --- |
| `make core-test` | All 37 tests passed |
| `make test` | All 78 tests passed; both Jellipak suites also rebuilt and passed after the analysis remedies |
| `make lint-c` | Formatting, clang-tidy, Cppcheck, and source-size checks passed |
| `make sanitize` | All 78 tests passed with ASan/UBSan |
| Reference LZ4 corpus | All 104 blocks passed in core and ASan/UBSan verifier builds using Python LZ4 4.4.5 / library 1.9.4 |

`make docs-check`, `make docs-fix`, and `git diff --check` passed after the
evidence record was finalized. The documentation fixer sorted the new memo's
tags; no unresolved findings remain.

The reference corpus contains 694,904 decoded bytes, 268,598 stored payload bytes,
and 271,958 complete container bytes. The 3,360-byte difference is the header
and directory for this synthetic corpus. These are interoperability fixture
measurements, not real artwork savings.

The local uncommitted implementation/test set has SHA256
`e6a60ff94301776d92bef65df4e71f953dd92ead1afa2a0b4dc5a0e09504b226`.
Hash sorted repository-relative names, a zero byte, then file contents for
`CMakeLists.txt`, `core/jellipak.c`, `core/jellipak_decode.c`,
`include/jelli/jellipak.h`, `tests/test_jellipak.c`,
`tests/test_jellipak_cli.py`, `tests/test_jellipak_lz4.py`, and
`tools/assets/jellipak_verify.c`. The checked-memory dependency is recorded in
[memo-072](memo-072-memory-foundation-implementation.md).

# Remaining work and measurement boundary

The [earlier payload measurements](memo-070-compressed-resource-payload-measurements.md)
remain experiments, not firmware capacity gains. The next compiler increment
must add the artwork schema, canonical shared palettes, grouping and ordering,
bounded block selection, and comparison against source RGB565/coverage.
Its complete serialized output must pass this reader before publication.
Matched linked firmware builds are still required to establish net flash savings.

The [build and validation plan](../rfcs/rfc-010-jellipak-build-and-validation.md)
also retains transactional generation, cache invalidation, working-set ownership,
and native/device threading tests. This increment establishes no firmware build,
physical display/touch evidence, or device timing measurements.

---
title: Jellipak native byte-for-byte decompression verification
author: Codex
created: 2026-10-19
tags: [assets, compression, implementation, testing]
id: memo-075
project_id: jelligotchi
doc_uuid: 761eb445-715a-4016-aa84-bb7f951f3ea2
---

# Closed validation gap

The owner asked whether native decompression had been validated byte for byte.
[Memo-074](memo-074-jellipak-artwork-compiler.md) established exact source-image
comparisons in Python and native decoded CRC checks, but not a complete
comparison of native output with original precompression buffers.

The [compiler](../../tools/assets/jellipak.py) now captures immutable metadata
and artwork buffers **before compression**. Every `build`, including
`make jellipak`, supplies those originals to the
[native verifier](../../tools/assets/jellipak_verify.c). The verifier decodes
through the portable C reader and compares every resulting byte before the
compiler publishes the pack. Expected bytes are not obtained by decompressing
the generated pack.

The temporary `JRAW` stream identifies the expected version, block count, and
each block's ID, length, and original bytes. The verifier reads it in bounded
chunks, rejects mismatched ordering or lengths, identifies the first differing
byte, and requires exact EOF. Truncation and trailing data fail. Inputs are
read-only; temporary expectations are removed after success, rejection, or
timeout. The build report records the original-buffer provenance, compared
block count, and byte count only after successful native comparison.

Direct verifier usage is:

```sh
build/desktop/jelli_jellipak_verify PACK --compare EXPECTED.jraw
```

Running the verifier with only `PACK` retains the previous CRC sweep. Likewise,
the standalone Python `verify --source-fidelity` command retains its previous
Python image comparisons and native CRC sweep; it does not claim a comparison
with original precompression buffers. Use `make jellipak` to reproduce the
mandatory original-buffer comparison.

# Measured result

The full source catalog at the explicit desktop target limit compares all
12 blocks and **111,093 bytes**, including:

| Original buffers | Bytes compared |
| --- | ---: |
| Shared-palette image indices and coverage / font masks | 98,952 |
| Binary image and palette metadata | 12,141 |

The pack remains 25,016 bytes with unchanged SHA256
`bf2b4d5395b3848bbea8e9041069a396b2198ddbcafe40aef56b09d3295ad8b7`.
This establishes lossless compression/decompression of the defined pack
representation. It does not imply preservation of arbitrary RGBA source bytes:
the representation deliberately uses RGB565 and normalizes invisible indices.
Source-image fidelity checks separately verify opaque colors and coverage.

# Tests and remaining boundaries

The [CLI tests](../../tests/test_jellipak_cli.py) independently construct expected
streams for raw and LZ4 blocks, including an empty container. They check altered
expected bytes despite a valid pack CRC, wrong counts/IDs/order/lengths, every
truncation of a small expected stream, trailing data, missing files, and unchanged
input files.

The [artwork tests](../../tests/test_jellipak_build.py) capture originals directly
from compilation and exercise full-catalog native comparison. They also corrupt
an expected byte and verify rejection, cleanup, and preservation of the previous
published pack. This closes the gap where a CRC-only sweep could be mistaken for
a direct byte comparison.

Sol implemented the comparison and Luna independently reviewed and tested it.
Local macOS validation passed:

| Check | Result |
| --- | --- |
| `make jellipak` | Mandatory native comparison: 12 blocks, 111,093 original bytes |
| `make core-test` | All 37 tests passed |
| `make test` | All 79 desktop tests passed |
| `make sanitize` | All 79 tests passed with ASan/UBSan |
| Native CLI suite | Seven tests, including independent comparison failures |
| Artwork suite | Twelve tests, including full-catalog original buffers and rollback |
| `make lint-c` | Formatting, static analysis, and source-size checks passed |
| Documentation checks and whitespace review | Passed with no unresolved findings |

The code and evidence remain local, uncommitted changes. The core decoder and firmware runtime were not
changed; C JART semantics, image expansion, scene residency, and firmware flash
measurements remain subsequent work.

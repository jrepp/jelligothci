---
title: Jellipak compressed indexed resource payload measurements
author: Codex
created: 2026-10-19T00:00:00Z
tags: [assets, compression, design, measurement]
id: memo-070
project_id: jelligotchi
doc_uuid: 24932607-6a3e-4bf6-831e-12583f0e20db
---

# Scope and reproduction

This is a Jellipak design experiment for [RFC-008](../rfcs/rfc-008-jellipak-resource-store.md),
not a firmware change or an implemented loader. Measurements use source revision
`d830df7374ca3e72ed0608f6341b0776d0ed29e0`. The manifest and PNG digest is
`10f3e06a8d51d01eec4421352b14b96f369630a76b3da711c3a35e5b5d51b232`.
The digest hashes sorted repository-relative filenames, a zero separator, and
file bytes. It covers the manifest and all referenced PNGs.

Run from the repository root:

```sh
./scripts/uv run --python 3.12 tools/assets/measure_jellipak.py > /tmp/jellipak-measurements.json
```

The [experiment](../../tools/assets/measure_jellipak.py) uses
[build_slice.load_assets](../../tools/assets/build_slice.py) for the validated
source catalog and mirrors [embed_slice](../../tools/assets/embed_slice.py)'s
RGB565 conversion and glyph-major font mask. Its PEP 723 dependencies pin
Pillow 12.0.0 and [python-lz4 4.4.5](https://pypi.org/project/lz4/4.4.5/).
The measured environment used Python 3.12.15 and linked LZ4 library 1.9.4.
The scoped Git status for the manifest and referenced PNGs was clean; unrelated
draft documentation does not affect this status. The experiment script SHA256
was `8e137f6b44395be1e6d7080fcebb704846324d63a6fbef4748350d85884ab657`.
JSON includes the script digest, scoped source status, dependency versions,
source digest, per-asset lengths, codec choices, raw block hashes, and totals.
The script measures twice and requires identical reports; all candidate codecs
round-trip every block. No device or firmware build was run for this experiment.

# Representation and codec experiment

Each non-font asset gets its own independently decodable block: packed 4-bit
indices, high nibble first with each row padded to a whole byte, followed by a separate row-padded MSB-first 1-bit
alpha mask. Sorted unique opaque RGB565 colours form its canonical palette,
stored as little-endian 16-bit values. Transparent pixels use index zero,
normalizing invisible RGB. Alpha remains separate: sixteen opaque colours plus
transparency cannot fit in sixteen combined colour/alpha symbols. The script
checks every opaque reconstructed RGB565 pixel and every mask bit against the
source. This establishes rendered pixel/mask equivalence, not equality of
invisible RGB bytes with the existing arrays. The font retains its existing
1-bit glyph-major representation, with no palette.

Block candidates are raw, byte RLE, independent raw LZ4, and RLE followed by
LZ4. RLE controls 0–127 carry 1–128 literal bytes; controls 128–255 repeat the
next byte 1–128 times. The encoder emits repeat packets for runs of at least
three. LZ4 uses high compression level 12 without a stored size prefix or
frame header; lengths belong in future block metadata. Decoding RLE+LZ4 also
requires the intermediate RLE length. Each candidate is tested against the
raw block. Additional samples cover empty input, literal boundaries, and repeat
boundaries. Minimum selection uses raw first on ties, then RLE, LZ4, RLE+LZ4.

The experiment also measures bounded multi-image blocks and shared palette
sets, described below. Every grouped candidate verifies concatenated LZ4
round-trips and individual image offset/length extraction. Palettes belong to
image sets and can be referenced by many compressed blocks.

# Measured payload bytes

All numbers below are dated measurements of the revision above. Every codec
column excludes palettes, indexes, alignment, loader code, and runtime RAM.
The baseline contains RGB565 pixels plus alpha masks and the existing font
mask, without C structs or pointers. The raw indexed column includes masks
and the font. These are real encoded byte counts, not entropy estimates.

| Kind | Assets | Baseline | Indexed raw | RLE | LZ4 | RLE+LZ4 | Minimum |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Creatures | 44 | 171904 | 50560 | 16988 | 14244 | 15354 | 14244 |
| Backgrounds | 6 | 52224 | 15360 | 8039 | 5882 | 6420 | 5882 |
| Effects | 8 | 4352 | 1280 | 910 | 783 | 821 | 773 |
| Font | 1 | 1152 | 1152 | 815 | 470 | 527 | 470 |
| Health | 10 | 21760 | 6400 | 3426 | 2270 | 2432 | 2270 |
| Icons | 12 | 6528 | 1920 | 1095 | 903 | 933 | 893 |
| Menus | 16 | 34816 | 10240 | 4842 | 3323 | 3494 | 3317 |
| Meters | 5 | 10880 | 3200 | 1426 | 1118 | 1169 | 1115 |
| Prizes | 10 | 21760 | 6400 | 3243 | 2367 | 2530 | 2358 |
| Props | 9 | 8296 | 2440 | 1511 | 1184 | 1238 | 1179 |
| Total | 121 | 333672 | 98952 | 42295 | 32544 | 34918 | 32501 |

Actual palette bytes total 1546 with one palette per image, or 950 with exact
byte deduplication across 75 unique palettes. Deduplication requires references
and palette lengths in future metadata. Minimum selection chooses 112 LZ4,
eight RLE+LZ4, and one RLE blocks; no raw block wins on this art. Raw fallback
still prevents compression expansion on future assets. Minimum payload plus
deduplicated palettes is 33451 bytes: 300221 bytes (89.97%) below the baseline.
LZ4-only plus those palettes is 33494 bytes, just 43 bytes larger. Therefore
these measurements favor implementing only raw/LZ4 initially; adding both RLE
paths would trade decoder complexity and additional code for little payload
benefit on this catalog.

# Natural grouping, shared palettes, and decode locality

Two grouping policies use raw/LZ4 minimum selection and decoded limits of
8, 16, and 32 KiB. Chunks never split an image. Creature ownership comes from
clip key prefixes; frames are traversed in manifest clip order with first-seen
deduplication. A frame reachable from multiple forms, or from no clip, gets
its own singleton group. The compact policy groups remaining images by kind,
including backgrounds across scenes. These blocks preserve per-image compact
palette meanings: adjacent images need not interpret an index identically.

The shared policy groups by authored palette RGB565 contents within a creature
form, across backgrounds, or across the remaining UI kinds. It then constructs
one sorted canonical palette from the actual opaque colour union of each full
image set and remaps every image losslessly. Each union is asserted to fit
sixteen colours; differing authored palettes are kept in compatibility
subgroups rather than merged or quantized. The font remains separate. A palette
belongs to the full compatible image set, such as one pet form, and is stored
once regardless of how many chunks hold that set. Actual shared palette bytes
are 170, both with and without deduplication across sets. The compact policy
uses the previously measured 950 deduplicated palette bytes.

The following table uses clip traversal order for creatures. Remaining compact
kind groups start in ascending asset ID order; shared UI groups concatenate
those kind traversals. Payload columns exclude palette bytes and all metadata.
The limit and decoded sizes refer to indexed/mask scratch bytes, not expanded
RGB565 residency. Extra decode is the maximum unrequested indexed/mask data decoded on a single
image miss, assuming none of that block is already cached.

| Policy | Indexed limit KiB | Blocks | Payload bytes | Palette bytes | Largest indexed block | Maximum extra decode |
| --- | --- | --- | --- | --- | --- | --- |
| Independent assets | — | 121 | 32544 | 950 | 2560 | 0 |
| Compact natural groups | 8 | 19 | 22342 | 950 | 7680 | 7040 |
| Compact natural groups | 16 | 14 | 20455 | 950 | 15840 | 14400 |
| Compact natural groups | 32 | 13 | 19981 | 950 | 25920 | 24480 |
| Shared compatible sets | 8 | 17 | 22816 | 170 | 7880 | 7720 |
| Shared compatible sets | 16 | 11 | 20274 | 170 | 16200 | 16040 |
| Shared compatible sets | 32 | 9 | 19105 | 170 | 30920 | 30760 |

Shared palettes do not guarantee smaller compressed data at every chunk size:
remapping indices and changing group boundaries affects LZ4 matches. Including
actual palette bytes, the shared 16 KiB candidate totals 20444 bytes, versus
33494 for independent LZ4 assets; the shared 32 KiB candidate totals 19275.
This is a locality/compression tradeoff, not evidence that a particular cache
or decode latency budget is met. Backgrounds specifically improve from 5882
bytes independently to 4551 with 8 KiB chunks or 3955 with 16/32 KiB chunks.
Such background batches decode inactive scenes on a miss. A future loader can
retain only requested expanded images after consuming the block; avoiding a
full decoded-block scratch buffer requires a qualified streaming decoder.

An additional ascending-ID order comparison is included in JSON. At 8/16/32
KiB its compact payloads are 22315/20696/19985 bytes, and shared payloads are
22750/20351/19110. The experiment does not search all permutations or establish
a global optimum. It supplies group/block lengths plus every image's offset
and length for review; a production directory must encode these references,
palette ownership, codec and decoded limits. Directory, palette descriptor,
alignment, integrity, and decoder overhead remain excluded.

# Interpretation and remaining uncertainty

The savings above concern source image payloads only. A file header, block and
asset indexes, palette descriptors, alignment, integrity checks, loader and
decoder code, and any new animation/catalog metadata are excluded. Existing
asset metadata is excluded on both sides. RFC-008 describes metadata fields; their final serialized overhead is not
measured by this experiment. Actual firmware net
flash savings require building both implementations with the same toolchain
and comparing linked sections; no such result exists here.

No decode timing, shared C decoder correctness, bounded cache behavior, peak
RAM, physical display, or touch evidence was collected. Python codec
round-trips establish the experiment's encoded data, not production decoder
qualification. Before integration, validate a portable bounded C decoder with
malformed blocks and pixel comparison on desktop, then measure ESP32 timing,
RAM, linked flash, and physical rendering separately.

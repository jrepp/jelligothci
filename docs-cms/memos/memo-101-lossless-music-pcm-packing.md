---
id: memo-101
title: Lossless music PCM packing experiment
author: Codex
created: 2026-10-21
tags: [assets, audio, measurement, memory]
project_id: jelligotchi
doc_uuid: b170472d-8c95-4337-a341-edc06f78fe52
---

# Scope and existing behavior

The owner requested investigation of header removal, normalization, RLE and delta
compression for music PCM in Jellipak. This increment implements an offline
[measurement and experimental pack emitter](../../tools/assets/measure_music_pcm.py).
It does not change the player, firmware build, authored MODs, or shipping codecs.
The broader runtime adoption remains proposed under [RFC-010](../rfcs/rfc-010-jellipak-build-and-validation.md).

The [music compiler](../../tools/audio/build_music.py) already strips MOD headers,
sample names and tracker encoding, emitting signed 8-bit mono PCM plus loop
metadata and compiled playback spans. Thus header removal offers no additional
runtime saving. Canonicalization here converts signed sample values to identical
8-bit representations, then deduplicates exact byte strings. It does not normalize
amplitude, resample, quantize or change sound. Loop start/length, per-instrument
volume and zero finetune remain separate references; identical PCM can be shared
without conflating those playback properties. The strict MOD codec rejects
nonzero finetune before measurement.

# Reproduction and accounting

Run from the repository root:

```sh
./scripts/uv run --python 3.12 tools/assets/measure_music_pcm.py > /tmp/music-pcm.json
./scripts/uv run --python 3.12 tools/assets/test_measure_music_pcm.py
```

The report records every authored MOD SHA-256, per-song PCM and span counts, each
unique blob hash, references and codec sizes. It checks exact round trips for all
candidates and repeatability. LZ4 is pinned; delta is modulo-256 byte differencing
with a zero predictor at each independent block start. Delta alone changes no
length. RLE reuses the existing experiment's literal/repeat byte codec.

Measurements on the fifteen-track catalog from commit `3894f16`:

- Repeated source PCM: 23,808 bytes in 71 nonempty sample references.
- Exact deduplication: 16 unique blobs totaling 3,104 bytes.
- Experimental reference metadata: 2,288 bytes (16-byte header plus 32 bytes per
  reference). This is an explicit candidate schema, not current player metadata.
- Independent layout: 32-byte JPAK header plus 17 directory entries of 32 bytes
  (one raw metadata block and sixteen sample blocks). No padding is assumed.

| Encoding | Independent PCM payload | Complete independent model | Complete single-PCM-block model |
| --- | ---: | ---: | ---: |
| Raw | 3,104 | 5,968 | 5,488 |
| RLE | 2,332 | 5,196 | 4,706 |
| Delta | 3,104 | 5,968 | 5,488 |
| Delta then RLE | 2,394 | 5,258 | 4,768 |
| LZ4 | 2,567 | 5,431 | 4,845 |
| Delta then LZ4 | 2,021 | 4,885 | 4,278 |
| Best existing raw/LZ4 per sample | 2,545 | 5,409 | — |
| Best experimental per sample | 1,976 | 4,840 | — |

Only the independent existing raw/LZ4 selection is also emitted as a real pack.
All other complete sizes are accounting models, not implemented runtime formats.
The grouped model needs a future reference-to-offset mapping; offsets can be
computed from the ordered unique lengths represented in the references, but no
grouped PCM schema is implemented here.

To emit and verify the concrete experimental artifact:

```sh
./scripts/uv run --python 3.12 tools/assets/measure_music_pcm.py \
  --pack /tmp/music-pcm.jpak \
  --verifier build/desktop/jelli_jellipak_verify
```

This uses the existing generic JPAK v1 BLOK/raw/LZ4 contract. Metadata block 1
contains four little-endian u32 values (schema version, track count, reference
count, unique blob count), followed by eight-u32 references: track index, sample
slot, zero-based blob index, PCM length, loop start, loop length, volume, finetune.
A blob index maps to block ID index+2. Empty sample slots imply silence. Names stay
in authoring MODs. The emitter verifies a temporary candidate using the existing
native generic reader against precompression originals before publishing it.
This schema has no player consumer and is experimental, not a stable asset API.

# Residency and proposed next increment

The largest sample is 1,024 bytes; maximum per-song unique PCM is 1,888 bytes.
A fully decoded shared bank needs 3,104 bytes. Independent decode needs bounded
stored scratch plus decoded output; decoded samples must remain resident while
used. LZ4 needs random access to already decoded output; delta inversion can run
in place afterward. RLE/delta work is linear in bounded output size, but neither
has a shipping Jellipak decoder. No timing benchmark is claimed.

The player seeks sample offsets and loops directly during mixing. Compression
cannot be inserted into that audio callback without a separate bounded load/cache
contract. Current immutable arrays use no PCM decode RAM. These proposed resident
bytes are therefore a new RAM cost, not free compression. Timeline metadata is
excluded from all pack totals: the catalog contains 4,186 compiled spans, with
18 bytes of logical fields per span before ABI padding. This is larger than the
PCM bank and requires separate investigation. Source array totals also do not
establish linked flash usage: compiler/linker constant merging must be measured
with matched map/image builds before claiming firmware savings.

The evidence supports a **proposed deduplication-first compiler increment**, with
native/render parity and matched linked-size measurements. Existing raw/LZ4 on
one shared PCM block nearly matches experimental per-sample choices (4,845 versus
4,840 complete modeled bytes), so new codec complexity is not justified yet.
Retain signed PCM bytes and independent loop/volume references. Decide loading
and residency separately before any runtime Jellipak adoption.

# Verification and limitations

Four regression tests cover predictor wrap, signed boundaries, literal/repeat
boundaries, seeded noise, catalog determinism/accounting and serialized raw/LZ4
round trips. The actual 5,409-byte pack passed native byte comparison of all 17
blocks, totaling 5,392 decoded bytes. Metadata is included in that comparison.
No corrupted-input runtime contract, audio-thread integration, linked-size
reduction, firmware build or physical playback is established by this experiment.
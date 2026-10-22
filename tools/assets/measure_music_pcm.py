#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0", "lz4==4.4.5"]
# ///
"""Lossless PCM packing experiment; no runtime codecs or authored files change.

Run ./scripts/uv run --script tools/assets/measure_music_pcm.py
The proposed metadata uses eight u32 fields per nonempty sample reference:
track, slot, blob, length, loop start, loop length, volume, finetune (zero).
Names remain authoring data. Container costs use existing JPAK v1 sizes.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import zlib
from importlib.metadata import version

import lz4.block
from measure_jellipak import compress, rle_encode, rle_decode
from jellipak import native_verify

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/audio'))
from build_music import KEYS, compile_song

spec = importlib.util.spec_from_file_location('music_mod', ROOT / 'tools/jelli-art/music_mod.py')
codec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codec)


def delta(data):
    return bytes((value - (data[i - 1] if i else 0)) & 255 for i, value in enumerate(data))


def undelta(data):
    out = bytearray()
    value = 0
    for difference in data:
        value = (value + difference) & 255
        out.append(value)
    return bytes(out)


def candidates(raw):
    differences = delta(raw)
    encoded = {'raw': raw, 'rle': rle_encode(raw), 'delta': differences,
               'delta_rle': rle_encode(differences), 'lz4': compress(raw),
               'delta_lz4': compress(differences)}
    decoded = {'raw': raw, 'rle': rle_decode(encoded['rle']),
               'delta': undelta(differences),
               'delta_rle': undelta(rle_decode(encoded['delta_rle'])),
               'lz4': lz4.block.decompress(encoded['lz4'], uncompressed_size=len(raw)),
               'delta_lz4': undelta(lz4.block.decompress(encoded['delta_lz4'], uncompressed_size=len(raw)))}
    assert all(value == raw for value in decoded.values())
    return encoded


def pack_pcm(metadata, blobs):
    """Experimental PCM metadata + existing raw/LZ4 BLOKs; no runtime schema."""
    originals = [metadata, *blobs]
    directory, payload = bytearray(), bytearray()
    offset = 32 + 32 * len(originals)
    crc = lambda data: zlib.crc32(data) & 0xffffffff
    for index, raw in enumerate(originals):
        packed = compress(raw)
        # Keep metadata raw to match the explicit accounting model.
        codec_id = int(index > 0 and len(packed) < len(raw))
        stored = packed if codec_id else raw
        directory.extend(struct.pack('<8I', index + 1, 0x4b4f4c42, codec_id,
                                     offset, len(stored), len(raw), crc(stored), crc(raw)))
        payload.extend(stored)
        offset += len(stored)
    header = struct.pack('<4sHH5I', b'JPAK', 1, 32, offset, len(originals), 32, 32, crc(directory))
    return header + struct.pack('<I', crc(header)) + directory + payload


def measure(pack_path=None, verifier=None):
    blobs, references, songs, hashes = [], [], [], {}
    for track, key in enumerate(KEYS):
        source = (ROOT / 'content/music' / (key + '.mod')).read_bytes()
        hashes[key] = hashlib.sha256(source).hexdigest()
        doc = codec.decode(source)
        spans = sum(map(len, compile_song(doc)))
        song_blobs = set()
        for slot, sample in enumerate(doc['samples']):
            raw = bytes(value & 255 for value in sample['pcm'])
            if not raw:
                continue
            if raw not in blobs:
                blobs.append(raw)
            blob = blobs.index(raw)
            song_blobs.add(blob)
            record = (track, slot, blob, len(raw), sample['loop_start'], sample['loop_length'], sample['volume'], 0)
            assert struct.unpack('<8I', struct.pack('<8I', *record)) == record
            assert [v if v < 128 else v - 256 for v in blobs[blob]] == sample['pcm']
            references.append(record)
        songs.append({'key': key, 'mod_bytes': len(source), 'pcm_bytes': sum(len(s['pcm']) for s in doc['samples']),
                      'unique_resident_pcm_bytes': sum(len(blobs[i]) for i in song_blobs), 'spans': spans})
    metadata = struct.pack('<4I', 1, len(KEYS), len(references), len(blobs)) + b''.join(struct.pack('<8I', *r) for r in references)
    if pack_path is not None:
        if verifier is None:
            raise ValueError('--pack requires --verifier for native byte comparison')
        data = pack_pcm(metadata, blobs)
        pack_path = Path(pack_path)
        pack_path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=pack_path.parent) as temporary:
            candidate = Path(temporary) / 'candidate.jpak'
            candidate.write_bytes(data)
            native_verify(verifier, candidate, [(i + 1, raw) for i, raw in enumerate([metadata, *blobs])])
            candidate.replace(pack_path)
    rows = [{'id': i, 'raw_bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
             'sizes': {name: len(data) for name, data in candidates(raw).items()}}
            for i, raw in enumerate(blobs)]
    variants = {}
    for name in rows[0]['sizes']:
        payload = sum(r['sizes'][name] for r in rows)
        variants[name] = {'payload_bytes': payload, 'complete_model_bytes': 32 + 32 * (len(blobs) + 1) + len(metadata) + payload}
    for name, allowed in [('existing_best', ('raw', 'lz4')), ('experimental_best', tuple(variants))]:
        selected = [min(allowed, key=lambda c: r['sizes'][c]) for r in rows]
        payload = sum(r['sizes'][c] for r, c in zip(rows, selected))
        variants[name] = {'payload_bytes': payload, 'complete_model_bytes': 32 + 32 * (len(blobs) + 1) + len(metadata) + payload,
                          'codec_counts': {c: selected.count(c) for c in allowed}}
    grouped = candidates(b''.join(blobs))
    return {'scope': 'PCM-only modeled pack; metadata uncompressed; excludes timelines and linked-firmware measurement',
            'lz4_version': version('lz4'), 'source_sha256': hashes, 'tracks': songs, 'unique_samples': rows,
            'references': references, 'reference_metadata_bytes': len(metadata),
            'source_pcm_bytes': sum(s['pcm_bytes'] for s in songs), 'unique_pcm_bytes': sum(map(len, blobs)),
            'largest_sample_decode_bytes': max(map(len, blobs)),
            'max_song_resident_pcm_bytes': max(s['unique_resident_pcm_bytes'] for s in songs),
            'span_count': sum(s['spans'] for s in songs), 'independent_samples': variants,
            'single_pcm_block': {name: {'payload_bytes': len(data), 'complete_model_bytes': 32 + 64 + len(metadata) + len(data)} for name, data in grouped.items()}}


if __name__ == '__main__':
    for raw in (b'', b'\0' * 257, bytes(range(256)), b'\x7f\x80\xff\0' * 40):
        assert undelta(delta(raw)) == raw
        candidates(raw)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, help='emit experimental independent-sample JPAK')
    parser.add_argument('--verifier', help='existing native jelli_jellipak_verify binary')
    args = parser.parse_args()
    result = measure(args.pack, args.verifier)
    assert result == measure(), 'nondeterministic report'
    print(json.dumps(result, indent=2, sort_keys=True))

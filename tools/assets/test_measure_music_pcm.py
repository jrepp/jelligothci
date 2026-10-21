#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0", "lz4==4.4.5"]
# ///
"""Regression checks for the offline PCM experiment, not a runtime codec API."""
import random
import struct
import zlib
import lz4.block
import unittest

from measure_music_pcm import candidates, delta, undelta, measure, pack_pcm


class PackingTests(unittest.TestCase):
    def test_delta_wrap_and_signed_boundaries(self):
        raw = bytes((127, 128, 255, 0, 1))
        self.assertEqual(delta(raw), bytes((127, 1, 127, 1, 1)))
        self.assertEqual(undelta(delta(raw)), raw)

    def test_all_codecs_on_boundaries_and_noise(self):
        rng = random.Random(101)
        for length in (0, 1, 2, 127, 128, 129, 256, 1024):
            for raw in (bytes(length), bytes(rng.randrange(256) for _ in range(length)),
                        bytes(i & 255 for i in range(length))):
                with self.subTest(length=length, raw=raw[:4]):
                    self.assertEqual(candidates(raw)['raw'], raw)

    def test_serialized_pack_roundtrip(self):
        originals = [b'metadata', bytes(1024), bytes(range(128))]
        pack = pack_pcm(originals[0], originals[1:])
        self.assertEqual(len(pack), struct.unpack_from('<I', pack, 8)[0])
        self.assertEqual(zlib.crc32(pack[:28]), struct.unpack_from('<I', pack, 28)[0])
        for i, raw in enumerate(originals):
            ident, kind, codec, offset, stored, decoded, sc, rc = struct.unpack_from('<8I', pack, 32 + 32*i)
            self.assertEqual((ident, kind, decoded), (i + 1, 0x4b4f4c42, len(raw)))
            data = pack[offset:offset+stored]
            self.assertEqual(zlib.crc32(data), sc)
            self.assertEqual(zlib.crc32(raw), rc)
            self.assertEqual(lz4.block.decompress(data, uncompressed_size=decoded) if codec else data, raw)

    def test_catalog_accounting_and_determinism(self):
        result = measure()
        self.assertEqual(result, measure())
        blobs = result['unique_samples']
        self.assertEqual(result['unique_pcm_bytes'], sum(b['raw_bytes'] for b in blobs))
        overhead = 32 + 32 * (len(blobs) + 1) + result['reference_metadata_bytes']
        for variant in result['independent_samples'].values():
            self.assertEqual(variant['complete_model_bytes'], overhead + variant['payload_bytes'])
        for ref in result['references']:
            self.assertEqual(ref[3], blobs[ref[2]]['raw_bytes'])
            self.assertLessEqual(ref[4] + ref[5], ref[3])
            self.assertEqual(ref[7], 0)


if __name__ == '__main__':
    unittest.main()

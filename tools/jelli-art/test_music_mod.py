# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = []
# ///
"""Standard MOD round trips and bounded hostile input rejection."""
import copy
from pathlib import Path
import struct
import shutil
import subprocess
import unittest
import music_mod as mod

ROOT = Path(__file__).resolve().parents[2]


class ModTests(unittest.TestCase):
    def setUp(self):
        self.raw = (ROOT/'content/music/morning-tea.mod').read_bytes()
        self.doc = mod.decode(self.raw)

    def test_all_builtins_round_trip_four_channels(self):
        files = list((ROOT/'content/music').glob('*.mod'))
        from music_data import CATALOG
        self.assertEqual({p.stem for p in files}, {song[0] for song in CATALOG})
        for path in files:
            raw = path.read_bytes()
            doc = mod.decode(raw)
            self.assertEqual(mod.encode(doc), raw)
            self.assertEqual(raw[1080:1084], b'M.K.')
            self.assertEqual(len(doc['samples']), 31)
            for channel in range(4):
                self.assertTrue(any(row[channel]['period'] for p in doc['patterns'] for row in p),
                                f'{path.name} channel {channel} is silent')

    @unittest.skipUnless(shutil.which('openmpt123'), 'Optional independent libopenmpt decoder')
    def test_independent_libopenmpt_reads_and_renders_all_builtins(self):
        for path in (ROOT/'content/music').glob('*.mod'):
            with self.subTest(module=path.name):
                info = subprocess.run(['openmpt123', '--info', '--banner', '0', str(path)],
                                      capture_output=True, check=True, text=True, timeout=15)
                self.assertIn('ProTracker MOD (M.K.)', info.stdout+info.stderr)
                self.assertIn('Channels...: 4', info.stdout+info.stderr)
                pcm = subprocess.run(['openmpt123', '--batch', '--stdout', '--quiet',
                                      '--no-float', '--samplerate', '22050', '--channels', '1',
                                      '--end-time', '1', str(path)], capture_output=True,
                                     check=True, timeout=15).stdout
                self.assertGreaterEqual(len(pcm), 44100)
                self.assertLess(len(pcm), 50000)
                self.assertTrue(any(pcm))

    def test_reject_binary_truncation_signature_and_finetune(self):
        for size in (0, 1083, 1084, len(self.raw)-1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                mod.decode(self.raw[:size])
        for offset, value in ((1080, 0), (950, 0), (950, 17), (952, 16), (44, 1), (45, 65)):
            raw = bytearray(self.raw)
            raw[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                mod.decode(bytes(raw))
        for raw in (self.raw+b'junk', bytes(131073), 'M.K.'):
            with self.assertRaises(ValueError):
                mod.decode(raw)

    def test_reject_bad_cells_and_shape(self):
        for field, values in {'sample': [True,-1,32], 'period': [True,1,857],
                              'effect': [True,1,11,16], 'param': [True,-1,256]}.items():
            for value in values:
                doc = copy.deepcopy(self.doc)
                doc['patterns'][0][1][0][field] = value
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    mod.encode(doc)
        for effect, param in ((0, 1), (12, 65), (15, 0)):
            doc = copy.deepcopy(self.doc)
            doc['patterns'][0][1][0].update(effect=effect,param=param)
            with self.assertRaises(ValueError):
                mod.encode(doc)
        for key, value in [('samples', []), ('orders', []), ('orders', [0]*17),
                           ('patterns', []), ('title', '\u00e9'), ('title', 'x'*21),
                           ('restart', True), ('restart', 128)]:
            doc = copy.deepcopy(self.doc)
            doc[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                mod.encode(doc)

    def test_reject_pcm_and_loop_bounds(self):
        for change in ({'pcm': [0]*8194}, {'pcm': [0]}, {'pcm': [128,0]},
                       {'pcm': [True,0]}, {'loop_start': 1}, {'loop_length': 2},
                       {'loop_length': 34}, {'loop_start': 2, 'loop_length': 32},
                       {'name': 'x'*23}, {'volume': 65}):
            doc = copy.deepcopy(self.doc)
            doc['samples'][0].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                mod.encode(doc)
        doc = copy.deepcopy(self.doc)
        for sample in doc['samples'][:5]:
            sample.update(pcm=[0]*8192,loop_start=0,loop_length=0)
        with self.assertRaises(ValueError):
            mod.encode(doc)

    def test_supported_effects_and_signed_pcm_round_trip(self):
        doc = copy.deepcopy(self.doc)
        doc['samples'][0].update(pcm=[-128,127,-1,0],loop_start=0,loop_length=4)
        for i, pair in enumerate(((12,0),(12,64),(15,1),(15,31),(15,32),(15,255))):
            doc['patterns'][0][i][0].update(effect=pair[0],param=pair[1])
        self.assertEqual(mod.decode(mod.encode(doc)), doc)
        # Conventional one-word disabled loops normalize to editor length zero.
        self.assertEqual(struct.unpack_from('>H', self.raw, 20+2*30+28)[0], 1)
        self.assertEqual(self.doc['samples'][2]['loop_length'], 0)


if __name__ == '__main__':
    unittest.main()

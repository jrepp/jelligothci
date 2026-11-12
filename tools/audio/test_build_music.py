# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = []
# ///
"""Known tracker timing, loop, C-volume and four-channel mixing semantics."""
import struct
import hashlib
from pathlib import Path
import unittest
from unittest.mock import patch
from build_music import compile_song, render_pcm
from music_samples import sample_bank, tracker_presets


def module():
    empty = dict(sample=0, period=0, effect=0, param=0)
    rows = [[dict(empty) for _ in range(4)] for _ in range(64)]
    for ch in range(4):
        rows[0][ch] = dict(sample=1, period=856, effect=0, param=0)
    samples = [dict(name='', volume=64, loop_start=0, loop_length=4,
                    pcm=[-128, -128, -128, -128]) for _ in range(31)]
    return dict(title='', restart=0, orders=[0], samples=samples, patterns=[rows])


class SampleBankTests(unittest.TestCase):
    def test_authoring_presets_render_with_their_loop_contract(self):
        rendered = set()
        for sample in tracker_presets():
            with self.subTest(sample=sample['name']):
                doc = module()
                doc['samples'][0] = sample
                for cell in doc['patterns'][0][0][1:]:
                    cell.update(sample=0, period=0)
                doc['patterns'][0][0][0]['period'] = 214
                with patch('build_music.LENGTH', 22050):
                    pcm = render_pcm(doc)
                self.assertTrue(any(pcm[:4410]))
                self.assertEqual(any(pcm[-200:]), bool(sample['loop_length']))
                rendered.add(hashlib.sha256(pcm).digest())
        self.assertEqual(len(rendered), len(tracker_presets()))

    def test_legacy_samples_and_budget(self):
        leads, bank = sample_bank()
        legacy = leads + [bank[name] for name in ('keys', 'bass', 'brush', 'kick', 'snare')]
        raw = bytes(value & 255 for voice in legacy for value in voice)
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '8cc24a47f92c15189cfb3e78fce55c0292739da28a7c35579076396b74bcff10')
        self.assertEqual((leads, bank), sample_bank())
        self.assertLess(sum(map(len, leads)) + sum(map(len, bank.values())), 4096)
        for voice in leads + list(bank.values()):
            self.assertTrue(all(-128 <= value <= 127 for value in voice))
            self.assertEqual(len(voice) % 2, 0)  # MOD word lengths.

    def test_workout_exposes_all_new_samples_in_used_mod_slots(self):
        import create_workout_music
        import music_mod
        doc = music_mod.decode(create_workout_music.compose())
        _, bank = sample_bank()
        expected = {'Tight drive kick': 'tight_kick', 'Metal hat': 'metal_hat',
                    'Clap': 'clap', 'Acid pulse bass': 'acid_bass', 'Sine sub bass': 'sub_bass'}
        played = {cell['sample'] for pattern in doc['patterns'] for row in pattern for cell in row}
        for name, key in expected.items():
            slot = next(i for i, sample in enumerate(doc['samples'], 1) if sample['name'] == name)
            self.assertIn(slot, played)
            self.assertEqual(doc['samples'][slot - 1]['pcm'], bank[key])
        # The shipped module also exposes the named voices through Studio's ordinary sample list.
        path = Path(__file__).resolve().parents[2] / 'content/music/iron-pulse.mod'
        shipped = music_mod.decode(path.read_bytes())
        self.assertTrue(set(expected) <= {sample['name'] for sample in shipped['samples']})

    def test_new_voice_envelopes_and_loop_seams(self):
        _, bank = sample_bank()
        for name in ('tight_kick', 'metal_hat', 'clap'):
            pcm = bank[name]
            self.assertEqual((pcm[0], pcm[-1]), (0, 0))
            self.assertGreater(max(map(abs, pcm)), 40)
        for name in ('acid_bass', 'sub_bass'):
            pcm = bank[name]
            self.assertEqual(sum(pcm), 0)
            self.assertGreater(max(map(abs, pcm)), 70)
            self.assertLessEqual(abs(pcm[-1] - pcm[0]),
                                 max(abs(a - b) for a, b in zip(pcm, pcm[1:])))


class TrackerTests(unittest.TestCase):
    def test_four_channels_loop_and_volume(self):
        doc = module()
        doc['patterns'][0][1][0].update(effect=12, param=0)
        spans = compile_song(doc)
        self.assertEqual(spans[0][1][0], 2646)  # six 20ms ticks
        self.assertEqual(spans[0][1][1], 2646 * spans[0][0][2])
        pcm = render_pcm(doc)
        self.assertEqual(struct.unpack_from('<h', pcm, 0)[0], -32768)
        self.assertEqual(struct.unpack_from('<h', pcm, 2646 * 2)[0], -24576)
        self.assertEqual(struct.unpack_from('<h', pcm, 4000 * 2)[0], -24576)

    def test_speed_tempo_and_note_restart(self):
        doc = module()
        rows = doc['patterns'][0]
        rows[0][2].update(effect=15, param=1)
        rows[0][3].update(effect=15, param=255)
        rows[1][0].update(period=428)
        spans = compile_song(doc)
        self.assertEqual(spans[0][1][0], 216)
        self.assertEqual(spans[0][1][1], 0)
        self.assertLessEqual(max(map(len, spans)), 3061)

    def test_protracker_first_pass_loop_at_zero(self):
        doc = module()
        doc['samples'][0]['pcm'] = [-128] * 4 + [127] * 4
        with patch('build_music.LENGTH', 1000):
            pcm = render_pcm(doc)
        # Four source bytes pass after roughly22 output frames. The positive
        # tail plays once, then the first four negative bytes loop forever.
        self.assertEqual(struct.unpack_from('<h', pcm, 30 * 2)[0], 32512)
        self.assertEqual(struct.unpack_from('<h', pcm, 60 * 2)[0], -32768)
        doc['samples'][0].update(loop_start=2, loop_length=4)
        doc['samples'][0]['pcm'] = [-128] * 6 + [127] * 2
        with patch('build_music.LENGTH', 1000):
            pcm = render_pcm(doc)
        self.assertTrue(all(value[0] == -32768 for value in struct.iter_unpack('<h', pcm)))

    def test_nonlooping_sample_ends(self):
        doc = module()
        doc['samples'][0].update(loop_length=0)
        pcm = render_pcm(doc)
        self.assertEqual(struct.unpack_from('<h', pcm, 100 * 2)[0], 0)
        with self.assertRaises(ValueError):
            render_pcm(doc, [65, 64, 64, 64])


if __name__ == '__main__':
    unittest.main()

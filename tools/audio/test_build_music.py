"""Known tracker timing, loop, C-volume and four-channel mixing semantics."""
import struct
import unittest
from unittest.mock import patch
from build_music import compile_song, render_pcm


def module():
    empty = dict(sample=0, period=0, effect=0, param=0)
    rows = [[dict(empty) for _ in range(4)] for _ in range(64)]
    for ch in range(4):
        rows[0][ch] = dict(sample=1, period=856, effect=0, param=0)
    samples = [dict(name='', volume=64, loop_start=0, loop_length=4,
                    pcm=[-128, -128, -128, -128]) for _ in range(31)]
    return dict(title='', restart=0, orders=[0], samples=samples, patterns=[rows])


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

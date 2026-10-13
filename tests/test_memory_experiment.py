"""Reject measurements from the wrong physical memory or an incomplete copy trial."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
from memory_experiment import validate


def sample():
    return dict(ok=True, mode=2, rows=8, draw_external=False, draw_psram=False,
                engine_external=True, canvas_external=True, stage_internal=True, stage_dma=True,
                draw_stride=88, draw_bytes=7456, internal_free=24000, internal_largest=12000,
                internal_min=23000, psram_free=7000000, pairs=8, bytes=[72, 932, 46592],
                stage_bytes=46600, us=[[4, 10, 100], [4, 10, 100], [4, 10, 100], [4, 10, None]])


class MemoryTests(unittest.TestCase):
    def test_placement_and_headroom(self):
        validate(sample(), 2)
        for key, value in (('ok', False), ('mode', 1), ('draw_external', True), ('draw_psram', True),
                           ('stage_dma', False), ('rows', 50), ('internal_free', 0), ('draw_bytes', 1024)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate(dict(sample(), **{key: value}), 2)

    def test_failed_or_incomplete_timing(self):
        for path, size, value in ((0, 0, 0), (1, 1, True), (2, 2, None), (3, 2, 100)):
            broken = copy.deepcopy(sample())
            broken['us'][path][size] = value
            with self.subTest(path=path, size=size), self.assertRaises(ValueError):
                validate(broken, 2)


if __name__ == '__main__':
    unittest.main()

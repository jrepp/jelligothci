"""Reject failed correctness or invalid rectangle timing evidence."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
from rect_simd_experiment import validate


class RectSimdTests(unittest.TestCase):
    def test_incomplete_or_failed_results(self):
        good = dict(ok=True, pairs=8, small_us=[10, 9], full_us=[100, 50], edge_us=[20, 19])
        validate(good)
        for key, value in [('ok', False), ('pairs', 0), ('small_us', []),
                           ('full_us', [0, 1]), ('edge_us', [1, True]), ('edge_us', [1])]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate(good | {key: value})


if __name__ == '__main__':
    unittest.main()

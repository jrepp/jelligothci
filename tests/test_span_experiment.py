"""Reject invalid correctness and timing evidence before deriving speedups."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
from span_experiment import validate


class SpanTests(unittest.TestCase):
    def test_reject_incomplete_or_failed_evidence(self):
        good = dict(ok=True, pairs=8, repeats=16, alpha=7,
                    endpoint_pixels=466, endpoint_alpha=[0, 16], endpoint_us=[[12, 1]] * 4,
                    counts=[[6, 36, 466, 512], [6, 36, 466, 23300]], us=[[12, 10] for _ in range(16)])
        validate(good)
        for key, value in [('ok', False), ('endpoint_us', []), ('endpoint_alpha', [1, 16]), ('pairs', 0), ('alpha', 16), ('counts', []),
                           ('us', []), ('us', [[1, True]] * 16), ('us', [[0, 1]] * 16),
                           ('us', [[1]] * 16), ('us', [[1, -1]] * 16)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate(good | {key: value})


if __name__ == '__main__':
    unittest.main()

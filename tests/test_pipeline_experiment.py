"""Reject stale and inconsistent pipeline measurements."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
import pipeline_experiment as pipeline


def result():
    return dict(ok=True, run=2, boot=7, active=False, expired=False, stage='copy', count=150, window=128,
                p50_us=20, p95_us=30, max_us=40, total_us=3000, bytes=4096)


class PipelineTests(unittest.TestCase):
    def test_invalid_distribution_and_stale_runs(self):
        pipeline.validate(result(), 2, 'copy', 7)
        for key, value in [('expired', True), ('boot', 8), ('run', 1), ('active', True), ('stage', 'swap'), ('ok', False),
                           ('count', -1), ('window', 129), ('window', 100),
                           ('p50_us', 31), ('p95_us', 41), ('total_us', 39), ('bytes', True)]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                pipeline.validate(dict(result(), **{key: value}), 2, 'copy', 7)

    def test_empty_and_short_windows(self):
        pipeline.validate(dict(result(), count=0, window=0, p50_us=0, p95_us=0,
                               max_us=0, total_us=0, bytes=0), 2, 'copy', 7)
        pipeline.validate(dict(result(), count=10, window=10), 2, 'copy', 7)


if __name__ == '__main__':
    unittest.main()

"""Synthetic touch evidence must reject stale or physically contaminated trials."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
import touch_experiment as touch


def result():
    return dict(run=1, status='complete', mode=2, period_ms=16, consumed=8,
                synthetic=True, restored_period_ms=33, notify_to_read_us=[12000] * 8,
                taps=1, false_taps=0)


class TouchExperimentTests(unittest.TestCase):
    def test_guard_requires_recovery_without_false_taps(self):
        touch.validate(result(), 1, 'guard', 16)
        for field, value in [('false_taps', 1), ('taps', 0), ('run', 2),
                             ('status', 'contaminated'), ('status', 'timeout'),
                             ('consumed', 7), ('restored_period_ms', 10),
                             ('notify_to_read_us', [-1] * 8)]:
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                touch.validate(dict(result(), **{field: value}), 1, 'guard', 16)

    def test_control_is_expected_to_reproduce_failure(self):
        control = dict(result(), mode=0, period_ms=33, taps=2, false_taps=1)
        touch.validate(control, 1, 'legacy', 33)
        with self.assertRaises(ValueError):
            touch.validate(dict(control, taps=1, false_taps=0), 1, 'legacy', 33)


if __name__ == '__main__':
    unittest.main()

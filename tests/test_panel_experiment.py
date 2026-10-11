"""Reject uncorrelated restoration and unsupported physical/power claims."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
import panel_experiment as panel


def result():
    return dict(run=2, boot=7, mode='sleep', status='complete', error=0, restore_error=0,
                faulted=False, brightness_before=60, brightness_requested=60,
                brightness_restore_requested=60, entry_us=120000, hold_us=200000,
                recovery_us=120000, refresh_requested=True, physical_verified=False,
                current_measured=False)


class PanelExperimentTests(unittest.TestCase):
    def test_restore_is_narrowly_validated(self):
        panel.validate(result(), dict(run=2, boot=7), 'sleep', 60)
        for key, value in [('run', 1), ('boot', 8), ('error', 1), ('restore_error', 1),
                           ('faulted', True), ('brightness_restore_requested', 59),
                           ('hold_us', 0), ('recovery_us', -1), ('physical_verified', True),
                           ('current_measured', True), ('refresh_requested', False)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                panel.validate(dict(result(), **{key: value}), dict(run=2, boot=7), 'sleep', 60)

    def test_brightness_treatment_must_match_requested_level(self):
        reply = dict(result(), mode='dim30', brightness_requested=30)
        panel.validate(reply, dict(run=2, boot=7), 'dim30', 60)
        with self.assertRaises(ValueError):
            panel.validate(dict(reply, brightness_requested=29), dict(run=2, boot=7), 'dim30', 60)


if __name__ == '__main__':
    unittest.main()

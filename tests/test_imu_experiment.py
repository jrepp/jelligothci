"""Reject false motion, stale results and incomplete sensor cleanup."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
from imu_experiment import BASELINE, ARMED, validate


class ImuTests(unittest.TestCase):
    def result(self):
        return dict(boot=1, run=2, mode='motion', status='complete', error=0,
                    restore_error=0, restored=True, faulted=False, who=5, revision=124,
                    before=BASELINE, after=BASELINE, armed=ARMED, commands=3,
                    ack_status=0, mcu_slept=False, current_measured=False,
                    firmware='010203', usid='010203040506', elapsed_us=2000000,
                    initial_level=0, observed_level=1, status1=4, status_valid=True)

    def test_motion_needs_pin_and_status(self):
        reply = self.result()
        self.assertEqual(validate(reply, {'boot': 1, 'run': 2}, 'motion'), 'motion_observed')
        for key, value in [('observed_level', 0), ('status1', 0), ('status_valid', False),
                           ('initial_level', 1), ('armed', BASELINE), ('after', ARMED),
                           ('boot', 3), ('run', 1), ('restore_error', 1), ('ack_status', 128)]:
            bad = reply | {key: value}
            with self.assertRaises(ValueError):
                validate(bad, {'boot': 1, 'run': 2}, 'motion')

    def test_int2_comparator_is_explicit(self):
        reply = self.result() | dict(mode='motion-int2', armed='30' + ARMED[2:])
        self.assertEqual(validate(reply, {'boot': 1, 'run': 2}, 'motion-int2'), 'motion_observed')
        with self.assertRaises(ValueError):
            validate(reply | {'armed': ARMED}, {'boot': 1, 'run': 2}, 'motion-int2')

    def test_quiet_needs_full_interval(self):
        reply = self.result() | dict(mode='quiet', observed_level=0, status1=0)
        self.assertEqual(validate(reply, {'boot': 1, 'run': 2}, 'quiet'), 'quiet_pass')
        for key, value in [('status1', 4), ('observed_level', 1), ('elapsed_us', 500000)]:
            with self.assertRaises(ValueError):
                validate(reply | {key: value}, {'boot': 1, 'run': 2}, 'quiet')


if __name__ == '__main__':
    unittest.main()

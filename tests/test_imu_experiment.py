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

    def test_rearm_needs_both_events_and_idle_restoration(self):
        reply = self.result() | dict(mode='rearm-int2', armed='30' + ARMED[2:],
                                    elapsed_us=40000000, completed_events=2,
                                    motion_levels=[1, 1], motion_status=[4, 4],
                                    ack_levels=[1, 0], idle_levels=[0, 0],
                                    ack_wait_us=[120000, 140000])
        self.assertEqual(validate(reply, {'boot': 1, 'run': 2}, 'rearm-int2'), 'rearm_observed')
        for key, value in [('completed_events', 1), ('completed_events', 2.0), ('motion_levels', [1, 0]),
                           ('motion_status', [4, 0]), ('motion_status', [4, 260]),
                           ('idle_levels', [0, 1]), ('ack_levels', [0, 2]),
                           ('ack_wait_us', [99999, 140000]), ('ack_wait_us', [1000000, 140000]),
                           ('ack_wait_us', [120000]), ('motion_levels', [True, 1]),
                           ('elapsed_us', 55000000), ('restore_error', 1)]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate(reply | {key: value}, {'boot': 1, 'run': 2}, 'rearm-int2')

    def test_sleep_requires_correlated_wake_and_clean_restoration(self):
        reply = self.result() | dict(mode='sleep-int2', armed='30' + ARMED[2:],
                                    mcu_slept=True, sleep_attempted=True, already_active=False,
                                    wake_cleanup_error=0, host_restore_error=0,
                                    wake_cause=7, wake_level=1, sleep_us=3000000)
        accepted = {'boot': 1, 'run': 2}
        self.assertEqual(validate(reply, accepted, 'sleep-int2'), 'gpio_motion_wake')
        for key, value in [('wake_cause', 4), ('wake_level', 0), ('status1', 0),
                           ('sleep_attempted', False), ('already_active', True),
                           ('wake_cleanup_error', 1), ('host_restore_error', 1),
                           ('mcu_slept', False), ('sleep_us', 0), ('sleep_us', True),
                           ('sleep_us', 12000000), ('boot', 2), ('restore_error', 1)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate(reply | {key: value}, accepted, 'sleep-int2')
        timer = reply | dict(wake_cause=4, wake_level=0, status1=0, sleep_us=10000100)
        self.assertEqual(validate(timer, accepted, 'sleep-int2'), 'timer_fallback')
        for key, value in [('sleep_us', 5000000), ('status1', 4), ('wake_level', 1)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate(timer | {key: value}, accepted, 'sleep-int2')


if __name__ == '__main__':
    unittest.main()

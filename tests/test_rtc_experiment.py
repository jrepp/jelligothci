"""Reject plausible but invalid RTC progression transcripts."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
from rtc_experiment import calendar, compare


class RtcTests(unittest.TestCase):
    def sample(self, second, run, start):
        return {'status': {'schema': 3, 'board': 'waveshare-31261',
                          'profile': 'factory-smoke', 'boot': 1, 'device': '907069fe21dc',
                          'elf_sha256': 'a' * 64, 'run': run},
                'start': start, 'end': start + .1,
                'rtc': {'samples': [[r, v] for r, v in enumerate(
                    [0, 0, 0, 0, second | 128, 0, 0, 1, 6, 1, 0])] + [[17, 24]]}}

    def test_ticking_does_not_grant_trust(self):
        result = compare(self.sample(0, 1, 0), self.sample(2, 2, 2.1))
        self.assertTrue(result['oscillator_stop'])
        self.assertEqual(result['utc_trust'], 'not_established')
        self.assertFalse(result['acceptance'])

    def test_bad_progression_and_identity(self):
        before = self.sample(0, 1, 0)
        for second in (0, 5):
            with self.assertRaises(ValueError):
                compare(before, self.sample(second, 2, 2.1))
        for field, value in [('boot', 2), ('run', 1), ('device', 'a' * 12)]:
            after = self.sample(2, 2, 2.1)
            after['status'][field] = value
            with self.assertRaises(ValueError):
                compare(before, after)

    def test_invalid_calendar_and_modes(self):
        for register, value in [(4, 0x8a), (9, 0), (7, 0x32), (0, 0x20), (0, 2)]:
            sample = copy.deepcopy(self.sample(0, 1, 0)['rtc'])
            sample['samples'][register][1] = value
            with self.assertRaises(ValueError):
                calendar(sample)

    def test_calendar_rollover(self):
        before = self.sample(0x59, 1, 0)
        after = self.sample(1, 2, 2.1)
        after['rtc']['samples'][5][1] = 1
        self.assertEqual(compare(before, after)['rtc_seconds'], 2)


if __name__ == '__main__':
    unittest.main()

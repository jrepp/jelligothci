"""Reject confounded cache experiments before device deployment."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
from check_cache_trial import VARIANTS, check_settings, compare, expected_cache, read_config


class CacheTrialTests(unittest.TestCase):
    def config(self, variant):
        return {**expected_cache(variant), 'CONFIG_JELLI_POWER_EXPERIMENTS': 'y',
                'CONFIG_JELLI_MEMORY_TRIAL': '0', 'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ': '160'}

    def test_disabled_and_hex(self):
        parsed = read_config('# CONFIG_DISABLED is not set\nCONFIG_SIZE=0x4000\n')
        self.assertEqual(parsed, {'CONFIG_DISABLED': 'n', 'CONFIG_SIZE': '0x4000'})
        baseline = self.config('baseline')
        baseline['CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE'] = '0x4000'
        check_settings(baseline, 'baseline')

    def test_each_variant(self):
        for variant in VARIANTS:
            candidate = self.config(variant)
            check_settings(candidate, variant)
            compare(self.config('baseline'), candidate, expected_cache(variant))

    def test_wrong_cache_selection(self):
        with self.assertRaises(ValueError):
            check_settings(self.config('baseline'), 'combined')

    def test_clock_drift(self):
        candidate = self.config('icache')
        candidate['CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ'] = '240'
        with self.assertRaises(ValueError):
            compare(self.config('baseline'), candidate, expected_cache('icache'))

    def test_memory_trial_rejected(self):
        candidate = self.config('baseline')
        candidate['CONFIG_JELLI_MEMORY_TRIAL'] = '2'
        with self.assertRaises(ValueError):
            check_settings(candidate, 'baseline')

    def test_baseline_must_match_reference(self):
        with self.assertRaises(ValueError):
            compare(self.config('baseline'), self.config('line64'))

    def test_dependency_hidden_disabled_symbol(self):
        # IDF omits the unavailable 16-byte instruction-line choice at 32 KiB.
        compare({'CONFIG_CHOICE': 'n'}, {})
        with self.assertRaises(ValueError):
            compare({}, {'CONFIG_CHOICE': 'y'})


if __name__ == '__main__':
    unittest.main()

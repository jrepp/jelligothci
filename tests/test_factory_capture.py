"""Station validation fails closed; factory capture uses the existing @J1 client."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/debug'))
import factory_capture as factory


def snapshot():
    state = {'ok': True, 'schema': 3, 'run': 1, 'boot': 7, 'board': 'waveshare-31261',
             'profile': 'factory-smoke', 'device': '907069fe21dc', 'elf_sha256': 'a' * 64,
             'status': 'incomplete', 'scope': 'all', 'passed': 6, 'errors': 0,
             'skipped': 4, 'pending': 0, 'acceptance': False}
    results = []
    for name in sorted(factory.EXPECTED):
        status = 'pass' if name in ('identity', 'psram.scratch', 'pmic.read', *factory.INVENTORIES) else 'skip'
        item = {'ok': True, 'run': 1, 'boot': 7, 'id': name, 'status': status,
                'error': 0 if status == 'pass' else 262, 'elapsed_us': 10}
        if name == 'pmic.read':
            item.update(registers=[3, 38, 128, 144], values=[74, 8, 15, 255])
        if name in factory.INVENTORIES:
            address, registers = factory.INVENTORIES[name]
            item.update(address=address, valid=len(registers),
                        samples=[[reg, 5 if name == 'imu.identity' and reg == 0 else 0]
                                 for reg in registers])
        results.append(item)
    return state, results


class Station:
    def __init__(self):
        self.calls = []
        self.state, self.results = snapshot()
        self.started = False

    def request(self, command):
        self.calls.append(command)
        if command == 'capabilities':
            return {'protocol': 1, 'profile': 'factory-smoke', 'factory_schema': 3, 'board': 'waveshare-31261'}
        if command == 'factory tests':
            return {'tests': [{'id': r['id'], 'supported': r['status'] == 'pass', 'required': True} for r in self.results]}
        if command == 'factory run':
            self.started = True
            return dict(self.state, status='running')
        if command == 'factory status':
            return dict(self.state) if self.started else dict(self.state, run=0, status='idle')
        if command.startswith('factory result 1 '):
            return next(dict(r) for r in self.results if r['id'] == command.split()[-1])
        raise AssertionError(command)


class FactoryCaptureTests(unittest.TestCase):
    def test_skips_never_accept(self):
        self.assertEqual(factory.validate(*snapshot()), 2)

    def test_malformed_results_fail_closed(self):
        good_state, good_results = snapshot()
        variants = [(good_state, good_results[:-1]), (good_state, good_results + [good_results[0]])]
        for key, value in (('schema', 1), ('board', 'other'), ('acceptance', True), ('passed', 7)):
            variants.append((dict(good_state, **{key: value}), good_results))
        for key, value in (('run', 2), ('boot', 8), ('status', 'pending')):
            bad = copy.deepcopy(good_results)
            bad[0][key] = value
            variants.append((good_state, bad))
        for state, results in variants:
            with self.subTest(state=state, results=results), self.assertRaises(ValueError):
                factory.validate(state, results)

    def test_inventory_rejects_partial_or_false_identity(self):
        for field, value in (('valid', 8), ('address', 0x6a),
                             ('samples', [[r, 0] for r in range(9)])):
            state, results = snapshot()
            next(r for r in results if r['id'] == 'imu.identity')[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                factory.validate(state, results)

    def test_partial_inventory_error_retains_evidence(self):
        state, results = snapshot()
        item = next(r for r in results if r['id'] == 'rtc.snapshot')
        item.update(status='error', error=263, valid=11)
        state.update(status='error', errors=1, passed=5)
        self.assertEqual(factory.validate(state, results), 1)

    def test_observations_preserve_rtc_uncertainty(self):
        _, results = snapshot()
        rtc = next(r for r in results if r['id'] == 'rtc.snapshot')
        rtc['samples'][4][1] = 0x80
        report = factory.observations(results)
        self.assertTrue(report['rtc']['oscillator_stop'])
        self.assertEqual(report['rtc']['utc_trust'], 'not_evaluated')
        rtc.update(status='error', valid=4)
        self.assertNotIn('rtc', factory.observations(results))

    def test_hardware_error_is_failure(self):
        state, results = snapshot()
        next(r for r in results if r['id'] == 'pmic.read').update(status='error', error=263)
        state.update(status='error', errors=1, passed=5)
        self.assertEqual(factory.validate(state, results), 1)

    def test_capture_queries_retained_results_and_never_retries_run(self):
        station = Station()
        self.assertEqual(factory.capture(station.request)[0], 2)
        self.assertEqual(station.calls.count('factory run'), 1)
        self.assertEqual(sum(c.startswith('factory result ') for c in station.calls), 10)

    def test_wrong_profile_does_not_run(self):
        calls = []
        def request(command):
            calls.append(command)
            return {'profile': 'game'}
        with self.assertRaises(ValueError):
            factory.capture(request)
        self.assertEqual(calls, ['capabilities'])

    def test_reboot_during_capture_rejects_snapshot(self):
        station = Station()
        def request(command):
            reply = station.request(command)
            if station.started and command == 'factory status':
                reply['boot'] = 8
            return reply
        with self.assertRaises(ValueError):
            factory.capture(request)
        self.assertEqual(station.calls.count('factory run'), 1)


if __name__ == '__main__':
    unittest.main()

# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Run factory checks through the shared @J1 client; exit 2 is incomplete, never accepted."""
import argparse
import json
from pathlib import Path
import re
import time
import uuid
from jelli_debug import Client, RecordedWire, open_serial

EXPECTED = {'identity', 'psram.scratch', 'pmic.read', 'display.visible',
            'touch.physical', 'audio.audible', 'power.current',
            'pmic.inventory', 'imu.identity', 'rtc.snapshot'}
INVENTORIES = {
    'pmic.inventory': (0x34, [0, 1, 3, 0x18, 0x20, 0x21, 0x26, 0x30, 0x40,
                             0x41, 0x42, 0x48, 0x49, 0x4a, 0x61, 0x62, 0x63,
                             0x64, 0x80, 0x82, 0x90, 0x91, 0x92, 0x93, 0xa4]),
    'imu.identity': (0x6b, list(range(9))),
    'rtc.snapshot': (0x51, list(range(11)) + [17]),
}


def identity(state):
    if (state.get('schema') != 3 or state.get('board') != 'waveshare-31261'
            or state.get('profile') != 'factory-smoke'
            or type(state.get('boot')) is not int or not 0 <= state['boot'] <= 0xFFFFFFFF
            or not re.fullmatch('[0-9a-f]{12}', state.get('device', ''))
            or not re.fullmatch('[0-9a-f]{64}', state.get('elf_sha256', ''))):
        raise ValueError('Unsupported factory identity/schema')
    return state['boot'], state['device'], state['elf_sha256']


def validate(status, results):
    identity(status)
    if type(status.get('run')) is not int or not 0 < status['run'] <= 0xFFFFFFFF:
        raise ValueError('Invalid run')
    seen = {}
    for item in results:
        name, value = item.get('id'), item.get('status')
        if (item.get('boot') != status['boot'] or item.get('run') != status['run']
                or name not in EXPECTED or name in seen
                or value not in ('pass', 'error', 'skip')):
            raise ValueError('Stale, duplicate or invalid result')
        if type(item.get('elapsed_us')) is not int or item['elapsed_us'] < 0:
            raise ValueError('Invalid timing')
        if type(item.get('error')) is not int or (value == 'pass' and item['error'] != 0):
            raise ValueError('Contradictory result')
        if name == 'pmic.read':
            values = item.get('values', [])
            if (item.get('registers') != [3, 38, 128, 144] or len(values) != 4
                    or any(type(v) is not int or not 0 <= v <= 255 for v in values)):
                raise ValueError('Invalid measurement')
        if name in INVENTORIES:
            address, registers = INVENTORIES[name]
            samples, valid = item.get('samples'), item.get('valid')
            if (item.get('address') != address or type(valid) is not int
                    or not 0 <= valid <= len(registers) or not isinstance(samples, list)
                    or len(samples) != len(registers)
                    or any(not isinstance(pair, list) or len(pair) != 2
                           or type(pair[0]) is not int or pair[0] != reg
                           or type(pair[1]) is not int or not 0 <= pair[1] <= 255
                           for pair, reg in zip(samples, registers))
                    or (value == 'pass' and valid != len(registers))
                    or (value == 'pass' and name == 'imu.identity' and samples[0][1] != 5)):
                raise ValueError('Invalid inventory')
        seen[name] = value
    if set(seen) != EXPECTED:
        raise ValueError('Missing results')
    passed, errors, skipped = (sum(v == key for v in seen.values()) for key in ('pass', 'error', 'skip'))
    if (status.get('scope') != 'all' or status.get('pending') != 0
            or status.get('passed') != passed or status.get('errors') != errors
            or status.get('skipped') != skipped
            or status.get('status') != ('error' if errors else 'incomplete')
            or status.get('acceptance') is not False):
        raise ValueError('Invalid completion summary')
    return 1 if errors else 2


def observations(results):
    """Interpret only complete passing samples; never infer calibrated SOC or UTC."""
    report = {}
    for item in results:
        name = item.get('id')
        if name not in INVENTORIES or item.get('status') != 'pass':
            continue
        _, registers = INVENTORIES[name]
        if item.get('valid') != len(registers):
            continue
        values = dict(item['samples'])
        if name == 'rtc.snapshot':
            report['rtc'] = {'oscillator_stop': bool(values[4] & 0x80),
                             'clock_stopped': bool(values[0] & 0x20),
                             'external_test': bool(values[0] & 0x80),
                             'mode_12_hour': bool(values[0] & 2),
                             'utc_trust': 'not_evaluated',
                             'clkout_disabled': (values[1] & 7) == 7,
                             'timer_clock_1_per_minute': (values[17] & 0x18) == 0x18}
        elif name == 'imu.identity':
            report['imu'] = {'revision': values[1], 'accelerometer_enabled': bool(values[8] & 1),
                             'gyroscope_enabled': bool(values[8] & 2),
                             'oscillator_disabled': bool(values[2] & 1)}
        else:
            report['pmic'] = {'adc_enable_raw': values[0x30],
                              'gauge_raw': values[0xa4], 'gauge_accuracy': 'not_validated',
                              'rail_voltage_accuracy': 'not_measured'}
    return report


def capture(request, timeout=10):
    caps = request('capabilities')
    if (caps.get('protocol') != 1 or caps.get('profile') != 'factory-smoke'
            or caps.get('factory_schema') != 3 or caps.get('board') != 'waveshare-31261'):
        raise ValueError('Expected factory firmware; no run submitted')
    before = request('factory status')
    cookie = identity(before)
    if before.get('status') == 'running':
        raise ValueError('Existing run is active; inspect it before starting another')
    catalog = request('factory tests').get('tests', [])
    names = [test.get('id') for test in catalog]
    if (len(names) != len(EXPECTED) or set(names) != EXPECTED
            or any(type(t.get('supported')) is not bool or t.get('required') is not True for t in catalog)):
        raise ValueError('Unexpected test catalog')
    started = request('factory run')  # Never retry a state-changing command.
    if identity(started) != cookie or started.get('run') == before.get('run'):
        raise ValueError('Reboot or stale run acknowledgement')
    run = started.get('run')
    deadline = time.monotonic() + timeout
    while True:
        status = request('factory status')
        if identity(status) != cookie or status.get('run') != run:
            raise ValueError('Reboot or another station replaced the run')
        if status.get('status') != 'running':
            break
        if time.monotonic() >= deadline:
            raise TimeoutError('Factory run remains active; inspect state before retrying')
        time.sleep(.03)
    results = [request(f'factory result {run} {name}') for name in names]
    final = request('factory status')
    if final != status:
        raise ValueError('State changed while collecting results')
    return validate(final, results), final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', required=True, help='Station identifier, or bench-unattended')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = {'capture_id': str(uuid.uuid4()), 'fixture': args.fixture,
                'port': args.port, 'started': time.time(), 'acceptance': False}
    (args.output / 'capture.json').write_text(json.dumps(metadata, indent=2) + '\n')
    wire = open_serial(args.port)
    try:
        with (args.output / 'serial.log').open('wb') as raw, (args.output / 'results.jsonl').open('w') as out:
            client = Client(RecordedWire(wire, raw))

            measurements = []

            def request(command):
                reply = client.request(command)
                out.write(json.dumps({'command': command, 'reply': reply}) + '\n')
                out.flush()
                if command.startswith('factory result '):
                    measurements.append(reply)
                return reply

            code, status = capture(request)
            (args.output / 'observations.json').write_text(
                json.dumps(observations(measurements), indent=2) + '\n')
            print(json.dumps(status))
            return code
    finally:
        wire.close()


if __name__ == '__main__':
    raise SystemExit(main())

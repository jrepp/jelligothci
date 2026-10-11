# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Capture synthetic touch recovery through the real power-profile LVGL adapter."""
import argparse
import json
from pathlib import Path
import time
from jelli_debug import Client, RecordedWire, open_serial


def validate(result, run, mode, period):
    expected = {'legacy': (0, 2, 1), 'propagate': (1, 3, 2), 'guard': (2, 1, 0)}[mode]
    if (result.get('run') != run or result.get('status') != 'complete'
            or result.get('mode') != expected[0] or result.get('period_ms') != period
            or result.get('consumed') != 8 or result.get('synthetic') is not True
            or result.get('restored_period_ms') != 33):
        raise ValueError('Stale, incomplete, contaminated or unrestored trial')
    times = result.get('notify_to_read_us')
    if (not isinstance(times, list) or len(times) != 8
            or any(type(t) is not int or not 0 <= t <= 3000000 for t in times)):
        raise ValueError('Invalid timing samples')
    if (result.get('taps'), result.get('false_taps')) != expected[1:]:
        raise ValueError('Recovery hypothesis did not match observed events')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--repetitions', type=int, default=10, choices=range(1, 101))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    wire = open_serial(args.port)
    try:
        with (args.output / 'serial.log').open('wb') as raw, (args.output / 'results.jsonl').open('w') as log:
            client = Client(RecordedWire(wire, raw))
            def request(command):
                result = client.request(command)
                log.write(json.dumps({'time': time.time(), 'command': command, 'reply': result}) + '\n')
                log.flush()
                return result
            caps = request('capabilities')
            if (caps.get('profile') != 'power' or caps.get('protocol') != 1
                    or caps.get('board') != 'waveshare-31261'):
                raise ValueError('Expected power validation image')
            results = []
            for mode, period in [('legacy', 33), ('propagate', 33), ('guard', 33),
                                 ('guard', 16), ('guard', 10), ('guard', 33)]:
                for _ in range(args.repetitions):
                    started = request(f'power touch run {mode} {period}')
                    run = started['run']  # Do not retry an ambiguous action.
                    deadline = time.monotonic() + 5
                    while True:
                        result = request('power touch result')
                        if result.get('status') != 'running':
                            break
                        if time.monotonic() >= deadline:
                            raise TimeoutError('Trial did not finish; inspect before retrying')
                        time.sleep(.03)
                    validate(result, run, mode, period)
                    results.append(result)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
            print(f'{len(results)} synthetic trials matched expectations; physical touch unverified')
    finally:
        wire.close()


if __name__ == '__main__':
    main()

# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Check RTC progression using existing factory probes, without setting time."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import time
from factory_capture import capture, identity, observations
from jelli_debug import Client, RecordedWire, open_serial


def calendar(item):
    values = dict(item['samples'])
    if values[0] & 0xa2:
        raise ValueError('Stopped, test or 12-hour clock is unsupported')
    def bcd(register, mask):
        value = values[register] & mask
        if value & 15 > 9 or value >> 4 > 9:
            raise ValueError('Invalid BCD calendar')
        return (value >> 4) * 10 + (value & 15)
    return datetime(2000 + bcd(10, 255), bcd(9, 31), bcd(7, 63),
                    bcd(6, 63), bcd(5, 127), bcd(4, 127))


def compare(before, after):
    if identity(before['status']) != identity(after['status']):
        raise ValueError('Device, image or boot changed')
    if after['status']['run'] <= before['status']['run']:
        raise ValueError('Stale or wrapped run; restart capture')
    first, last = before['rtc'], after['rtc']
    a, b = dict(first['samples']), dict(last['samples'])
    if any(a[r] != b[r] for r in (0, 1, 2, 3, 17)) or (a[4] ^ b[4]) & 128:
        raise ValueError('RTC configuration or oscillator flag changed')
    elapsed = (calendar(last) - calendar(first)).total_seconds()
    lower = after['start'] - before['end']
    upper = after['end'] - before['start']
    if lower <= 1 or upper < lower or elapsed <= 0 or not lower - 1 <= elapsed <= upper + 1:
        raise ValueError('RTC did not progress within station bounds and one-second quantization')
    return {'rtc_seconds': elapsed, 'station_lower_seconds': lower,
            'station_upper_seconds': upper, 'oscillator_stop': bool(b[4] & 128),
            'progression': 'pass', 'utc_trust': 'not_established',
            'retention': 'not_tested', 'acceptance': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--interval', type=float, default=2, choices=(2, 5, 10))
    parser.add_argument('--repetitions', type=int, default=10, choices=range(1, 101))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    wire = open_serial(args.port)
    try:
        with (args.output / 'serial.log').open('wb') as raw, (args.output / 'results.jsonl').open('w') as log:
            client = Client(RecordedWire(wire, raw))
            samples = []
            def request(command):
                reply = client.request(command)
                log.write(json.dumps({'time': time.time(), 'command': command, 'reply': reply}) + '\n')
                log.flush()
                if command.startswith('factory result '):
                    samples.append(reply)
                return reply
            previous, results = None, []
            for _ in range(args.repetitions + 1):
                samples.clear()
                start = time.monotonic()
                code, status = capture(request)
                end = time.monotonic()
                if code != 2:
                    raise ValueError('Factory inventory failed')
                current = {'status': status, 'start': start, 'end': end,
                           'rtc': next(item for item in samples if item['id'] == 'rtc.snapshot')}
                calendar(current['rtc'])
                if previous is not None:
                    results.append(compare(previous, current))
                previous = current
                if len(results) < args.repetitions:
                    time.sleep(args.interval)
            summary = {'intervals': results, 'observations': observations(samples),
                       'fixture': 'USB-powered bench; no RTC writes', 'status': status}
            (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary))
    finally:
        wire.close()


if __name__ == '__main__':
    main()

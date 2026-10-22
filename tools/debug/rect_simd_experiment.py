# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["pyserial==3.5"]
# ///
"""Compare current clipped scanline rectangles with the power-only SIMD candidate."""
import argparse
import json
from pathlib import Path
import statistics
import time
from jelli_debug import Client, RecordedWire, open_serial

WORKLOADS = ('small_us', 'full_us', 'edge_us')


def validate(reply):
    if reply.get('ok') is not True or reply.get('pairs') != 8:
        raise ValueError('Pixel oracle or workload checksums failed')
    for key in WORKLOADS:
        values = reply.get(key)
        if not isinstance(values, list) or len(values) != 2 or any(type(v) is not int or v <= 0 for v in values):
            raise ValueError('Missing/invalid rectangle timings')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runs', type=int, choices=range(1, 11), default=5)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    wire = open_serial(args.port)
    try:
        with (args.output / 'serial.log').open('wb') as raw, (args.output / 'results.jsonl').open('w') as log:
            client = Client(RecordedWire(wire, raw), timeout=30)

            def request(command):
                reply = client.request(command)
                log.write(json.dumps({'time': time.time(), 'command': command, 'reply': reply}) + '\n')
                log.flush()
                return reply

            caps = request('capabilities')
            if caps.get('profile') != 'power' or caps.get('board') != 'waveshare-31261':
                raise ValueError('Expected SKU31261 power image')
            request('power')
            request('display')
            samples, controls = [], []
            for n in range(args.runs):
                for mode in (('simd', 'control') if n % 2 == 0 else ('control', 'simd')):
                    reply = request(f'display benchmark-{mode}-rect')
                    validate(reply)
                    (samples if mode == 'simd' else controls).append(reply)
                print(f'Run {n + 1}: exact clipping/dim oracle and workload checksums passed', flush=True)
            rows = []
            for key in WORKLOADS:
                medians = [statistics.median(s[key][m] for s in samples) / 8 for m in (0, 1)]
                control = statistics.median(s[key][1] for s in controls) / 8
                rows.append(dict(workload=key, current_us=medians[0], candidate_us=medians[1],
                                 control_us=control, speedup=medians[0] / medians[1],
                                 control_speedup=control / medians[1]))
            (args.output / 'summary.json').write_text(json.dumps(rows, indent=2) + '\n')
            request('display refresh')
            time.sleep(6)
            health = request('display')
            if (not health.get('checks') or health.get('mismatches') != 0 or health.get('transfer_failures') != 0
                    or any(health.get(k) is not True for k in ('buffers_equal', 'guards_ok', 'heap_ok'))):
                raise ValueError('Display recovery failed')
            print(json.dumps(rows, indent=2))
    finally:
        wire.close()


if __name__ == '__main__':
    main()

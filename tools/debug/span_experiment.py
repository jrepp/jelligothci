# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Measure power-profile scalar/SIMD fill and scalar/packed RGB565 blend."""
import argparse
import json
from pathlib import Path
import statistics
import time
from jelli_debug import Client, RecordedWire, open_serial


def validate(reply):
    if (reply.get('ok') is not True or reply.get('pairs') != 8 or reply.get('repeats') != 16
            or reply.get('alpha') != 7 or reply.get('counts') != [[6, 36, 466, 512], [6, 36, 466, 23300]]):
        raise ValueError('Incorrect pixels/guards or unexpected benchmark contract')
    samples = reply.get('us')
    if not isinstance(samples, list) or len(samples) != 16:
        raise ValueError('Missing timings')
    if (reply.get('endpoint_pixels') != 466 or reply.get('endpoint_alpha') != [0, 16]
            or not isinstance(reply.get('endpoint_us'), list) or len(reply['endpoint_us']) != 4):
        raise ValueError('Missing alpha endpoint timing')
    for pair in samples + reply['endpoint_us']:
        if not isinstance(pair, list) or len(pair) != 2 or any(type(t) is not int or t <= 0 for t in pair):
            raise ValueError('Invalid timing pair')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--runs', type=int, default=5, choices=range(1, 11))
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
                raise ValueError('Expected SKU31261 power firmware')
            for command in ('power', 'display', 'network'):
                request(command)
            samples = []
            for n in range(args.runs):
                reply = request('display benchmark-spans')  # Never retry an ambiguous action.
                validate(reply)
                samples.append(reply)
                print(f'Run {n + 1}: exact pixels and guards passed', flush=True)
            rows = []
            for memory, label in enumerate(('internal', 'psram')):
                for kind, kernel in enumerate(('fill_simd', 'blend_packed')):
                    for size, pixels in enumerate(samples[0]['counts'][memory]):
                        index = memory * 8 + kind * 4 + size
                        medians = [statistics.median(s['us'][index][m] for s in samples) / 128 for m in (0, 1)]
                        rows.append(dict(memory=label, kernel=kernel, pixels=pixels,
                                         scalar_us=medians[0], candidate_us=medians[1], speedup=medians[0] / medians[1]))
            for memory, label in enumerate(('internal', 'psram')):
                for a, alpha in enumerate((0, 16)):
                    medians = [statistics.median(s['endpoint_us'][memory * 2 + a][m] for s in samples) / 128 for m in (0, 1)]
                    rows.append(dict(memory=label, kernel='blend_endpoint', alpha=alpha, pixels=466,
                                     scalar_us=medians[0], candidate_us=medians[1], speedup=medians[0] / medians[1]))
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

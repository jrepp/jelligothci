# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Bounded transfer-copy and actual memory-class evidence on the power image."""
import argparse
import json
from pathlib import Path
import time
from jelli_debug import Client, RecordedWire, open_serial


def validate(reply, mode, measured=True):
    if reply.get('ok') is not True or reply.get('mode') != mode:
        raise ValueError('Memory trial failed or wrong image')
    external = mode != 2
    if (reply.get('draw_external') is not external or reply.get('draw_psram') is not external
            or reply.get('rows') != (50 if mode == 0 else 8)
            or any(reply.get(k) is not True for k in ('engine_external', 'canvas_external', 'stage_internal', 'stage_dma'))):
        raise ValueError('Unexpected physical memory placement')
    if (type(reply.get('draw_stride')) is not int or reply['draw_stride'] <= 0
            or type(reply.get('draw_bytes')) is not int
            or reply['draw_bytes'] < reply['rows'] * 932 or reply['draw_stride'] > reply['draw_bytes']):
        raise ValueError('Invalid draw-buffer dimensions')
    for key in ('internal_free', 'internal_largest', 'internal_min', 'psram_free'):
        if type(reply.get(key)) is not int or reply[key] <= 0:
            raise ValueError('Invalid memory headroom')
    if not measured:
        return
    if reply.get('pairs') != 8 or reply.get('bytes') != [72, 932, 46592] or reply.get('stage_bytes') != 46600:
        raise ValueError('Unexpected copy workload')
    rows = reply.get('us')
    if not isinstance(rows, list) or len(rows) != 4:
        raise ValueError('Missing memory timing paths')
    for path, row in enumerate(rows):
        if not isinstance(row, list) or len(row) != 3:
            raise ValueError('Missing sizes')
        for size, value in enumerate(row):
            if path == 3 and size == 2:
                if value is not None:
                    raise ValueError('Unsupported internal strip measurement')
            elif type(value) is not int or value <= 0:
                raise ValueError('Invalid copy timing')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--mode', type=int, choices=(0, 1, 2), required=True)
    p.add_argument('--runs', type=int, choices=range(1, 6), default=3)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    wire = open_serial(args.port)
    try:
        with (args.output / 'serial.log').open('wb') as raw, (args.output / 'results.jsonl').open('w') as log:
            client = Client(RecordedWire(wire, raw), timeout=15)
            def request(command):
                reply = client.request(command)
                log.write(json.dumps(dict(command=command, reply=reply)) + '\n')
                log.flush()
                return reply
            caps = request('capabilities')
            if caps.get('profile') != 'power' or caps.get('board') != 'waveshare-31261':
                raise ValueError('Wrong test bed')
            identity = request('power')
            before = request('display memory')
            validate(before, args.mode, False)
            samples = []
            for n in range(args.runs):
                sample = request('display benchmark-memory')
                validate(sample, args.mode)
                samples.append(sample)
                print(f'Copy run {n + 1}: bytes and guards passed', flush=True)
            request('display refresh')
            time.sleep(6)
            health = request('display')
            if (not health.get('checks') or health.get('mismatches') != 0 or health.get('transfer_failures') != 0
                    or any(health.get(k) is not True for k in ('buffers_equal', 'guards_ok', 'heap_ok'))):
                raise ValueError('Display recovery failed')
            result = dict(identity=identity, memory=before, samples=samples, health=health,
                          physical_verified=False, current_measured=False)
            (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(before, indent=2))
    finally:
        wire.close()


if __name__ == '__main__':
    main()

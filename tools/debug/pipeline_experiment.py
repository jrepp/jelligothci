# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Capture bounded pipeline timing; transfer completion is not visible scanout."""
import argparse
import json
from pathlib import Path
import time
from jelli_debug import Client, RecordedWire, open_serial

STAGES = ('engine', 'lock', 'copy', 'check', 'swap', 'stage', 'transfer')


def validate(reply, run, stage, boot):
    if (reply.get('ok') is not True or reply.get('run') != run or reply.get('boot') != boot
            or reply.get('active') is not False or reply.get('expired') is not False or reply.get('stage') != stage):
        raise ValueError('Stale or active pipeline result')
    for key in ('count', 'window', 'p50_us', 'p95_us', 'max_us', 'total_us', 'bytes'):
        if type(reply.get(key)) is not int or reply[key] < 0:
            raise ValueError('Invalid pipeline counter')
    if (reply['window'] != min(reply['count'], 128)
            or not reply['p50_us'] <= reply['p95_us'] <= reply['max_us']
            or reply['total_us'] < reply['max_us']):
        raise ValueError('Inconsistent pipeline distribution')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, choices=range(6, 31), default=10)
    parser.add_argument('--scene', required=True, help='Observer description of the running scene')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    wire = open_serial(args.port)
    try:
        with (args.output / 'serial.log').open('wb') as raw, (args.output / 'results.jsonl').open('w') as log:
            client = Client(RecordedWire(wire, raw), timeout=6)
            def request(command):
                reply = client.request(command)
                log.write(json.dumps({'time': time.time(), 'command': command, 'reply': reply}) + '\n')
                log.flush()
                return reply
            caps = request('capabilities')
            if caps.get('profile') != 'power' or caps.get('board') != 'waveshare-31261':
                raise ValueError('Expected SKU 31261 power image')
            identity = request('power')
            before = request('display')
            state = request('state')
            started = request('display profile start')  # Never retry an ambiguous action.
            if started.get('ok') is not True or started.get('active') is not True:
                raise ValueError('Profiler did not start')
            time.sleep(args.seconds)
            stopped = request('display profile stop')
            validate(stopped, started['run'], 'engine', started['boot'])
            replies = {}
            for stage in STAGES:
                reply = request(f'display profile {stage}')
                validate(reply, started['run'], stage, started['boot'])
                replies[stage] = reply
            if replies['stage']['count'] and not replies['transfer']['count']:
                raise ValueError('Missing transfer-completion measurements')
            health = request('display')
            if (not all(health.get(k) is True for k in ('buffers_equal', 'guards_ok', 'heap_ok'))
                    or health.get('mismatches') != before.get('mismatches')
                    or health.get('transfer_failures') != before.get('transfer_failures')):
                raise ValueError('Display health regression')
            result = {'scene': args.scene, 'seconds': args.seconds, 'identity': identity,
                      'state': state, 'stages': replies, 'health': health,
                      'percentiles': 'nearest-rank over last 128 samples per stage',
                      'physical_verified': False, 'current_measured': False}
            (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
            for stage, reply in replies.items():
                print(f"{stage}: n={reply['count']} p50={reply['p50_us']} p95={reply['p95_us']} us")
    finally:
        wire.close()


if __name__ == '__main__':
    main()

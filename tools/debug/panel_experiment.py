# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Run checked display transitions; serial success does not prove image or energy."""
import argparse
import json
from pathlib import Path
import time
from jelli_debug import Client, RecordedWire, open_serial

MODES = ('baseline', 'dim30', 'dim10', 'black', 'off', 'sleep', 'standby', 'baseline')


def validate(reply, accepted, mode, brightness):
    target = {'dim30': 30, 'dim10': 10, 'black': 0}.get(mode, brightness)
    if (reply.get('run') != accepted['run'] or reply.get('boot') != accepted['boot']
            or reply.get('mode') != mode or reply.get('status') != 'complete'
            or reply.get('error') != 0 or reply.get('restore_error') != 0
            or reply.get('faulted') is not False
            or reply.get('brightness_before') != brightness
            or reply.get('brightness_requested') != target
            or reply.get('brightness_restore_requested') != brightness
            or reply.get('refresh_requested') is not True
            or reply.get('physical_verified') is not False
            or reply.get('current_measured') is not False):
        raise ValueError('Incomplete, stale, failed or unrestored display trial')
    for field in ('entry_us', 'hold_us', 'recovery_us'):
        if type(reply.get(field)) is not int or not 0 <= reply[field] < 5000000:
            raise ValueError('Invalid timing')
    if reply['hold_us'] < 190000:
        raise ValueError('Missing hold interval')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--repetitions', type=int, choices=range(1, 101), default=10)
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
            if (caps.get('profile') != 'power' or caps.get('protocol') != 1
                    or caps.get('board') != 'waveshare-31261'):
                raise ValueError('Expected SKU 31261 power image')
            brightness = request('power')['brightness']
            replies = []
            for mode in MODES:
                for _ in range(args.repetitions):
                    accepted = request(f'power panel run {mode}')  # Never retry actions.
                    deadline = time.monotonic() + 6
                    while True:
                        reply = request('power panel result')
                        if reply.get('run') == accepted['run']:
                            break
                        if time.monotonic() > deadline:
                            raise TimeoutError('Result missing; inspect before retrying')
                        time.sleep(.03)
                    validate(reply, accepted, mode, brightness)
                    replies.append(reply)
            # Existing display guard counters refresh at five-second intervals.
            time.sleep(5.1)
            health = request('display')
            if (not all(health.get(k) is True for k in ('buffers_equal', 'guards_ok', 'heap_ok'))
                    or health.get('mismatches') != 0 or health.get('transfer_failures') != 0):
                raise ValueError('Post-trial display health failed')
            client.screenshot(args.output / 'logical-frame.png')
            request('power')
            (args.output / 'summary.json').write_text(json.dumps(replies, indent=2) + '\n')
            print(f'{len(replies)} transitions restored; physical image/current unverified')
    finally:
        wire.close()


if __name__ == '__main__':
    main()

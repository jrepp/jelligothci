# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Qualify the QMI8658 command protocol and bounded awake motion detection."""
import argparse
import json
from pathlib import Path
import re
import time
from jelli_debug import Client, RecordedWire, open_serial

BASELINE = '20000000000000000000'
ARMED = '200d0000218000804800'


def validate(reply, accepted, mode):
    if (reply.get('boot') != accepted.get('boot') or reply.get('run') != accepted.get('run')
            or reply.get('mode') != mode or reply.get('status') != 'complete'
            or reply.get('error') != 0 or reply.get('restore_error') != 0
            or reply.get('restored') is not True or reply.get('faulted') is not False
            or reply.get('who') != 5 or reply.get('revision') != 0x7c
            or reply.get('before') != BASELINE or reply.get('after') != BASELINE
            or reply.get('commands') != (1 if mode == 'identity' else 3)
            or reply.get('ack_status', 128) & 128
            or reply.get('mcu_slept') is not False or reply.get('current_measured') is not False):
        raise ValueError('Failed, stale, unsupported or unrestored IMU trial')
    if (not re.fullmatch('[0-9a-f]{6}', reply.get('firmware', ''))
            or not re.fullmatch('[0-9a-f]{12}', reply.get('usid', ''))
            or type(reply.get('elapsed_us')) is not int or not 0 <= reply['elapsed_us'] < 25000000):
        raise ValueError('Invalid identity or timing')
    if mode == 'identity':
        return 'handshake_pass'
    if reply.get('armed') != (('30' + ARMED[2:]) if mode.endswith('-int2') else ARMED) or reply.get('initial_level') != 0 or reply.get('status_valid') is not True:
        raise ValueError('Missing verified motion setup/status')
    motion = bool(reply['status1'] & 4)
    high = reply.get('observed_level') == 1
    if mode.startswith('quiet'):
        if motion or high or reply['elapsed_us'] < 2000000:
            raise ValueError('Activity in quiet interval; cannot claim quiet success')
        return 'quiet_pass'
    if not (motion and high):
        raise ValueError('Pickup not confirmed by both GPIO and WoM status')
    return 'motion_observed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repetitions', type=int, default=10, choices=range(1, 101))
    parser.add_argument('mode', choices=('identity', 'quiet', 'motion', 'quiet-int2', 'motion-int2'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    wire = open_serial(args.port)
    try:
        with (args.output / 'serial.log').open('wb') as raw, (args.output / 'results.jsonl').open('w') as log:
            client = Client(RecordedWire(wire, raw), timeout=5)
            def request(command):
                reply = client.request(command)
                log.write(json.dumps({'time': time.time(), 'command': command, 'reply': reply}) + '\n')
                log.flush()
                return reply
            caps = request('capabilities')
            if caps.get('profile') != 'power' or caps.get('board') != 'waveshare-31261':
                raise ValueError('Expected SKU31261 power firmware')
            results, cookie = [], None
            for _ in range(args.repetitions):
                accepted = request('power imu run ' + args.mode)
                if accepted.get('pending') is not True or type(accepted.get('run')) is not int:
                    raise ValueError('Run not accepted; do not retry')
                if cookie and (accepted.get('boot') != cookie[0] or accepted['run'] <= cookie[1]):
                    raise ValueError('Reboot or stale run')
                cookie = (accepted.get('boot'), accepted['run'])
                deadline, announced = time.monotonic() + 25, False
                while True:
                    reply = request('power imu result')
                    if reply.get('boot') != cookie[0] or reply.get('run') != cookie[1]:
                        raise ValueError('Unexpected run identity')
                    if reply.get('status') == 'complete':
                        break
                    if reply.get('status') not in ('armed', 'settling') or time.monotonic() >= deadline:
                        raise TimeoutError('Result missing; inspect without retrying action')
                    if not announced and reply.get('status') == 'armed':
                        print('ARMED: ' + args.mode, flush=True)
                        announced = True
                    time.sleep(.05)
                outcome = validate(reply, accepted, args.mode)
                results.append({'outcome': outcome, 'reply': reply})
                print(outcome, flush=True)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    finally:
        wire.close()


if __name__ == '__main__':
    main()

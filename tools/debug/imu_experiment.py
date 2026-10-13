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


def validate_rearm(reply):
    if type(reply.get('completed_events')) is not int or reply['completed_events'] != 2:
        raise ValueError('Missing two completed motion/ACK episodes')
    for key in ('motion_levels', 'motion_status', 'ack_levels', 'idle_levels', 'ack_wait_us'):
        values = reply.get(key)
        if not isinstance(values, list) or len(values) != 2 or any(type(v) is not int for v in values):
            raise ValueError('Invalid rearm observations')
    if (reply['motion_levels'] != [1, 1] or reply['idle_levels'] != [0, 0]
            or any(not (v & 4) or not 0 <= v <= 255 for v in reply['motion_status'])
            or any(v not in (0, 1) for v in reply['ack_levels'])
            or any(not 100000 <= v < 1000000 for v in reply['ack_wait_us'])):
        raise ValueError('Motion/ACK did not return to sampled stable idle')
    return 'rearm_observed'


def validate(reply, accepted, mode):
    if (reply.get('boot') != accepted.get('boot') or reply.get('run') != accepted.get('run')
            or reply.get('mode') != mode or reply.get('status') != 'complete'
            or reply.get('error') != 0 or reply.get('restore_error') != 0
            or reply.get('restored') is not True or reply.get('faulted') is not False
            or reply.get('who') != 5 or reply.get('revision') != 0x7c
            or reply.get('before') != BASELINE or reply.get('after') != BASELINE
            or reply.get('commands') != (1 if mode == 'identity' else 3)
            or reply.get('ack_status', 128) & 128
            or reply.get('mcu_slept') is not (mode == 'sleep-int2') or reply.get('current_measured') is not False):
        raise ValueError('Failed, stale, unsupported or unrestored IMU trial')
    if (not re.fullmatch('[0-9a-f]{6}', reply.get('firmware', ''))
            or not re.fullmatch('[0-9a-f]{12}', reply.get('usid', ''))
            or type(reply.get('elapsed_us')) is not int
            or not 0 <= reply['elapsed_us'] < (55000000 if mode == 'rearm-int2' else 25000000)):
        raise ValueError('Invalid identity or timing')
    if mode == 'identity':
        return 'handshake_pass'
    if reply.get('armed') != (('30' + ARMED[2:]) if mode.endswith('-int2') else ARMED) or reply.get('initial_level') != 0 or reply.get('status_valid') is not True:
        raise ValueError('Missing verified motion setup/status')
    if mode == 'rearm-int2':
        return validate_rearm(reply)
    if mode == 'sleep-int2':
        return validate_sleep(reply)
    motion = bool(reply['status1'] & 4)
    high = reply.get('observed_level') == 1
    if mode.startswith('quiet'):
        if motion or high or reply['elapsed_us'] < 2000000:
            raise ValueError('Activity in quiet interval; cannot claim quiet success')
        return 'quiet_pass'
    if not (motion and high):
        raise ValueError('Pickup not confirmed by both GPIO and WoM status')
    return 'motion_observed'


def validate_sleep(reply):
    if (reply.get('sleep_attempted') is not True or reply.get('already_active') is not False
            or reply.get('wake_cleanup_error') != 0 or reply.get('host_restore_error') != 0
            or type(reply.get('sleep_us')) is not int
            or not 0 < reply['sleep_us'] < 12000000):
        raise ValueError('Missing sleep or incomplete host/wake-source restoration')
    if reply.get('wake_cause') == 4 and reply.get('wake_level') == 0 and not (reply['status1'] & 4):
        if reply['sleep_us'] < 9900000:
            raise ValueError('Timer fallback returned too early')
        return 'timer_fallback'
    if reply.get('wake_cause') == 7 and reply.get('wake_level') == 1 and reply['status1'] & 4:
        return 'gpio_motion_wake'
    raise ValueError('Wake cause, pin and sensor status do not agree')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repetitions', type=int, default=10, choices=range(1, 101))
    parser.add_argument('mode', choices=('identity', 'quiet', 'motion', 'quiet-int2', 'motion-int2', 'rearm-int2'))
    args = parser.parse_args()
    if args.mode == 'rearm-int2' and args.repetitions != 1:
        parser.error('rearm-int2 requires --repetitions 1; coordinate each two-pickup window')
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
                deadline = time.monotonic() + (55 if args.mode == 'rearm-int2' else 25)
                announced = set()
                while True:
                    reply = request('power imu result')
                    if reply.get('boot') != cookie[0] or reply.get('run') != cookie[1]:
                        raise ValueError('Unexpected run identity')
                    if reply.get('status') == 'complete':
                        break
                    if reply.get('status') not in ('armed', 'settling') or time.monotonic() >= deadline:
                        raise TimeoutError('Result missing; inspect without retrying action')
                    event = reply.get('event', 1)
                    if reply.get('status') == 'armed' and event not in announced:
                        suffix = f' pickup {event}' if args.mode == 'rearm-int2' else ''
                        print(f'ARMED: {args.mode}{suffix}', flush=True)
                        announced.add(event)
                    time.sleep(.05)
                outcome = validate(reply, accepted, args.mode)
                results.append({'outcome': outcome, 'reply': reply})
                print(outcome, flush=True)
            (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    finally:
        wire.close()


if __name__ == '__main__':
    main()

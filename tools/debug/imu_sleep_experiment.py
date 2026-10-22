# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["pyserial==3.5"]
# ///
"""Run one bounded IMU sleep trial; never retry an action after USB loss."""
import argparse
import json
from pathlib import Path
import time
from serial.tools import list_ports
from jelli_debug import Client, RecordedWire, open_serial
from imu_experiment import validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--recover', action='store_true', help='Read the retained result of this output directory; never start a trial')
    parser.add_argument('--expect', choices=('timer_fallback', 'gpio_motion_wake'), required=True)
    args = parser.parse_args()
    identity = next((p.serial_number for p in list_ports.comports() if p.device == args.port), None)
    if not identity:
        raise ValueError('USB identity required before starting')
    manifest = args.output / 'identity.json'
    accepted = None
    if args.recover:
        if json.loads(manifest.read_text())['usb_serial'] != identity:
            raise ValueError('Recovery USB identity differs from original run')
        records = [json.loads(line) for line in (args.output / 'results.jsonl').read_text().splitlines()]
        actions = [r['reply'] for r in records if r['command'] == 'power imu run sleep-int2']
        if len(actions) != 1:
            raise ValueError('Expected exactly one previously accepted action')
        accepted = actions[0]
    else:
        args.output.mkdir(parents=True, exist_ok=False)
        manifest.write_text(json.dumps({'usb_serial': identity}) + '\n')
    wire = open_serial(args.port)
    try:
        with (args.output / 'serial.log').open('ab') as raw, (args.output / 'results.jsonl').open('a') as log:
            client = Client(RecordedWire(wire, raw), timeout=5)

            def request(command):
                reply = client.request(command)
                log.write(json.dumps({'time': time.time(), 'command': command, 'reply': reply}) + '\n')
                log.flush()
                return reply

            caps = request('capabilities')
            if caps.get('profile') != 'power' or caps.get('board') != 'waveshare-31261':
                raise ValueError('Expected SKU31261 power firmware')
            if not args.recover:
                for command in ('network', 'display', 'state'):
                    request(command)
                accepted = request('power imu run sleep-int2')
                if (accepted.get('pending') is not True or type(accepted.get('boot')) is not int
                        or type(accepted.get('run')) is not int):
                    raise ValueError('Run not accepted; inspect without retrying')
                print('ACCEPTED: sleep trial; ten-second window begins when the display goes dark', flush=True)
                wire.close()
                # No command is sent while the MCU/USB is sleeping. This includes
                # setup and recovery margin; only a retained result is queried next.
                time.sleep(15)
                ports = [p.device for p in list_ports.comports() if p.serial_number == identity]
                if len(ports) != 1:
                    raise RuntimeError('USB identity missing/ambiguous; inspect without repeating the trial')
                wire.port = ports[0]
                wire.open()
            reply = request('power imu result')
            # Retain post-trial health even when the requested outcome failed.
            for command in ('display', 'network', 'state'):
                request(command)
            outcome = validate(reply, accepted, 'sleep-int2')
            (args.output / 'summary.json').write_text(json.dumps({'outcome': outcome, 'reply': reply}, indent=2) + '\n')
            if outcome != args.expect:
                raise ValueError(f'Expected {args.expect}; observed {outcome}')
            print(outcome, flush=True)
    finally:
        wire.close()


if __name__ == '__main__':
    main()

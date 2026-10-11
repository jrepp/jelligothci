# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Run explicit ESP32 power trials, retaining replies and raw serial evidence."""
import argparse
import json
from pathlib import Path
import time

from serial.tools import list_ports
from jelli_debug import Client, RecordedWire, open_serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('modes', nargs='+', choices=('delay', 'blank', 'pause', 'off', 'panel', 'audio', 'reset50', 'reset100', 'light', 'touch', 'auto'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    wire = open_serial(args.port, 15)
    identity = next((p.serial_number for p in list_ports.comports() if p.device == args.port), None)
    try:
        with (args.output / 'serial.log').open('wb') as raw, (args.output / 'results.jsonl').open('w') as results:
            client = Client(RecordedWire(wire, raw), timeout=20)

            def request(command):
                reply = client.request(command)
                record = {'time': time.time(), 'command': command, 'reply': reply}
                results.write(json.dumps(record) + '\n')
                results.flush()
                print(json.dumps(record), flush=True)
                return reply

            identity_reply = request('power')
            if (not identity_reply.get('ok') or identity_reply.get('schema') != 1
                    or identity_reply.get('board') != 'waveshare-31261'
                    or identity_reply.get('profile') != 'power'):
                raise RuntimeError('Expected schema 1 power validation firmware for Waveshare 31261')
            for command in ('state', 'clock', 'display', 'power locks'):
                request(command)
            for mode in args.modes:
                if mode == 'audio':
                    request('sound 5 15')
                request('power run ' + mode)
                # USB cannot receive during explicit MCU sleep. Capture unsolicited
                # logs through the bounded window, then send a fresh result query.
                if mode in ('light', 'touch'):
                    wire.close()
                    time.sleep(3 if mode == 'light' else 13)
                    ports = [p.device for p in list_ports.comports() if identity and p.serial_number == identity]
                    if len(ports) != 1:
                        raise RuntimeError('Device identity not rediscovered; inspect outcome before retrying')
                    wire.port = ports[0]
                    wire.open()
                reply = request('power result')
                if reply.get('mode') != mode or reply.get('error') or reply.get('restore_error'):
                    raise RuntimeError(f'Trial failed; stop and inspect evidence: {reply}')
                if mode == 'audio':
                    request('sound 5 15')
                request('power')
                request('display')
            client.screenshot(args.output / 'after.png')
            request('state')
    finally:
        wire.close()


if __name__ == '__main__':
    main()

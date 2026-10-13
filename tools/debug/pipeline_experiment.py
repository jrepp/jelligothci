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
from memory_experiment import validate as validate_memory

STAGES = ('engine', 'lock', 'copy', 'check', 'swap', 'stage', 'transfer', 'render', 'lvgl', 'lvgl_wait', 'lvgl_flush', 'lvgl_work', 'lvgl_image', 'lvgl_fill', 'lvgl_tasks', 'lvgl_dispatch', 'lvgl_draw_wait', 'lvgl_events')


def validate_network(reply):
    if (reply.get('ok') is not True or reply.get('available') is not True
            or reply.get('pending') is not False or type(reply.get('result')) is not int
            or reply['result'] != 0):
        raise ValueError('Network initialization/operation failed; timing would compare different resource states')


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


def validate_stages(replies):
    if not replies['engine']['count'] or not replies['render']['count']:
        raise ValueError('Missing engine/render measurements')
    if replies['stage']['count'] and not all(replies[s]['count'] for s in ('transfer', 'lvgl', 'lvgl_wait', 'lvgl_flush', 'lvgl_work', 'lvgl_image')):
        raise ValueError('Missing transfer-completion or LVGL measurements')
    if replies['stage']['count'] and not replies['lvgl_tasks']['count']:
        raise ValueError('Missing synchronous draw-task instrumentation')
    if sum(replies[s]['total_us'] for s in ('lvgl_tasks', 'lvgl_dispatch', 'lvgl_draw_wait')) > replies['lvgl_work']['total_us']:
        raise ValueError('Dispatch subintervals exceed LVGL work')
    if replies['stage']['count'] and not replies['lvgl_events']['count']:
        raise ValueError('Missing object draw-event instrumentation')
    if replies['lvgl_events']['total_us'] > replies['lvgl_work']['total_us']:
        raise ValueError('Object draw-event time exceeds LVGL work')
    if replies['lvgl_work']['count'] != replies['lvgl']['count']:
        raise ValueError('Incomplete LVGL interval decomposition')
    if sum(replies[s]['total_us'] for s in ('lvgl_work', 'lvgl_flush', 'lvgl_wait')) != replies['lvgl']['total_us']:
        raise ValueError('LVGL intervals do not account for total elapsed time')


def restore_modes(request):
    errors = []
    for command, key, expected in (
            ('display profile stop', 'active', False),
            ('display profile overlays baseline', 'overlays', 'baseline'),
            ('display profile memcpy builtin', 'memcpy', 'builtin')):
        try:
            reply = request(command)
            if reply.get('ok') is not True or reply.get(key) != expected:
                raise ValueError('Unexpected cleanup reply')
        except Exception as error:
            errors.append(f'{command}: {error}')
    if errors:
        raise ValueError('Cleanup failed; restore normal firmware: ' + '; '.join(errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, choices=range(6, 31), default=10)
    parser.add_argument('--memory-mode', type=int, choices=(0, 1, 2), default=0)
    parser.add_argument('--overlays', choices=('baseline', 'skip'), default='baseline')
    parser.add_argument('--memcpy', choices=('builtin', 'libc'), default='builtin')
    parser.add_argument('--refresh-ms', type=int, choices=(0, 500, 1000), default=0,
                        help='Explicit full-canvas refresh cadence; zero leaves normal damage')
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
            network = request('network')
            validate_network(network)
            memory = request('display memory')
            validate_memory(memory, args.memory_mode, measured=False)
            before = request('display')
            state = request('state')
            try:
                mode = request('display profile memcpy ' + args.memcpy)
                if mode.get('ok') is not True or mode.get('memcpy') != args.memcpy:
                    raise ValueError('Copy mode or byte-equivalence check failed')
                overlay = request('display profile overlays ' + args.overlays)
                if overlay.get('ok') is not True or overlay.get('overlays') != args.overlays:
                    raise ValueError('Overlay guard or mode change failed')
                started = request('display profile start')  # Never retry an ambiguous action.
                if started.get('ok') is not True or started.get('active') is not True:
                    raise ValueError('Profiler did not start')
                if args.refresh_ms:
                    deadline = time.monotonic() + args.seconds
                    while time.monotonic() < deadline:
                        reply = request('display refresh')
                        if reply.get('pending') is not True:
                            raise ValueError('Refresh not accepted')
                        time.sleep(min(args.refresh_ms / 1000, max(0, deadline - time.monotonic())))
                else:
                    time.sleep(args.seconds)
                stopped = request('display profile stop')
                validate(stopped, started['run'], 'engine', started['boot'])
                replies = {}
                for stage in STAGES:
                    reply = request(f'display profile {stage}')
                    validate(reply, started['run'], stage, started['boot'])
                    replies[stage] = reply
                validate_stages(replies)
                health = request('display')
                network_after = request('network')
                validate_network(network_after)
                if any(network.get(k) != network_after.get(k) for k in ('enabled', 'connected', 'operation', 'completed')):
                    raise ValueError('Network state changed during capture')
                if (not all(health.get(k) is True for k in ('buffers_equal', 'guards_ok', 'heap_ok'))
                        or health.get('mismatches') != before.get('mismatches')
                        or health.get('transfer_failures') != before.get('transfer_failures')):
                    raise ValueError('Display health regression')
            finally:
                restore_modes(request)
            result = {'overlays': args.overlays, 'overlays_restored': True, 'memory': memory, 'memcpy': args.memcpy, 'copy_restored': True, 'scene': args.scene, 'seconds': args.seconds, 'refresh_ms': args.refresh_ms, 'identity': identity,
                      'state': state, 'stages': replies, 'health': health,
                      'network': network, 'network_after': network_after,
                      'percentiles': 'nearest-rank over last 128 samples per stage',
                      'physical_verified': False, 'current_measured': False}
            (args.output / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
            for stage, reply in replies.items():
                print(f"{stage}: n={reply['count']} p50={reply['p50_us']} p95={reply['p95_us']} us")
    finally:
        wire.close()


if __name__ == '__main__':
    main()

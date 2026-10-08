"""Live SDL artwork swaps preserve care state and recover from invalid packs."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('jelli_debug', root / 'tools/debug/jelli_debug.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def pack(color, center=8192, duplicate=False):
    payload = struct.pack('<H', color) * 4096 + b'\xff' * 512
    record = struct.pack('<IHHHHHHBBBBI', 10001, 64, 64, 0, 0, center, 8192,
                         0, 0, 64, 64, len(payload)) + payload
    body = record * (2 if duplicate else 1)
    return struct.pack('<4sIII', b'JLAP', 1, 2 if duplicate else 1, zlib.crc32(body)) + body


def pixel(client):
    token = client.request('capture')['capture']
    try:
        return int(client.request(f'pixels {token} {233 * 466 + 100} 1')['rgb565'], 16)
    finally:
        client.request(f'release {token}')


def wait_pixel(client, expected):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if pixel(client) == expected:
            return
        time.sleep(.05)
    raise AssertionError('Live artwork did not update')


with tempfile.TemporaryDirectory(prefix='jelli-assets-') as folder:
    folder = Path(folder)
    path, artwork = folder / 'debug.sock', folder / 'live.jlap'
    artwork.write_bytes(pack(0xf800))
    app = subprocess.Popen([sys.argv[1], '--headless', '--debug-socket', str(path),
                            '--asset-pack', str(artwork), '--frames', '10000',
                            '--wall-ms', '1791374400000'], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
    wire = None
    try:
        deadline = time.monotonic() + 4
        while wire is None:
            assert app.poll() is None, 'SDL app stopped'
            try:
                wire = module.SocketWire(str(path), 1)
            except OSError:
                assert time.monotonic() < deadline
                time.sleep(.01)
        client = module.Client(wire)
        wait_pixel(client, 0xf800)
        client.press('MENU')
        client.press('SETTINGS')
        client.press('REST')
        before = client.state()
        artwork.write_bytes(pack(0x07e0, center=8193))  # In-range but false alpha centroid.
        time.sleep(.65)
        assert pixel(client) == 0xf800
        artwork.write_bytes(pack(0x07e0))
        wait_pixel(client, 0x07e0)
        after = client.state()
        for key in ('pet_id', 'asleep', 'page', 'food', 'gifts'):
            assert before['visual'][key] == after['visual'][key], key
        assert after['ticks'] >= before['ticks']
        artwork.write_bytes(pack(0x001f, duplicate=True))
        time.sleep(.65)
        assert pixel(client) == 0x07e0
        corrupt = bytearray(pack(0x001f))
        corrupt[12] ^= 1
        artwork.write_bytes(corrupt)
        time.sleep(.65)
        assert pixel(client) == 0x07e0
        artwork.write_bytes(pack(0x001f))
        wait_pixel(client, 0x001f)
        assert app.poll() is None  # Same process; no rebuild or restart.
    finally:
        if wire is not None:
            wire.close()
        app.terminate()
        app.communicate(timeout=3)
print('PASS: live geometry validation, atomic bank swaps, invalid-pack retry, state preservation')

"""Exercise clock admission, host completion, habits, and journal over the SDL socket."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('jelli_debug', root / 'tools/debug/jelli_debug.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

# Admission is not RTC completion: an unavailable port must surface a failure.
failed = module.Client(None)
failed.request = lambda command: ({'ok': True, 'pending': True} if command != 'clock' else
                                  {'ok': True, 'pending': False, 'clock_known': False,
                                   'result': 'not ready'})
try:
    failed.set_clock(1700000000, -240)
    raise AssertionError('Unknown RTC reported successful sync')
except module.DebugError as exc:
    assert 'rejected' in str(exc)

with tempfile.TemporaryDirectory(prefix='jelli-clock-') as folder:
    path = Path(folder) / 'debug.sock'
    app = subprocess.Popen([sys.argv[1], '--headless', '--debug-socket', str(path),
                            '--frames', '5000'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    wire = None
    try:
        deadline = time.monotonic() + 4
        while wire is None:
            assert app.poll() is None, 'SDL app stopped'
            try:
                wire = module.SocketWire(str(path), 1)
            except OSError:
                assert time.monotonic() < deadline, 'Socket not ready'
                time.sleep(.01)
        client = module.Client(wire)
        result = client.set_clock(1700000000, -240)
        assert result['clock_known'] and not result['pending']
        assert result['timezone_minutes'] == -240
        for command in ('clock 0 720', 'clock 1700000000 1561', 'clock 4102444800 720'):
            try:
                client.request(command)
                raise AssertionError('Invalid clock admitted')
            except module.DebugError as exc:
                assert 'range' in str(exc)
        client.request('capture')
        try:
            client.set_clock(1700000000, -240)
            raise AssertionError('Captured clock edit admitted')
        except module.DebugError as exc:
            assert 'busy' in str(exc)
        token = client.request('state')['capture']
        client.request(f'release {token}')
        habits = client.request('habits')
        assert set(habits['scores']) == {'sleep', 'food', 'play'}
        assert all(0 <= score <= 1000 for score in habits['scores'].values())
        assert client.request('sleep-log')['sessions'] == []
        client.press('MENU')
        client.press('SETTINGS')
        for _ in range(9):
            client.press('REST')
            client.press('WAKE')
        log = client.request('sleep-log')
        assert len(log['sessions']) == 8 and not log['active']
        assert all(s['real_duration'] and s['flags'] & 3 == 3 for s in log['sessions'])
        assert all(s['wake_unix_seconds'] >= s['bed_unix_seconds'] for s in log['sessions'])
    finally:
        if wire is not None:
            wire.close()
        app.terminate()
        app.communicate(timeout=3)
print('PASS: clock admission/completion, capture rejection, habit coverage, bounded sleep journal')

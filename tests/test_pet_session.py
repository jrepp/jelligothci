"""Exercise desktop restart/clock/error behavior with isolated two-slot saves."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()


def run(base, wall, frames=1, extra=()):
    return subprocess.run([str(binary), "--pet", "--headless", "--frames", str(frames),
                           "--wall-ms", str(wall), "--save", str(base), *extra],
                          text=True, capture_output=True, check=False)


def latest(base):
    slots = [path.read_bytes() for path in [Path(str(base) + ".0"), Path(str(base) + ".1")]
             if path.exists()]
    return max(slots, key=lambda data: struct.unpack_from("<Q", data, 12)[0])


def ticks(data):
    return struct.unpack_from("<Q", data, 32)[0]


with tempfile.TemporaryDirectory(prefix="jelli-session-") as folder:
    base = Path(folder) / "pet"
    assert run(base, 1000000, 125).returncode == 0
    first = latest(base)
    assert ticks(first) == 10
    anchor = struct.unpack_from("<Q", first, 20)[0]
    assert anchor == 1001000
    assert run(base, anchor + 3600000).returncode == 0
    second = latest(base)
    assert ticks(second) == ticks(first) + 36000
    second_anchor = struct.unpack_from("<Q", second, 20)[0]
    assert run(base, second_anchor).returncode == 0
    third = latest(base)
    assert ticks(third) == ticks(second)  # no repeated offline hour
    assert run(base, second_anchor - 10000).returncode == 0
    assert ticks(latest(base)) == ticks(third)  # reversed clock is forgiven
    assert run(base, second_anchor + 100000000).returncode == 0
    assert ticks(latest(base)) == ticks(third) + 864000  # 24-hour cap
    paths = [Path(str(base) + suffix) for suffix in (".0", ".1")]
    for path in paths:
        path.write_bytes(b"interrupted-write")
    before = [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
    failed = run(base, second_anchor)
    assert failed.returncode != 0 and "preserved" in failed.stderr
    assert before == [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
    demo = Path(folder) / "demo"
    assert run(demo, 1000000, 780, ("--demo",)).returncode == 0
    assert ticks(latest(demo)) == 780
print("PASS: saved time cuts, restart idempotence, invalid/capped clocks, corrupt-file preservation, demo save")

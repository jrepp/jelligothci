"""Exercise desktop restart/clock/error behavior with isolated two-slot saves."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

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


COUNT, ACTIVE, PETS = 68, 69, 71  # Roster header offsets in the save record.
SOUND_FROM_END = 9  # Version 6 sound level byte, just before the 8-byte trailer.


def roster(data):
    """(count, active slot, pet record size, [(id, form)]) of a saved roster."""
    count, active = data[COUNT], data[ACTIVE]
    ids = [1, 2, 3]  # A new game's stable IDs, in slot order.
    size = next(n for n in range(64, 1024)
                if all(struct.unpack_from("<I", data, PETS + i * n)[0] == ids[i] for i in range(3)))
    # form is the first byte after id, eight u64s, four u32s, and three u16 groups.
    form_at = 4 + 8 * 7 + 4 * 4 + 2 * 5 * 2 + 2 * 3
    pets = [(struct.unpack_from("<I", data, PETS + i * size)[0], data[PETS + i * size + form_at])
            for i in range(count)]
    return count, active, size, pets


def store(base, data, version):
    """Rewrites a record as `version`, with a correct size and checksum, as the only slot."""
    data = bytearray(data)
    data[4] = version
    struct.pack_into("<I", data, 8, len(data))
    struct.pack_into("<I", data, len(data) - 8, zlib.crc32(bytes(data[:len(data) - 8])))
    Path(str(base) + ".0").write_bytes(bytes(data))
    Path(str(base) + ".1").unlink(missing_ok=True)


def without_sound(data):
    """The same record in the version 4/5 layout, which ends before the sound level."""
    return data[:-SOUND_FROM_END] + data[-SOUND_FROM_END + 1:]


def keeps_saved_pet(folder):
    """New players start with Nirmal (slot 2); loaded v4-v6 saves keep their own pet."""
    base = Path(folder) / "fresh"
    assert run(base, 1000000, 3).returncode == 0
    fresh = latest(base)
    count, active, size, pets = roster(fresh)
    assert (count, active) == (3, 2) and pets == [(1, 0), (2, 0), (3, 2)], (count, active, pets)
    assert fresh[4] == 6 and fresh[-SOUND_FROM_END] == 5  # New games save the middle level.
    for slot in (0, 1):  # Current-version save with a jelly active and a stored level.
        data = bytearray(fresh)
        data[ACTIVE], data[-SOUND_FROM_END] = slot, 9
        store(base, data, 6)
        assert run(base, 1000000, 3).returncode == 0
        assert roster(latest(base))[1] == slot and latest(base)[-SOUND_FROM_END] == 9
    for slot in (0, 1):  # Version 5 save: same roster, the sound level takes its default.
        data = bytearray(without_sound(fresh))
        data[ACTIVE] = slot
        store(base, data, 5)
        assert run(base, 1000000, 3).returncode == 0
        resaved = latest(base)
        assert resaved[4] == 6 and roster(resaved)[1] == slot and resaved[-SOUND_FROM_END] == 5
    for slot in (0, 1):  # Pre-ninja v4 save: two Mints, the ninja is appended, not activated.
        data = bytearray(without_sound(fresh[:PETS + 2 * size] + fresh[PETS + 3 * size:]))
        data[COUNT], data[ACTIVE] = 2, slot
        store(base, data, 4)
        assert run(base, 1000000, 3).returncode == 0
        count, active, _, pets = roster(latest(base))
        assert (count, active) == (3, slot) and pets[2] == (3, 2), (count, active, pets)


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
    keeps_saved_pet(folder)
print("PASS: saved time cuts, restart idempotence, invalid/capped clocks, corrupt-file preservation, demo save, "
      "Nirmal for new players, v4-v6 saves keep their active pet and v6 its sound level")

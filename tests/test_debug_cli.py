"""Exercise the CLI against the actual portable C protocol and framebuffer."""
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

source = Path(__file__).resolve().parents[1] / "tools/debug/jelli_debug.py"
spec = importlib.util.spec_from_file_location("jelli_debug", source)
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class Pipe:
    def __init__(self, executable):
        self.process = subprocess.Popen([executable, "--pipe"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE)

    def write(self, data):
        size = self.process.stdin.write(data)
        self.process.stdin.flush()
        return size

    def read(self, size):
        return self.process.stdout.read(size)

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)
        self.process.stdout.close()
        assert self.process.returncode == 0


wire = Pipe(sys.argv[1])
try:
    debug = client.Client(wire)
    assert debug.state()["visual"]["page"] == "home"
    debug.press("MENU")
    debug.press("CARE")
    assert debug.state()["visual"]["page"] == "care"
    try:
        debug.request("press 0 1")
        raise AssertionError("stale-page press accepted")
    except client.DebugError as exc:
        assert "page_changed" in str(exc)
    debug.press("BACK")
    debug.press("SETTINGS")
    debug.press("REST")
    assert debug.state()["visual"]["asleep"]
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "capture.png"
        debug.screenshot(path)
        state = json.loads(path.with_suffix(".png.json").read_text())
        assert state["visual"]["asleep"] and state["capture"]
        png = path.read_bytes()
        assert png[:8] == b"\x89PNG\r\n\x1a\n"
        offset, image = 8, bytearray()
        while offset < len(png):
            length = struct.unpack_from(">I", png, offset)[0]
            kind = png[offset + 4:offset + 8]
            data = png[offset + 8:offset + 8 + length]
            crc = struct.unpack_from(">I", png, offset + 8 + length)[0]
            assert zlib.crc32(kind + data) == crc
            if kind == b"IDAT":
                image.extend(data)
            offset += length + 12
        raw = zlib.decompress(image)
        assert len(raw) == 466 * (466 * 3 + 1)
        assert raw[:4] == b"\x00\x00\x00\x00"  # round corner is black
        assert len(set(raw)) > 10  # actual rendered content, not a blank capture
    assert debug.state()["capture"] == 0
    debug.press("WAKE")
    assert not debug.state()["visual"]["asleep"]
finally:
    wire.close()


class Noisy:
    def write(self, data):
        prefix = data.split(b"state")[0]
        self.received = io.BytesIO(b"boot log\n" + b"x" * 3000 + b"\n@J1 0 {}\n"
                                  + prefix + b'{"ok":true}\r\n')
        return len(data)

    def read(self, size):
        return self.received.read(size)


assert client.Client(Noisy()).request("state") == {"ok": True}


class Silent:
    def __init__(self):
        self.writes = 0

    def write(self, data):
        self.writes += 1
        return len(data)

    def read(self, size):
        return b""


silent = Silent()
try:
    client.Client(silent, timeout=0.001).request("tap 114 332")
    raise AssertionError("missing timeout")
except client.DebugError:
    assert silent.writes == 1  # Never replay an input after ambiguous loss.
print("PASS: C protocol, navigation, sleep input, exact PNG capture, release, logs, timeout")

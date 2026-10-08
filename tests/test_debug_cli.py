"""Exercise the CLI against the actual portable C protocol and framebuffer."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import types
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
    home = debug.state()["visual"]
    assert home["page"] == "home"
    assert (home["active_slot"], home["pet_id"], home["form"]) == (2, 3, 2)  # Nirmal
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
    battery = debug.request("battery charging 42")["battery"]
    assert battery["override"] and battery["caption"] == "42%" and battery["percent"] == 42
    assert debug.state()["visual"]["battery"]["state"] == "charging"
    try:
        debug.request("battery charging 101")
        raise AssertionError("out-of-range battery override accepted")
    except client.DebugError as exc:
        assert "battery_state_or_percent" in str(exc)
    battery = debug.request("battery clear")["battery"]
    assert not battery["override"] and battery["state"] == "unknown" and battery["percent"] is None
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


def run_cli(*argv):
    """Runs the CLI in-process with a recording client; never opens a port or socket."""
    if "serial" not in sys.modules:
        try:
            import serial  # noqa: F401  (the real package, when installed)
        except ImportError:
            fake = types.ModuleType("serial")
            fake.SerialException = OSError
            fake.tools = types.ModuleType("serial.tools")
            fake.tools.list_ports = types.ModuleType("serial.tools.list_ports")
            sys.modules.update({"serial": fake, "serial.tools": fake.tools,
                                "serial.tools.list_ports": fake.tools.list_ports})
    sent = []

    class Recorder:
        def __init__(self, wire, timeout=3.0):
            pass

        def request(self, command):
            sent.append(command)
            return {"ok": True}

    class NoWire:
        def __init__(self, path, timeout):
            pass

        def close(self):
            pass

    saved = sys.argv, client.Client, client.SocketWire
    sys.argv = ["jelli_debug.py", *argv]
    client.Client, client.SocketWire = Recorder, NoWire
    errors = io.StringIO()
    try:
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
            code = client.main()
    except SystemExit as exc:  # argparse rejects before any connection
        code = exc.code
    finally:
        sys.argv, client.Client, client.SocketWire = saved
    return code, sent, errors.getvalue()


assert run_cli("battery") == (0, ["battery"], "")
assert run_cli("battery", "clear")[:2] == (0, ["battery clear"])
assert run_cli("battery", "charging", "42")[:2] == (0, ["battery charging 42"])
assert run_cli("battery", "discharging")[:2] == (0, ["battery discharging unknown"])
assert run_cli("battery", "charged", "0")[:2] == (0, ["battery charged 0"])  # "0" is not falsy
assert run_cli("battery", "no-battery", "unknown")[:2] == (0, ["battery no-battery unknown"])
assert run_cli("battery", "discharging", "100")[:2] == (0, ["battery discharging 100"])
for bad in (("charging", "101"), ("charging", "-1"), ("charging", "abc"), ("charging", ""),
            ("charging", "999999999999"), ("charging", "1e2"), ("charging", "٣"),
            ("charging", " 5"), ("clear", "5"), ("sideways",), ("Charging", "5"),
            ("charging", "5", "extra")):
    code, sent, errors = run_cli("battery", *bad)
    assert code == 2 and sent == [], (bad, code, sent)
    assert "battery" in errors or "invalid choice" in errors or "unrecognized" in errors, errors
print("PASS: C protocol, navigation, sleep input, exact PNG capture, release, logs, timeout, CLI args")

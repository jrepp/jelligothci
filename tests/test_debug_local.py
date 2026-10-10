"""Integration test of the real SDL app's local socket, including lost clients."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("jelli_debug", root / "tools/debug/jelli_debug.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
binary = str(Path(sys.argv[1]).resolve())


def connect(path, process):
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        assert process.poll() is None, "SDL app stopped unexpectedly"
        try:
            return module.SocketWire(str(path), 1)
        except OSError:
            time.sleep(0.01)
    raise AssertionError("debug endpoint did not appear")


with tempfile.TemporaryDirectory(prefix="jelli-local-") as directory:
    path = Path(directory) / "debug.sock"
    app = subprocess.Popen([binary, "--headless", "--debug-socket", str(path),
                            "--frames", "2000"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        wire = connect(path, app)
        debug = module.Client(wire)
        assert debug.state()["visual"]["page"] == "home"
        debug.request("swipe 2")
        assert debug.state()["visual"]["stat_index"] == 1
        debug.request("swipe 3")
        assert debug.state()["visual"]["stat_index"] == 0
        debug.request("swipe 0")
        debug.press("CARE")
        assert debug.state()["visual"]["page"] == "care"
        debug.press("BACK")
        debug.press("SETTINGS")
        debug.press("PETS")
        assert debug.state()["visual"]["page"] == "pets"
        debug.press("COMPANION 2")
        debug.press("EVOLUTIONS")
        debug.press("LILAC")
        assert debug.state()["visual"]["pet_id"] == 1
        debug.press("BACK")
        debug.press("BRING OUT")
        assert debug.state()["visual"]["pet_id"] == 2
        debug.press("MENU")
        debug.press("SETTINGS")
        debug.press("CLOCK")
        minute = debug.state()["visual"]["clock_minute"]
        debug.press("HR +")
        assert debug.state()["visual"]["clock_minute"] == (minute + 60) % 1440
        debug.request("swipe 1")
        debug.press("REST")
        assert debug.state()["visual"]["asleep"]
        output = Path(directory) / "screen.png"
        debug.screenshot(output)
        assert output.read_bytes().startswith(b"\x89PNG")
        assert json.loads(output.with_suffix(".png.json").read_text())["visual"]["asleep"]
        # A second server must not remove or replace the live socket.
        collision = subprocess.run([binary, "--headless", "--debug-socket", str(path),
                                    "--frames", "1"], capture_output=True, timeout=3)
        assert collision.returncode != 0 and path.exists()
        assert debug.state()["capture"] == 0
        capture = debug.request("capture")
        wire.close()  # Abandon capture: disconnect should release it immediately.
        wire = connect(path, app)
        debug = module.Client(wire)
        resumed = debug.state()
        assert resumed["capture"] == 0
        assert resumed["ticks"] - capture["ticks"] < 5
        debug.press("WAKE")
        assert not debug.state()["visual"]["asleep"]
        wire.close()
        # A broken/partial request must not carry over to the next connection.
        wire = connect(path, app)
        wire.write(b"@J1 4 tap ")
        wire.close()
        wire = connect(path, app)
        assert module.Client(wire).state()["capture"] == 0
        wire.close()
    finally:
        app.terminate()
        app.communicate(timeout=3)
    path.unlink(missing_ok=True)  # SIGTERM is abrupt; normal exit is checked below.
    app = subprocess.Popen([binary, "--headless", "--debug-socket", str(path), "--frames", "50"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    wire = connect(path, app)
    assert module.Client(wire).state()["rendered"]
    wire.close()
    app.communicate(timeout=3)
    assert app.returncode == 0 and not path.exists()
    path.write_text("unrelated file")
    failed = subprocess.run([binary, "--headless", "--debug-socket", str(path), "--frames", "1"],
                            capture_output=True, timeout=3)
    assert failed.returncode != 0 and path.read_text() == "unrelated file"
print("PASS: local SDL input, capture, reconnect, abandoned capture, path ownership, clean exit")

"""Screen sleep in the real SDL app: desktop saves still land while the screen is dark,
capture is refused until it wakes, debug input wakes it, and the host logs both edges."""
import importlib.util
from pathlib import Path
import struct
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


def sequence(base):
    """Highest save sequence across both slots, or 0 before the first save."""
    best = 0
    for slot in (Path(str(base) + ".0"), Path(str(base) + ".1")):
        if slot.exists() and slot.stat().st_size >= 20:
            best = max(best, struct.unpack_from("<Q", slot.read_bytes(), 12)[0])
    return best


def wait_for(predicate, what):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError(f"timed out waiting for {what}")


with tempfile.TemporaryDirectory(prefix="jelli-screen-") as directory:
    socket, base = Path(directory) / "debug.sock", Path(directory) / "pet-save"
    app = subprocess.Popen([binary, "--pet", "--headless", "--debug-socket", str(socket),
                            "--save", str(base), "--frames", "6000"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        wire = connect(socket, app)
        debug = module.Client(wire)
        screen = debug.request("screen")["screen"]
        assert screen["awake"] and 0 < screen["sleep_in_ms"] <= screen["sleep_after_ms"] == 180000
        debug.request("cheat food 3")
        wait_for(lambda: sequence(base) > 0, "the first save")
        assert not debug.request("screen sleep")["screen"]["awake"]
        before, ticks = sequence(base), debug.state()["ticks"]
        debug.request("cheat food 5")  # Requests a save; not screen activity.
        wait_for(lambda: sequence(base) > before, "a save while the screen is dark")
        wait_for(lambda: debug.state()["ticks"] > ticks, "the game to advance while dark")
        assert not debug.state()["visual"]["screen"]["awake"]
        try:
            debug.screenshot(Path(directory) / "dark.png")
            raise AssertionError("capture of a dark screen accepted")
        except module.DebugError as exc:
            assert "screen_asleep" in str(exc), exc
        assert not (Path(directory) / "dark.png").exists()
        assert debug.request("tap 10 10")["input"] == "delivered"  # Trusted input wakes it.
        assert debug.request("screen")["screen"]["awake"]
        wait_for(lambda: debug.state().get("rendered"), "the wake's full redraw")
        token = debug.request("capture")["capture"]  # Accepted again once lit.
        assert token and debug.request(f"pixels {token} 0 4")["rgb565"]
        debug.request(f"release {token}")
        wire.close()
    finally:
        app.terminate()
        _, errors = app.communicate(timeout=5)
    assert "Screen asleep; double-click wakes" in errors, errors
    assert "Screen awake" in errors, errors
print("PASS: dark-screen saves, capture refusal, debug wake, and host edge logs")

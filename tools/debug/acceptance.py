"""Manual black-box acceptance test: invokes the public CLI as separate processes.

Run against an existing SDL socket first, then the connected ESP32 serial port.
Changes menu and sleep/wake state; leaves the menu closed and restores initial sleep state.
Logs and screenshots go to --output. No client internals or mocked transports are used.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]


def validate_png(path):
    png = path.read_bytes()
    assert png[:8] == b"\x89PNG\r\n\x1a\n", "invalid PNG signature"
    offset, compressed = 8, bytearray()
    while offset < len(png):
        length = struct.unpack_from(">I", png, offset)[0]
        kind, data = png[offset + 4:offset + 8], png[offset + 8:offset + 8 + length]
        assert zlib.crc32(kind + data) == struct.unpack_from(">I", png, offset + 8 + length)[0]
        if kind == b"IHDR":
            assert struct.unpack(">IIBBBBB", data) == (466, 466, 8, 2, 0, 0, 0)
        if kind == b"IDAT":
            compressed.extend(data)
        offset += length + 12
    raw = zlib.decompress(compressed)
    assert len(raw) == 466 * (466 * 3 + 1)
    assert raw[:4] == b"\0\0\0\0" and len(set(raw)) > 10, "blank/bad round framebuffer"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--socket")
    transport.add_argument("--port")
    parser.add_argument("--health", action="store_true", help="Validate all five clickers and pet effects")
    parser.add_argument("--tunables", action="store_true", help="Also validate the tunable extension")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    base = [str(ROOT / "scripts/jelli-debug"), "--socket" if args.socket else "--port",
            args.socket or args.port]
    report = {"transport": base[1:], "commands": [], "passed": False}

    def cli(*command, error=None):
        start = time.monotonic()
        result = subprocess.run(base + list(command), capture_output=True, text=True, timeout=40)
        report["commands"].append({"command": list(command), "seconds": round(time.monotonic() - start, 3),
                                   "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        if error:
            assert result.returncode != 0 and error in result.stderr, result.stderr
            return None
        assert result.returncode == 0, f"{command}: {result.stderr}"
        return json.loads(result.stdout)

    def state(page=None, opened=None):
        value = cli("state")
        for _ in range(30):
            if not value.get("transitioning"):
                break
            time.sleep(.05)
            value = cli("state")
        assert not value.get("transitioning")
        assert value["version"] == 1 and value["rendered"] and value["capture"] == 0
        assert (value["width"], value["height"]) == (466, 466)
        if page is not None:
            assert value["visual"]["page"] == page, value
        if opened is not None:
            assert value["visual"]["menu_open"] == opened, value
        return value

    try:
        initial = state()
        asleep = initial["visual"]["asleep"]
        if initial["visual"]["menu_open"]:
            for _ in range(3):
                if state()["visual"]["page"] == "home":
                    break
                cli("press", "0")
            cli("press", "CLOSE")
        closed = state("home", False)
        assert [b["label"] for b in cli("buttons")] == ["MENU"]
        cli("press", "CARE", error="Button must uniquely match")
        cli("press", "7", error="Button must uniquely match")
        cli("tap", "466", "0", error="invalid choice")
        assert state("home", False)["visual"]["asleep"] == asleep
        cli("press", "menu")
        assert len(state("home", True)["buttons"]) == 5
        cli("press", "CARE")
        assert "BACK" in [b["label"] for b in state("care", True)["buttons"]]
        cli("press", "0")
        state("home", True)
        cli("tap", "356", "111")
        moments = state("moments", True)
        assert {"BREAKFAST", "TEA", "GOING OUT", "MOVIE", "BACK"}.issubset(
            {b["label"] for b in moments["buttons"]})
        screenshot = args.output.resolve() / "moments.png"
        cli("screenshot", str(screenshot))
        validate_png(screenshot)
        captured = json.loads(screenshot.with_suffix(".png.json").read_text())
        assert captured["capture"] and captured["visual"]["page"] == "moments"
        state("moments", True)  # separate CLI process: released capture and reconnect work
        cli("press", "BACK")
        cli("press", "SETTINGS")
        cli("press", "WAKE" if asleep else "REST")
        assert state()["visual"]["asleep"] != asleep
        cli("press", "REST" if asleep else "WAKE")
        assert state()["visual"]["asleep"] == asleep
        if args.health:
            if asleep:
                cli("press", "WAKE")
            cli("press", "BACK")
            cli("press", "CARE")
            cli("press", "HEALTH")
            for label, page in (("BRUSH TEETH", "brush"), ("MEDICINE", "medicine"),
                                ("SHOT", "shot"), ("WASH", "wash"), ("STRETCH", "stretch")):
                cli("press", label)
                before = state(page, True)
                assert len(before["buttons"]) == 2 and before["visual"]["clicker_hits"] == 0
                for tap in range(36):
                    if before["visual"]["clicker_done"]:
                        break
                    cli("press", "1")
                    after = state(page, True)
                    assert after["visual"]["result"] == "ok", after
                    assert after["visual"]["bond"] > before["visual"]["bond"], after
                    before = after
                assert before["visual"]["clicker_done"]
                if page == "brush":
                    shot = args.output.resolve() / "healthy-clicker.png"
                    cli("screenshot", str(shot))
                    validate_png(shot)
                cli("press", "1")
                after = state(page, True)
                assert after["visual"]["result"] == "not ready"
                assert after["visual"]["bond"] == before["visual"]["bond"]
                cli("press", "BACK")
                state("health", True)
            cli("press", "0")
            state("care", True)
            cli("press", "BACK")
            state("home", True)
            if asleep:
                cli("press", "SETTINGS")
                cli("press", "REST")
                cli("press", "BACK")
        if state()["visual"]["page"] != "home":
            cli("press", "BACK")
        cli("press", "CLOSE")
        end = state("home", False)
        assert end["ticks"] >= closed["ticks"], "world clock regressed"
        if args.tunables:
            def values(reply):
                return {v["name"]: v for v in reply["tunables"]}
            pet_id = str(end["visual"]["pet_id"])
            baseline = values(cli("tunables"))
            creature_baseline = values(cli("tunables", "--creature", pet_id))
            try:
                assert baseline["pet.idle_frame_ms"]["default"] == 900
                assert baseline["ui.animation_scale_pct"]["default"] == 300
                cli("tune", "pet.idle_frame_ms", "1200")
                assert values(cli("tunables", "--creature", pet_id))["pet.idle_frame_ms"]["value"] == 1200
                cli("tune", "pet.idle_frame_ms", "1800", "--creature", pet_id)
                item = values(cli("tunables", "--creature", pet_id))["pet.idle_frame_ms"]
                assert item["value"] == 1800 and item["source"] == "creature"
                cli("tune", "pet.idle_frame_ms", "0", error="tunable_range")
                cli("tune", "unknown", "1", error="unknown_tunable")
                cli("tunables", "--creature", "4294967295", error="invalid_scope")
                cli("tune", "pet.idle_frame_ms", "reset", "--creature", pet_id)
                assert values(cli("tunables", "--creature", pet_id))["pet.idle_frame_ms"]["value"] == 1200
                cli("tap", "233", "332")
                tile = state()["visual"]
                assert tile["tile_phase"] == 0
            finally:
                original = creature_baseline["pet.idle_frame_ms"]
                cli("tune", "pet.idle_frame_ms", str(original["value"]) if original["source"] == "creature" else "reset",
                    "--creature", pet_id)
                original = baseline["pet.idle_frame_ms"]
                cli("tune", "pet.idle_frame_ms", str(original["value"]) if original["source"] == "global" else "reset")
        report["passed"] = True
        print(f"PASS: {len(report['commands'])} external CLI invocations; state, labels, numeric inputs, "
              "tap, rejection, PNG+state, release/reconnect, sleep/wake, BACK/CLOSE")
    finally:
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

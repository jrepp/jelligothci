# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Small request/response client for Jelligotchi local/USB serial debug protocol v1."""
import argparse
import json
from pathlib import Path
import secrets
import socket
import select
import struct
import sys
import time
import zlib


class DebugError(Exception):
    pass


class Client:
    def __init__(self, wire, timeout=3.0):
        self.wire = wire
        self.timeout = timeout
        self.sequence = secrets.randbelow(0xFFFFFFFE) + 1

    def request(self, command):
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        prefix = f"@J1 {self.sequence} ".encode("ascii")
        message = prefix + command.encode("ascii") + b"\n"
        if self.wire.write(message) != len(message):
            raise DebugError("Short write; input outcome unknown. Inspect state before retrying.")
        deadline = time.monotonic() + self.timeout
        line = bytearray()
        overflow = False
        while time.monotonic() < deadline:
            available = getattr(self.wire, "in_waiting", 0)
            data = self.wire.read(max(1, min(4096, available)))
            for byte in data:
                if byte == 10:
                    if not overflow and line.startswith(prefix):
                        try:
                            reply = json.loads(line[len(prefix):])
                        except (ValueError, UnicodeError) as exc:
                            raise DebugError("Damaged reply; input outcome unknown.") from exc
                        if not isinstance(reply, dict) or not isinstance(reply.get("ok"), bool):
                            raise DebugError("Invalid protocol reply")
                        if not reply["ok"]:
                            raise DebugError(str(reply.get("error", "device error")))
                        return reply
                    line.clear()
                    overflow = False
                elif len(line) < 4096:
                    line.append(byte)
                else:
                    overflow = True
        raise DebugError("Timed out; input may have executed. Inspect state before retrying. "
                         "Close other serial monitors and check that debug firmware is running.")

    def state(self):
        result = self.request("state")
        if result.get("version") != 1 or not result.get("rendered"):
            raise DebugError("Unsupported protocol or no rendered frame yet")
        return result

    def press(self, button):
        state = self.state()
        deadline = time.monotonic() + 3.0
        while state.get("transitioning"):
            if time.monotonic() >= deadline:
                raise DebugError("Menu transition has not settled; resume the game before pressing")
            time.sleep(.03)
            state = self.state()
        options = state["buttons"]
        matches = [item for item in options if str(item["id"]) == button
                   or item["label"].casefold() == button.casefold()]
        if len(matches) != 1:
            raise DebugError("Button must uniquely match a current label or index 0–6")
        item = matches[0]
        # The device validates the page before dispatching through normal UI hit testing.
        page = ["home", "care", "more", "collection", "settings", "moments", "health", "brush", "medicine", "shot", "wash", "stretch"].index(state["visual"]["page"])
        return self.request(f"press {page} {item['id']}")

    def screenshot(self, path):
        state = self.request("capture")
        token = state.get("capture", 0)
        if not token:
            raise DebugError("Device did not start a capture")
        try:
            width, height = state["width"], state["height"]
            if (width, height) != (466, 466):
                raise DebugError("Unsupported framebuffer size")
            pixels = bytearray()
            for offset in range(0, width * height, width):
                row = self.request(f"pixels {token} {offset} {width}")
                if row.get("capture") != token or row.get("offset") != offset:
                    raise DebugError("Mismatched screenshot chunk")
                data = row.get("rgb565", "")
                if not isinstance(data, str) or len(data) != width * 4:
                    raise DebugError("Invalid screenshot chunk length")
                try:
                    pixels.extend(bytes.fromhex(data))
                except ValueError as exc:
                    raise DebugError("Invalid screenshot pixels") from exc
            if len(pixels) != width * height * 2:
                raise DebugError("Incomplete screenshot")
        finally:
            # A failed release is reported; the device also expires abandoned captures.
            self.request(f"release {token}")
        path.write_bytes(png_rgb565(width, height, pixels))
        path.with_suffix(path.suffix + ".json").write_text(json.dumps(state, indent=2) + "\n")
        return {"png": str(path), "state": str(path.with_suffix(path.suffix + ".json"))}


def png_rgb565(width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG's unfiltered scanline marker.
        for x in range(width):
            index = (y * width + x) * 2
            value = (pixels[index] << 8) | pixels[index + 1]
            r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
            rows.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


class SocketWire:
    def __init__(self, path, timeout):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        try:
            self.socket.connect(path)
        except BaseException:
            self.socket.close()
            raise
        self.socket.settimeout(0.05)

    @property
    def in_waiting(self):
        return 4096 if select.select([self.socket], [], [], 0)[0] else 0

    def write(self, data):
        self.socket.sendall(data)
        return len(data)

    def read(self, size):
        try:
            data = self.socket.recv(size)
        except socket.timeout:
            return b""
        if not data:
            raise DebugError("Local game disconnected; input outcome may be unknown")
        return data

    def close(self):
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--port", help="Serial device; list candidates with 'ports'")
    transport.add_argument("--socket", help="Local SDL Unix socket; defaults to build/jelli-debug.sock")
    parser.add_argument("--timeout", type=float, default=3.0)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("ports", "state", "buttons"):
        commands.add_parser(name)
    press = commands.add_parser("press")
    press.add_argument("button", help="Current label (quote spaces) or index 0–6")
    tap = commands.add_parser("tap")
    tap.add_argument("x", type=int, choices=range(466), metavar="X[0..465]")
    tap.add_argument("y", type=int, choices=range(466), metavar="Y[0..465]")
    tunables = commands.add_parser("tunables", help="List tunable values, ranges, and sources")
    tunables.add_argument("--creature", type=int, default=0, help="Stable pet ID; default is global")
    tune = commands.add_parser("tune", help="Set a session override; use reset to inherit")
    tune.add_argument("name")
    tune.add_argument("value", help="Integer value or reset")
    tune.add_argument("--creature", type=int, default=0, help="Stable pet ID; default is global")
    events = commands.add_parser("events", help="Read discrete action/state events")
    events.add_argument("--after", type=int, choices=range(0x100000000), default=0, metavar="SEQUENCE")
    cheat = commands.add_parser("cheat", help="Explicit debug edits; recorded as cheat events")
    cheat.add_argument("name", choices=("fullness", "energy", "clean", "fun", "connection", "bond", "food", "gifts", "heal"))
    cheat.add_argument("value", type=int, nargs="?")
    sound = commands.add_parser("sound", help="Queue a tiny procedural pet sound")
    sound.add_argument("cue", choices=("chirp", "happy", "sparkle", "hello", "sleepy", "tap", "coo"))
    sound.add_argument("--volume", type=int, choices=range(81), default=35, metavar="0..80")
    screenshot = commands.add_parser("screenshot")
    screenshot.add_argument("output", type=Path, help="PNG path; also writes .png.json state")
    args = parser.parse_args()
    import serial
    from serial.tools import list_ports
    if args.command == "ports":
        print(json.dumps([{"port": p.device, "description": p.description, "hwid": p.hwid}
                          for p in list_ports.comports()], indent=2))
        return 0
    if not 0.1 <= args.timeout <= 30:
        parser.error("--timeout must be between 0.1 and 30 seconds")
    if args.command in ("tunables", "tune") and not 0 <= args.creature <= 0xFFFFFFFF:
        parser.error("creature must be an unsigned 32-bit ID")
    if args.command == "tune":
        if not args.name.isascii() or not all(part.replace("_", "").isalnum() for part in args.name.split(".")) or len(args.name) > 32:
            parser.error("invalid tunable name")
        if args.value != "reset" and (not args.value.isascii() or not args.value.isdecimal()
                                      or len(args.value) > 10 or int(args.value) > 0xFFFFFFFF):
            parser.error("tunable value must be an unsigned 32-bit integer or reset")
    if args.command == "cheat":
        if args.name == "heal":
            if args.value is not None:
                parser.error("heal takes no value")
        elif args.value is None or not 0 <= args.value <= (20 if args.name in ("food", "gifts") else 100):
            parser.error("cheat needs a value: 0–100 for needs/bond, 0–20 for inventory")
    wire = None
    try:
        if args.port:
            wire = serial.Serial(port=None, baudrate=115200, timeout=0.05, write_timeout=args.timeout)
            # Native ESP32 USB Serial/JTAG: keep both lines asserted.
            # Deasserting them on macOS caused a reset on every CLI open.
            wire.dtr = True
            wire.rts = True
            wire.port = args.port
            wire.open()
        else:
            wire = SocketWire(args.socket or str(Path(__file__).resolve().parents[2] / "build/jelli-debug.sock"),
                              args.timeout)
        client = Client(wire, args.timeout)
        if args.command == "cheat":
            result = client.request("cheat " + args.name + ("" if args.name == "heal" else f" {args.value}"))
        elif args.command == "state":
            result = client.state()
        elif args.command == "buttons":
            result = client.state()["buttons"]
        elif args.command == "press":
            result = client.press(args.button)
        elif args.command == "tunables":
            result = client.request(f"tunables {args.creature}")
        elif args.command == "tune":
            result = client.request(f"tune {args.creature} {args.name} {args.value}")
        elif args.command == "events":
            result = client.request(f"events {args.after}")
        elif args.command == "sound":
            cue = ("chirp", "happy", "sparkle", "hello", "sleepy", "tap", "coo").index(args.cue)
            result = client.request(f"sound {cue} {args.volume}")
        elif args.command == "tap":
            result = client.request(f"tap {args.x} {args.y}")
        else:
            result = client.screenshot(args.output)
        print(json.dumps(result, indent=2))
        return 0
    except (DebugError, serial.SerialException, OSError, KeyError, ValueError) as exc:
        print(f"jelli-debug: {exc}", file=sys.stderr)
        return 1
    finally:
        if wire is not None:
            wire.close()


if __name__ == "__main__":
    sys.exit(main())

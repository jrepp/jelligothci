# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Client for Loki Agent's @A1 USB serial console (RFC-003).

The agent frames requests like the pet's @J1 debug protocol (ADR-010) under its
own tag: "@A1 <id> <words>\\n" out, "\\n@A1 <id> <json>\\n" back. Logs share the
port, so only the line that starts with this request's prefix is the reply.
"""
import argparse
import getpass
import json
import secrets
import sys
import time


class AgentError(Exception):
    pass


class Client:
    def __init__(self, wire, timeout):
        self.wire = wire
        self.timeout = timeout
        self.sequence = secrets.randbelow(0xFFFFFFFE) + 1

    def request(self, command, extra_seconds=0.0):
        self.sequence = self.sequence % 0xFFFFFFFF + 1
        prefix = f"@A1 {self.sequence} ".encode("ascii")
        message = prefix + command.encode("ascii") + b"\n"
        if self.wire.write(message) != len(message):
            raise AgentError("Short write; the command may not have run.")
        deadline = time.monotonic() + self.timeout + extra_seconds
        line = bytearray()
        while time.monotonic() < deadline:
            for byte in self.wire.read(max(1, min(4096, getattr(self.wire, "in_waiting", 0)))):
                if byte != 10:
                    if len(line) < 4096:
                        line.append(byte)
                    continue
                if line.startswith(prefix):
                    return parse(bytes(line[len(prefix):]))
                line.clear()
        raise AgentError("Timed out. Is the agent running? Close other serial monitors.")


def parse(body):
    try:
        reply = json.loads(body)
    except (ValueError, UnicodeError) as exc:
        raise AgentError("Damaged reply") from exc
    if not isinstance(reply, dict) or not isinstance(reply.get("ok"), bool):
        raise AgentError("Invalid protocol reply")
    if not reply["ok"]:
        raise AgentError(str(reply.get("error", "device error")))
    return reply


def wifi_line(ssid, password):
    """The "wifi set" request. Credentials travel as hex because they may contain spaces;
    the device stores them in agent_nvs and never echoes them."""
    name, secret = ssid.encode("utf-8"), password.encode("utf-8")
    if not 1 <= len(name) <= 32:
        raise AgentError("The network name must be 1 to 32 bytes")
    if secret and not 8 <= len(secret) <= 63:
        raise AgentError("The password must be 8 to 63 bytes, or empty for an open network")
    return "wifi set " + name.hex() + (" " + secret.hex() if secret else "")


def setup_line(args):
    """Wi-Fi and audio set-up requests; returns (request, extra seconds)."""
    if args.command == "wifi":
        if args.action != "set":
            return f"wifi {args.action}", 0.0
        ssid = args.ssid if args.ssid is not None else input("Wi-Fi network name: ")
        return wifi_line(ssid, getpass.getpass("Password (empty for an open network): ")), 0.0
    return f"audio {args.action} {args.value}", args.value / 1000 if args.action == "test" else 0.0


def loki_line(args):
    """The link to Loki Bridge: pairing state and the commands for trying it without
    the board's interface. Pairing itself is bridge/'s `loki-bridge pair`."""
    if args.command == "pair":
        return f"pair {args.action}", 0.0
    # Checked here as the device does, so a stray space or newline cannot add words.
    if args.action == "answer":
        if args.value not in ("0", "1", "2", "3"):
            raise AgentError("loki answer needs the option number, 0 to 3")
        return f"loki answer {args.value}", 0.0
    if args.action == "ptt":
        if args.value not in ("start", "stop"):
            raise AgentError("loki ptt needs start or stop")
        return f"loki ptt {args.value}", 0.0
    if args.value is not None:
        raise AgentError(f"loki {args.action} takes no more words")
    return f"loki {args.action}", 0.0


def ui_line(args):
    """The unicorn's routines, a hold to talk and a pet (ADR-017), and Settings' side
    button, taps, and drags (ADR-016)."""
    values = list(args.values)
    if args.action == "prance":
        if values not in (["wander"], ["celebrate"], ["frolic"]):
            raise AgentError("ui prance takes wander, celebrate, or frolic")
        return f"ui prance {values[0]}"
    if args.action == "hold":
        if values not in (["start"], ["stop"]):
            raise AgentError("ui hold takes start or stop")
        return f"ui hold {values[0]}"
    if args.action in ("button", "pet"):
        if values:
            raise AgentError(f"ui {args.action} takes no more words")
        return f"ui {args.action}"
    if args.action == "tap":
        if len(values) != 2 or not all(v.isascii() and v.isdigit() and int(v) < 466 for v in values):
            raise AgentError("ui tap takes X and Y, each 0..465")
        return f"ui tap {int(values[0])} {int(values[1])}"
    if (len(values) != 2 or values[0] not in ("up", "down") or not values[1].isascii()
            or not values[1].isdigit()
            or not 1 <= int(values[1]) <= 466):
        raise AgentError("ui drag takes up or down and 1..466 pixels")
    return f"ui drag {values[0]} {int(values[1])}"


def command_line(args):
    """Returns the console command and the extra seconds it needs to finish."""
    if args.command == "hello":
        return "hello", 0.0
    if args.command == "boot":
        return f"boot {args.app}", 0.0
    if args.command in ("wifi", "audio"):
        return setup_line(args)
    if args.command in ("loki", "pair"):
        return loki_line(args)
    if args.command == "render":
        return f"render bench {args.mode} {args.seconds}", float(args.seconds)
    if args.command == "ui":
        return ui_line(args), 0.0
    if args.command == "screen":
        return f"screen {args.action}", 0.0
    if args.what in ("pwr", "te"):
        if args.ms is None:
            raise AgentError(f"probe {args.what} needs a duration in milliseconds")
        pull = getattr(args, "pull", "none")
        if pull != "none" and args.what != "pwr":
            raise AgentError("--pull applies to probe pwr only")
        return f"probe {args.what} {args.ms}" + ("" if pull == "none" else f" {pull}"), args.ms / 1000
    if args.ms is not None:
        raise AgentError(f"probe {args.what} takes no duration")
    return f"probe {args.what}", 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True, help="Serial device; list with scripts/esp ports")
    parser.add_argument("--timeout", type=float, default=3.0)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("hello", help="App, protocol, board, and version")
    probe = commands.add_parser("probe", help="Read-only hardware probes")
    probe.add_argument("what", choices=("chip", "heap", "pmu", "pwr", "te"))
    probe.add_argument("ms", type=int, nargs="?", help="Duration for pwr (2..10000) or te (100..5000)")
    probe.add_argument("--pull", choices=("none", "up"), default="none",
                       help="pwr only: turn on the key line's internal pull-up while sampling")
    boot = commands.add_parser("boot", help="Restart into another installed app")
    boot.add_argument("app", choices=("pet",))
    wifi = commands.add_parser("wifi", help="Wi-Fi; set prompts for the password")
    wifi.add_argument("action", choices=("set", "clear", "status"))
    wifi.add_argument("--ssid", help="Network name; prompted for when omitted")
    pair = commands.add_parser("pair", help="The paired Loki Bridge (pair with bridge/'s loki-bridge)")
    pair.add_argument("action", choices=("show", "status", "clear"))
    loki = commands.add_parser("loki", help="The link to Loki Bridge")
    loki.add_argument("action", choices=("status", "inbox", "answer", "ptt", "cancel"))
    loki.add_argument("value", nargs="?", help="ptt: start or stop; answer: the option, 0 to 3")
    ui = commands.add_parser("ui", help="Prance now; hold the screen to talk or let go; pet the "
                                        "unicorn; or press the side button, tap X Y, or drag "
                                        "up|down PX on the screen")
    ui.add_argument("action", choices=("prance", "hold", "pet", "button", "tap", "drag"))
    ui.add_argument("values", nargs="*", help="prance: wander|celebrate|frolic; hold: start|stop; "
                                              "tap: X Y; drag: up|down PX")
    screen = commands.add_parser("screen", help="Screen sleep: status, or sleep or wake it now")
    screen.add_argument("action", choices=("status", "sleep", "wake"))
    render = commands.add_parser("render", help="Time the 384 px unicorn animation (it stays on screen)")
    render.add_argument("action", choices=("bench",))
    render.add_argument("mode", choices=("full", "bob", "patch"))
    render.add_argument("seconds", type=int, choices=range(1, 11), metavar="SECONDS[1..10]")
    audio = commands.add_parser("audio", help="Speaker tone with capture levels, or speaker level")
    audio.add_argument("action", choices=("test", "volume"))
    audio.add_argument("value", type=int, help="test: 200..5000 ms; volume: 0..100")
    args = parser.parse_args()
    if not 0.1 <= args.timeout <= 30:
        parser.error("--timeout must be between 0.1 and 30 seconds")
    try:
        command, extra = command_line(args)  # Checked before any port opens.
    except AgentError as exc:
        print(f"loki-agent: {exc}", file=sys.stderr)
        return 1
    import serial
    wire = None
    try:
        wire = serial.Serial(port=None, baudrate=115200, timeout=0.05, write_timeout=args.timeout)
        # Native ESP32 USB Serial/JTAG: keep both lines asserted, as jelli-debug does;
        # deasserting them on macOS resets the board on every open.
        wire.dtr = True
        wire.rts = True
        wire.port = args.port
        wire.open()
        print(json.dumps(Client(wire, args.timeout).request(command, extra), indent=2))
        return 0
    except (AgentError, serial.SerialException, OSError) as exc:
        print(f"loki-agent: {exc}", file=sys.stderr)
        return 1
    finally:
        if wire is not None:
            wire.close()


if __name__ == "__main__":
    sys.exit(main())

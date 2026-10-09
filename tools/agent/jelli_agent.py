# /// script
# requires-python = ">=3.12"
# dependencies = ["pyserial==3.5"]
# ///
"""Client for the voice agent app's @A1 USB serial console (RFC-003 phase 0).

The agent frames requests like the pet's @J1 debug protocol (ADR-010) under its
own tag: "@A1 <id> <words>\\n" out, "\\n@A1 <id> <json>\\n" back. Logs share the
port, so only the line that starts with this request's prefix is the reply.
"""
import argparse
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


def command_line(args):
    """Returns the console command and the extra seconds it needs to finish."""
    if args.command == "hello":
        return "hello", 0.0
    if args.command == "boot":
        return f"boot {args.app}", 0.0
    if args.what in ("pwr", "te"):
        if args.ms is None:
            raise AgentError(f"probe {args.what} needs a duration in milliseconds")
        return f"probe {args.what} {args.ms}", args.ms / 1000
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
    boot = commands.add_parser("boot", help="Restart into another installed app")
    boot.add_argument("app", choices=("pet",))
    args = parser.parse_args()
    if not 0.1 <= args.timeout <= 30:
        parser.error("--timeout must be between 0.1 and 30 seconds")
    import serial
    wire = None
    try:
        command, extra = command_line(args)
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
        print(f"jelli-agent: {exc}", file=sys.stderr)
        return 1
    finally:
        if wire is not None:
            wire.close()


if __name__ == "__main__":
    sys.exit(main())

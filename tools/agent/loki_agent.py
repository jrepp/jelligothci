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
import getpass
import json
import re
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


# Dotted quad of 0..255 without leading zeros, as jelli_agent_ipv4_parse accepts.
IPV4 = re.compile(r"(?:(?:25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])\.){3}"
                  r"(?:25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])")


def wifi_line(ssid, password):
    """The "wifi set" request. Credentials travel as hex because they may contain spaces;
    the device stores them in agent_nvs and never echoes them."""
    name, secret = ssid.encode("utf-8"), password.encode("utf-8")
    if not 1 <= len(name) <= 32:
        raise AgentError("The network name must be 1 to 32 bytes")
    if secret and not 8 <= len(secret) <= 63:
        raise AgentError("The password must be 8 to 63 bytes, or empty for an open network")
    return "wifi set " + name.hex() + (" " + secret.hex() if secret else "")


def spike_line(args):
    """Requests for the phase 0 audio spike; returns (request, extra seconds)."""
    if args.command == "wifi":
        if args.action != "set":
            return f"wifi {args.action}", 0.0
        ssid = args.ssid if args.ssid is not None else input("Wi-Fi network name: ")
        return wifi_line(ssid, getpass.getpass("Password (empty for an open network): ")), 0.0
    if args.command == "link":
        if args.action == "status":
            return "link status", 0.0
        # Checked here as the device does, so a stray space or newline cannot add words.
        if args.tcp_port is None or args.host is None or not IPV4.fullmatch(args.host):
            raise AgentError("link set needs the Mac's IPv4 address and port")
        return f"link set {args.host} {args.tcp_port}", 0.0
    if args.command == "mic":
        # Streaming, then up to 15 s for a reply to finish playing, plus connection time.
        return f"mic stream {args.seconds}", args.seconds + 25.0
    return f"audio {args.action} {args.value}", args.value / 1000 if args.action == "test" else 0.0


def command_line(args):
    """Returns the console command and the extra seconds it needs to finish."""
    if args.command == "hello":
        return "hello", 0.0
    if args.command == "boot":
        return f"boot {args.app}", 0.0
    if args.command in ("wifi", "link", "mic", "audio"):
        return spike_line(args)
    if args.command == "render":
        return f"render bench {args.mode} {args.seconds}", float(args.seconds)
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
    wifi = commands.add_parser("wifi", help="Wi-Fi for the phase 0 spike; set prompts for the password")
    wifi.add_argument("action", choices=("set", "clear", "status"))
    wifi.add_argument("--ssid", help="Network name; prompted for when omitted")
    link = commands.add_parser("link", help="The Mac running tools/agent/spike_server.py")
    link.add_argument("action", choices=("set", "status"))
    link.add_argument("host", nargs="?", help="The Mac's IPv4 address")
    link.add_argument("tcp_port", type=int, nargs="?", choices=range(1, 65536), metavar="PORT")
    mic = commands.add_parser("mic", help="Stream the microphone to the Mac and play its reply")
    mic.add_argument("action", choices=("stream",))
    mic.add_argument("seconds", type=int, choices=range(1, 31), metavar="SECONDS[1..30]")
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
        print(f"loki-agent: {exc}", file=sys.stderr)
        return 1
    finally:
        if wire is not None:
            wire.close()


if __name__ == "__main__":
    sys.exit(main())

"""The agent console client against a scripted serial wire; no board or pyserial."""
from pathlib import Path
import argparse
import contextlib
import importlib.util
import io
import random
import sys
import unittest
import unittest.mock

SOURCE = Path(__file__).resolve().parents[1] / "tools/agent/loki_agent.py"
spec = importlib.util.spec_from_file_location("loki_agent", SOURCE)
agent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(agent)


class Wire:
    """Answers each written request with scripted bytes built from its prefix."""

    def __init__(self, respond):
        self.respond = respond
        self.written = []
        self.pending = b""

    def write(self, data):
        self.written.append(data)
        prefix = data[:data.index(b" ", 4) + 1]
        self.pending += self.respond(prefix)
        return len(data)

    @property
    def in_waiting(self):
        return len(self.pending)

    def read(self, size):
        data, self.pending = self.pending[:size], self.pending[size:]
        return data


class ClientTests(unittest.TestCase):
    def test_frames_requests_and_finds_the_reply_among_logs(self):
        wire = Wire(lambda prefix: b"I (12) agent: log line\n\n@A1 1 {\"ok\":true,\"other\":1}\n"
                    + b"\n" + prefix + b"{\"ok\":true,\"app\":\"agent\"}\n")
        client = agent.Client(wire, 1.0)
        self.assertEqual(client.request("hello"), {"ok": True, "app": "agent"})
        sent = wire.written[0]
        self.assertTrue(sent.startswith(b"@A1 ") and sent.endswith(b" hello\n"))

    def test_ids_advance_and_wrap_without_reaching_zero(self):
        client = agent.Client(Wire(lambda prefix: b"\n" + prefix + b"{\"ok\":true}\n"), 1.0)
        client.sequence = 0xFFFFFFFE
        client.request("hello")
        client.request("hello")
        self.assertEqual([line.split(b" ")[1] for line in client.wire.written], [b"4294967295", b"1"])

    def test_device_errors_and_damaged_replies_raise(self):
        cases = ((b"{\"ok\":false,\"error\":\"range\"}", "range"), (b"{\"ok\":tru", "Damaged reply"),
                 (b"[1,2]", "Invalid protocol reply"), (b"{\"ok\":1}", "Invalid protocol reply"))
        for body, message in cases:
            with self.subTest(body=body):
                client = agent.Client(Wire(lambda prefix, body=body: b"\n" + prefix + body + b"\n"), 1.0)
                with self.assertRaisesRegex(agent.AgentError, message):
                    client.request("probe te 100")

    def test_a_reply_for_another_request_times_out(self):
        client = agent.Client(Wire(lambda prefix: b"\n@A1 7 {\"ok\":true}\n"), 0.2)
        client.sequence = 1
        with self.assertRaisesRegex(agent.AgentError, "Timed out"):
            client.request("hello")


class TricklingWire(Wire):
    """Delivers at most `chunk` bytes per read and reports nothing waiting, like a slow port."""

    def __init__(self, respond, chunk=1):
        super().__init__(respond)
        self.chunk = chunk
        self.reads = 0

    @property
    def in_waiting(self):
        return 0

    def read(self, size):
        self.reads += 1
        return Wire.read(self, min(size, self.chunk))


class StreamTests(unittest.TestCase):
    """Framing against what really shares the port: logs, garbage, and partial reads."""

    def test_a_reply_split_across_single_byte_reads_is_reassembled(self):
        reply = b"{\"ok\":true,\"app\":\"agent\"}"
        wire = TricklingWire(lambda prefix: b"I (5) log\n\n" + prefix + reply + b"\n")
        self.assertEqual(agent.Client(wire, 2.0).request("hello"), {"ok": True, "app": "agent"})
        self.assertGreater(wire.reads, len(reply))

    def test_a_wire_without_in_waiting_reads_one_byte_at_a_time(self):
        class Bare:
            def __init__(self):
                self.pending, self.sizes = b"", []

            def write(self, data):
                self.pending += b"\n" + data[:data.index(b" ", 4) + 1] + b"{\"ok\":true}\n"
                return len(data)

            def read(self, size):
                self.sizes.append(size)
                data, self.pending = self.pending[:size], self.pending[size:]
                return data

        wire = Bare()
        self.assertEqual(agent.Client(wire, 2.0).request("hello"), {"ok": True})
        self.assertEqual(set(wire.sizes), {1})

    def test_oversized_log_lines_without_lf_do_not_hide_the_reply(self):
        for size in (4095, 4096, 4097, 50000):
            with self.subTest(size=size):
                wire = Wire(lambda prefix, size=size: b"x" * size + b"\n" + prefix + b"{\"ok\":true}\n")
                self.assertEqual(agent.Client(wire, 2.0).request("hello"), {"ok": True})

    def test_a_reply_cannot_hide_behind_a_long_unterminated_prefix(self):
        # The device always sends LF before a reply; without it the prefix is mid-line.
        wire = Wire(lambda prefix: b"x" * 5000 + prefix + b"{\"ok\":true}\n")
        with self.assertRaisesRegex(agent.AgentError, "Timed out"):
            agent.Client(wire, 0.2).request("hello")

    def test_an_oversized_reply_is_truncated_and_reported_damaged(self):
        body = b"{\"ok\":true,\"pad\":\"" + b"a" * 5000 + b"\"}"
        wire = Wire(lambda prefix: b"\n" + prefix + body + b"\n")
        with self.assertRaisesRegex(agent.AgentError, "Damaged reply"):
            agent.Client(wire, 2.0).request("hello")

    def test_non_utf8_garbage_before_the_reply_is_skipped(self):
        garbage = bytes(range(256)).replace(b"\n", b"") + b"\xff\xfe\xc3("
        wire = Wire(lambda prefix: garbage + b"\n" + garbage + b"\n\n" + prefix + b"{\"ok\":true}\n")
        self.assertEqual(agent.Client(wire, 2.0).request("hello"), {"ok": True})

    def test_non_utf8_inside_the_reply_is_damaged_not_a_crash(self):
        wire = Wire(lambda prefix: b"\n" + prefix + b"{\"ok\":\xff}\n")
        with self.assertRaisesRegex(agent.AgentError, "Damaged reply"):
            agent.Client(wire, 2.0).request("hello")

    def test_a_short_write_is_reported_before_reading(self):
        class Short(Wire):
            def write(self, data):
                super().write(data)
                return len(data) - 1

        wire = Short(lambda prefix: b"\n" + prefix + b"{\"ok\":true}\n")
        with self.assertRaisesRegex(agent.AgentError, "Short write"):
            agent.Client(wire, 2.0).request("hello")

    def test_a_longer_id_with_the_same_digits_is_not_this_reply(self):
        # "@A1 1 " must not match "@A1 12 ": the trailing space is part of the prefix.
        wire = Wire(lambda prefix: b"\n" + prefix[:-1] + b"2 {\"ok\":true}\n")
        client = agent.Client(wire, 0.2)
        client.sequence = 0
        with self.assertRaisesRegex(agent.AgentError, "Timed out"):
            client.request("hello")

    def test_extra_seconds_extend_the_deadline(self):
        class Clock:
            now = 0.0

            def monotonic(self):
                self.now += 0.5
                return self.now

        class Late(Wire):
            def read(self, size):
                self.reads = getattr(self, "reads", 0) + 1
                return super().read(size) if self.reads >= 4 else b""

        original = agent.time
        self.addCleanup(setattr, agent, "time", original)
        for extra, succeeds in ((0.0, False), (2.0, True)):
            with self.subTest(extra=extra):
                agent.time = Clock()
                wire = Late(lambda prefix: b"\n" + prefix + b"{\"ok\":true}\n")
                client = agent.Client(wire, 1.0)
                if succeeds:
                    self.assertEqual(client.request("probe te 2000", extra), {"ok": True})
                else:
                    with self.assertRaisesRegex(agent.AgentError, "Timed out"):
                        client.request("probe te 2000", extra)


class CommandLineTests(unittest.TestCase):
    def args(self, **values):
        return argparse.Namespace(**{"command": "probe", "what": None, "ms": None, **values})

    def test_timed_probes_need_a_duration_and_extend_the_wait(self):
        self.assertEqual(agent.command_line(self.args(what="pwr", ms=5000)), ("probe pwr 5000", 5.0))
        self.assertEqual(agent.command_line(self.args(what="te", ms=1000)), ("probe te 1000", 1.0))
        with self.assertRaisesRegex(agent.AgentError, "needs a duration"):
            agent.command_line(self.args(what="te"))

    def test_snapshots_take_no_duration(self):
        self.assertEqual(agent.command_line(self.args(what="pmu")), ("probe pmu", 0.0))
        with self.assertRaisesRegex(agent.AgentError, "takes no duration"):
            agent.command_line(self.args(what="chip", ms=10))

    def test_main_rejects_bad_arguments_before_opening_any_port(self):
        cases = (["--timeout", "0", "hello"], ["--timeout", "31", "hello"], ["boot", "agent"],
                 ["probe", "flash"], ["probe", "te", "soon"], [])
        for arguments in cases:
            with self.subTest(arguments=arguments):
                argv = ["loki-agent", "--port", "/dev/null", *arguments]
                with unittest.mock.patch.object(sys, "argv", argv), \
                        contextlib.redirect_stderr(io.StringIO()) as errors, \
                        self.assertRaises(SystemExit) as raised:
                    agent.main()
                self.assertEqual(raised.exception.code, 2)
                self.assertIn("usage:", errors.getvalue())

    def test_pull_adds_a_word_to_probe_pwr_only(self):
        for pull, expected in (("none", "probe pwr 500"), ("up", "probe pwr 500 up")):
            with self.subTest(pull=pull):
                self.assertEqual(agent.command_line(self.args(what="pwr", ms=500, pull=pull)), (expected, 0.5))
        # Namespaces built without the option (older callers) behave as "none".
        self.assertEqual(agent.command_line(self.args(what="pwr", ms=500)), ("probe pwr 500", 0.5))
        self.assertEqual(agent.command_line(self.args(what="te", ms=100, pull="none")), ("probe te 100", 0.1))
        with self.assertRaisesRegex(agent.AgentError, "pwr only"):
            agent.command_line(self.args(what="te", ms=100, pull="up"))
        # The duration is still required, and checked first.
        with self.assertRaisesRegex(agent.AgentError, "needs a duration"):
            agent.command_line(self.args(what="pwr", pull="up"))

    def test_pull_is_checked_before_any_port_opens(self):
        class NoSerial:
            def __init__(self, **_):
                raise AssertionError("a port was opened")

        fake = type(sys)("serial")
        fake.Serial, fake.SerialException = NoSerial, OSError
        for arguments, code in ((["probe", "te", "100", "--pull", "up"], 1),
                                (["probe", "pwr", "500", "--pull", "sideways"], 2),
                                (["probe", "pwr", "500", "--pull", "down"], 2),
                                (["probe", "pwr", "500", "--pull"], 2)):
            with self.subTest(arguments=arguments):
                argv = ["loki-agent", "--port", "/dev/null", *arguments]
                with unittest.mock.patch.dict(sys.modules, {"serial": fake}), \
                        unittest.mock.patch.object(sys, "argv", argv), \
                        contextlib.redirect_stderr(io.StringIO()) as err:
                    if code == 2:
                        with self.assertRaises(SystemExit) as raised:
                            agent.main()
                        self.assertEqual(raised.exception.code, 2)
                    else:
                        self.assertEqual(agent.main(), 1)
                        self.assertIn("--pull applies to probe pwr only", err.getvalue())

    def test_pull_reaches_the_wire_after_the_duration(self):
        class FakeSerial:
            written = b""

            def __init__(self, **_):
                self.pending = b""

            def open(self):
                pass

            def close(self):
                pass

            def write(self, data):
                FakeSerial.written += data
                self.pending += b"\n" + data[:data.index(b" ", 4) + 1] + b"{\"ok\":true}\n"
                return len(data)

            @property
            def in_waiting(self):
                return len(self.pending)

            def read(self, size):
                data, self.pending = self.pending[:size], self.pending[size:]
                return data

        fake = type(sys)("serial")
        fake.Serial, fake.SerialException = FakeSerial, OSError
        argv = ["loki-agent", "--port", "/dev/null", "probe", "pwr", "200", "--pull", "up"]
        with unittest.mock.patch.dict(sys.modules, {"serial": fake}), \
                unittest.mock.patch.object(sys, "argv", argv), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(agent.main(), 0)
        self.assertRegex(FakeSerial.written, rb"^@A1 \d+ probe pwr 200 up\n$")

    def test_no_pull_down_for_the_pwr_key(self):
        # Pulling GPIO3 low could read as a press of PWRON; 6 s of that powers off.
        argv = ["loki-agent", "--port", "/dev/null", "probe", "pwr", "200", "--pull", "down"]
        with unittest.mock.patch.object(sys, "argv", argv), \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            agent.main()
        self.assertEqual(raised.exception.code, 2)

    def test_render_bench_waits_for_its_duration(self):
        for mode in ("full", "bob", "patch"):
            with self.subTest(mode=mode):
                args = self.args(command="render", action="bench", mode=mode, seconds=5)
                self.assertEqual(agent.command_line(args), (f"render bench {mode} 5", 5.0))
        for arguments in (["render", "bench", "spin", "5"], ["render", "bench", "full", "0"],
                          ["render", "bench", "full", "11"], ["render", "go", "full", "5"]):
            with self.subTest(arguments=arguments):
                argv = ["loki-agent", "--port", "/dev/null", *arguments]
                with unittest.mock.patch.object(sys, "argv", argv), \
                        contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                    agent.main()
                self.assertEqual(raised.exception.code, 2)

    def test_hello_and_boot(self):
        self.assertEqual(agent.command_line(self.args(command="hello")), ("hello", 0.0))
        self.assertEqual(agent.command_line(self.args(command="boot", app="pet")), ("boot pet", 0.0))


class SpikeCommandTests(unittest.TestCase):
    """Phase 0 spike requests: Wi-Fi credentials, the Mac's address, streaming, audio."""

    def args(self, **values):
        return argparse.Namespace(**values)

    def test_credentials_travel_as_hex_with_their_spaces(self):
        self.assertEqual(agent.wifi_line("Home Net", "pass word"),
                         "wifi set " + b"Home Net".hex() + " " + b"pass word".hex())
        self.assertEqual(agent.wifi_line("caf\u00e9", ""), "wifi set " + "caf\u00e9".encode().hex())
        self.assertEqual(agent.wifi_line("x" * 32, "p" * 63).split()[2:], ["78" * 32, "70" * 63])

    def test_credential_lengths_follow_802_11(self):
        for ssid, password in (("", "password"), ("x" * 33, "password"), ("\u00e9" * 17, ""),
                               ("net", "short"), ("net", "p" * 64)):
            with self.subTest(ssid=ssid, password=len(password)):
                with self.assertRaises(agent.AgentError):
                    agent.wifi_line(ssid, password)

    def test_wifi_set_prompts_for_the_password_without_echo(self):
        args = self.args(command="wifi", action="set", ssid=None)
        with unittest.mock.patch("builtins.input", return_value="Home") as asked, \
                unittest.mock.patch.object(agent.getpass, "getpass", return_value="hunter2hunter2") as hidden:
            request, extra = agent.command_line(args)
        asked.assert_called_once()
        hidden.assert_called_once()
        self.assertEqual(request, agent.wifi_line("Home", "hunter2hunter2"))
        self.assertEqual(extra, 0.0)

    def test_other_spike_requests(self):
        cases = ((dict(command="wifi", action="status", ssid=None), ("wifi status", 0.0)),
                 (dict(command="wifi", action="clear", ssid=None), ("wifi clear", 0.0)),
                 (dict(command="link", action="set", host="192.168.68.66", tcp_port=8765),
                  ("link set 192.168.68.66 8765", 0.0)),
                 (dict(command="link", action="status", host=None, tcp_port=None), ("link status", 0.0)),
                 (dict(command="mic", action="stream", seconds=8), ("mic stream 8", 33.0)),
                 (dict(command="audio", action="test", value=2000), ("audio test 2000", 2.0)),
                 (dict(command="audio", action="volume", value=20), ("audio volume 20", 0.0)))
        for values, expected in cases:
            with self.subTest(values=values):
                self.assertEqual(agent.command_line(self.args(**values)), expected)
        with self.assertRaisesRegex(agent.AgentError, "IPv4 address and port"):
            agent.command_line(self.args(command="link", action="set", host="192.168.68.66", tcp_port=None))

    def test_the_password_reaches_the_wire_but_is_never_printed(self):
        secret = "correct horse battery"

        class FakeSerial:
            written = b""

            def __init__(self, **_):
                self.pending = b""

            def open(self):
                pass

            def close(self):
                pass

            def write(self, data):
                FakeSerial.written += data
                prefix = data[:data.index(b" ", 4) + 1]
                self.pending += b"\n" + prefix + b"{\"ok\":true,\"wifi\":\"saved\"}\n"
                return len(data)

            @property
            def in_waiting(self):
                return len(self.pending)

            def read(self, size):
                data, self.pending = self.pending[:size], self.pending[size:]
                return data

        fake = type(sys)("serial")
        fake.Serial, fake.SerialException = FakeSerial, OSError
        argv = ["loki-agent", "--port", "/dev/null", "wifi", "set", "--ssid", "Home"]
        with unittest.mock.patch.dict(sys.modules, {"serial": fake}), \
                unittest.mock.patch.object(sys, "argv", argv), \
                unittest.mock.patch.object(agent.getpass, "getpass", return_value=secret), \
                contextlib.redirect_stdout(io.StringIO()) as out, \
                contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(agent.main(), 0)
        self.assertIn(secret.encode().hex().encode(), FakeSerial.written)
        for text in (out.getvalue(), err.getvalue()):
            self.assertNotIn(secret, text)
            self.assertNotIn(secret.encode().hex(), text)
        self.assertIn('"saved"', out.getvalue())

    def test_spike_arguments_are_checked_before_any_port_opens(self):
        cases = (["link", "set", "192.168.68.66", "70000"], ["mic", "stream", "0"], ["mic", "stream", "31"],
                 ["wifi", "forget"], ["audio", "loud", "1"], ["mic", "record", "5"])
        for arguments in cases:
            with self.subTest(arguments=arguments):
                argv = ["loki-agent", "--port", "/dev/null", *arguments]
                with unittest.mock.patch.object(sys, "argv", argv), \
                        contextlib.redirect_stderr(io.StringIO()), \
                        self.assertRaises(SystemExit) as raised:
                    agent.main()
                self.assertEqual(raised.exception.code, 2)

    def test_link_hosts_follow_the_device_parser(self):
        good = ("0.0.0.0", "192.168.68.66", "255.255.255.255", "10.0.0.1")
        bad = ("", "1.2.3", "1.2.3.4.", "01.2.3.4", "256.1.1.1", "1.2.3.1000", "1.2.3.4 9",
               "1.2.3.4\n@A1 9 boot pet", "1.2.3.4\n", "h\u00e9llo", "\u0661.\u0662.\u0663.\u0664",
               "\uff11.2.3.4", "localhost", "-1.2.3.4", "1.2.3.+4")
        for host in good + bad:
            with self.subTest(host=host):
                args = self.args(command="link", action="set", host=host, tcp_port=80)
                if host in good:
                    self.assertEqual(agent.command_line(args), (f"link set {host} 80", 0.0))
                else:
                    with self.assertRaisesRegex(agent.AgentError, "IPv4 address and port"):
                        agent.command_line(args)

    def test_link_hosts_match_a_reference_over_random_text(self):
        def reference(text):
            parts = text.split(".")
            return len(parts) == 4 and all(
                0 < len(part) <= 3 and all("0" <= c <= "9" for c in part)
                and (part == "0" or part[0] != "0") and int(part) <= 255 for part in parts)

        generator = random.Random(2026)
        valid = ["0", "7", "10", "199", "255"]
        pieces = valid + ["00", "07", "256", "1000", "", " ", "\n", "+", "\u0663", "a"]
        accepted = 0
        for case in range(4000):
            count = generator.choice((4, 4, 4, 3, 5))
            text = ".".join(generator.choice(valid) if generator.random() < 0.8
                            else generator.choice(pieces) for _ in range(count))
            if generator.random() < 0.1:
                text += chr(generator.randrange(1, 256))
            with self.subTest(case=case, text=text):
                expected = reference(text)
                accepted += expected
                self.assertEqual(agent.IPV4.fullmatch(text) is not None, expected)
        self.assertGreater(accepted, 400)

    def test_a_bad_host_fails_before_any_port_opens(self):
        class NoSerial:
            def __init__(self, **_):
                raise AssertionError("a port was opened")

        fake = type(sys)("serial")
        fake.Serial, fake.SerialException = NoSerial, OSError
        argv = ["loki-agent", "--port", "/dev/null", "link", "set", "h\u00e9llo", "8765"]
        with unittest.mock.patch.dict(sys.modules, {"serial": fake}), \
                unittest.mock.patch.object(sys, "argv", argv), \
                contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(agent.main(), 1)
        self.assertIn("IPv4 address and port", err.getvalue())

    def test_a_given_ssid_is_not_asked_for(self):
        args = self.args(command="wifi", action="set", ssid="Home")
        with unittest.mock.patch("builtins.input") as asked, \
                unittest.mock.patch.object(agent.getpass, "getpass", return_value="") as hidden:
            self.assertEqual(agent.command_line(args), ("wifi set " + b"Home".hex(), 0.0))
        asked.assert_not_called()
        hidden.assert_called_once()

    def test_password_lengths_at_the_boundaries(self):
        for size, allowed in ((7, False), (8, True), (63, True), (64, False)):
            with self.subTest(size=size):
                if allowed:
                    self.assertEqual(agent.wifi_line("n", "p" * size).split()[3], "70" * size)
                else:
                    with self.assertRaises(agent.AgentError):
                        agent.wifi_line("n", "p" * size)
        # Bytes, not characters: four 2-byte letters make an 8-byte password.
        self.assertEqual(agent.wifi_line("n", "\u00e9" * 4).split()[3], "c3a9" * 4)

    def test_the_longest_wifi_request_fits_one_console_line(self):
        # include/jelli/agent_console.h: 255 bytes before the LF, at most 8 words.
        line = f"@A1 {0xFFFFFFFF} " + agent.wifi_line("\u00e9" * 16, "\u00e9" * 31 + "x")
        self.assertEqual(len(line.encode("ascii")), 215)
        self.assertEqual(len(line.split()), 6)

    def test_waits_cover_the_device_work(self):
        cases = ((dict(command="mic", action="stream", seconds=1), 26.0),
                 (dict(command="mic", action="stream", seconds=30), 55.0),
                 (dict(command="audio", action="test", value=200), 0.2),
                 (dict(command="audio", action="test", value=5000), 5.0),
                 (dict(command="audio", action="volume", value=100), 0.0))
        for values, extra in cases:
            with self.subTest(values=values):
                self.assertAlmostEqual(agent.command_line(self.args(**values))[1], extra)


if __name__ == "__main__":
    unittest.main()

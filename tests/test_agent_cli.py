"""The agent console client against a scripted serial wire; no board or pyserial."""
from pathlib import Path
import argparse
import contextlib
import importlib.util
import io
import sys
import unittest
import unittest.mock

SOURCE = Path(__file__).resolve().parents[1] / "tools/agent/jelli_agent.py"
spec = importlib.util.spec_from_file_location("jelli_agent", SOURCE)
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
                argv = ["jelli-agent", "--port", "/dev/null", *arguments]
                with unittest.mock.patch.object(sys, "argv", argv), \
                        contextlib.redirect_stderr(io.StringIO()) as errors, \
                        self.assertRaises(SystemExit) as raised:
                    agent.main()
                self.assertEqual(raised.exception.code, 2)
                self.assertIn("usage:", errors.getvalue())

    def test_hello_and_boot(self):
        self.assertEqual(agent.command_line(self.args(command="hello")), ("hello", 0.0))
        self.assertEqual(agent.command_line(self.args(command="boot", app="pet")), ("boot pet", 0.0))


if __name__ == "__main__":
    unittest.main()

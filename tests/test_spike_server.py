"""The phase 0 spike server's audio conversion, stream statistics, sessions, and Chatter
rendering against fakes; no network, no websockets package, no Chatter."""
from pathlib import Path
import argparse
import asyncio
import contextlib
import importlib.util
import itertools
import io
import json
import os
import random
import struct
import sys
import tempfile
import types
import unittest
import unittest.mock
import urllib.error
import wave

SOURCE = Path(__file__).resolve().parents[1] / "tools/agent/spike_server.py"
spec = importlib.util.spec_from_file_location("spike_server", SOURCE)
spike = importlib.util.module_from_spec(spec)
spec.loader.exec_module(spike)


def write_wav(path, rate, width, channels, samples):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(channels)
        out.setsampwidth(width)
        out.setframerate(rate)
        out.writeframes(b"".join(value.to_bytes(width, "little", signed=True) for value in samples))


class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "in.wav"

    def test_24_bit_mono_keeps_the_top_16_bits(self):
        # Chatter writes 24 kHz, 24-bit mono.
        write_wav(self.path, 24000, 3, 1, [0, 256, -256, 0x7FFFFF, -0x800000, 255])
        pcm = spike.to_int16_mono(self.path)
        self.assertEqual(struct.unpack("<6h", pcm), (0, 1, -1, 32767, -32768, 0))

    def test_16_bit_stereo_averages_channels(self):
        write_wav(self.path, 24000, 2, 2, [1000, 3000, -32768, -32768, 32767, 32767, 1, -2])
        self.assertEqual(struct.unpack("<4h", spike.to_int16_mono(self.path)), (2000, -32768, 32767, 0))

    def test_other_rates_and_widths_are_refused(self):
        for rate, width in ((16000, 2), (48000, 3), (24000, 1), (24000, 4)):
            with self.subTest(rate=rate, width=width):
                write_wav(self.path, rate, width, 1, [0, 1])
                with self.assertRaises(spike.SpikeError):
                    spike.to_int16_mono(self.path)


class RecordingTests(unittest.TestCase):
    def test_statistics_report_gaps_order_and_levels(self):
        with tempfile.TemporaryDirectory() as directory:
            recording = spike.Recording(Path(directory), 2)
            frame = struct.pack("<4h", 16384, 0, -16384, 0)  # mic at -6 dBFS, silent reference
            for sequence in (0, 1, 3, 2, 4, 4, 7):
                recording.add(sequence, frame)
            report = recording.close({"type": "stream_end"})
            self.assertEqual(report["frames"], 7)
            self.assertEqual(report["missing_frames"], 2)  # 5 and 6; the late 2 is not missing
            self.assertEqual(report["duplicates"], 1)
            self.assertEqual(report["out_of_order"], 1)  # 3 -> 2
            self.assertEqual(report["rms_dbfs"][0], -6.0)
            self.assertLess(report["rms_dbfs"][1], -90)
            self.assertAlmostEqual(report["seconds"], 14 / 24000)
            saved = json.loads(Path(report["wav"]).with_suffix(".json").read_text())
            self.assertEqual(saved["frames"], 7)
            with wave.open(report["wav"]) as wav:
                self.assertEqual((wav.getnchannels(), wav.getframerate(), wav.getnframes()), (2, 24000, 14))

    def test_header_layout_matches_the_firmware(self):
        # tests/test_agent_args.c pins the same twelve bytes on the device side.
        header = bytes([0x4A, 0x41, 1, 2, 0x04, 0x03, 0x02, 0x01, 0xD0, 0xC0, 0xB0, 0xA0])
        self.assertEqual(spike.HEADER.unpack(header), (b"JA", 1, 2, 0x01020304, 0xA0B0C0D0))
        self.assertEqual(spike.HEADER.size, 12)

    def test_an_empty_recording_reports_no_gaps_and_silence(self):
        with tempfile.TemporaryDirectory() as directory:
            report = spike.Recording(Path(directory), 2).close({"type": "disconnected"})
            self.assertEqual((report["frames"], report["missing_frames"], report["duplicates"],
                              report["out_of_order"], report["seconds"]), (0, 0, 0, 0, 0.0))
            self.assertEqual(report["arrival_ms"], {"mean": None, "p95": None, "max": None})
            self.assertEqual(report["rms_dbfs"], [-90.3, -90.3])
            self.assertEqual(report["device"], {"type": "disconnected"})
            with wave.open(report["wav"]) as wav:
                self.assertEqual(wav.getnframes(), 0)

    def test_arrival_intervals_come_from_the_monotonic_clock(self):
        clock = FakeClock()
        with tempfile.TemporaryDirectory() as directory, \
                unittest.mock.patch.object(spike, "time", clock):
            recording = spike.Recording(Path(directory), 1)
            for sequence in range(22):  # intervals of 1..21 ms
                clock.now += sequence / 1000
                recording.add(sequence, struct.pack("<h", 0))
            report = recording.close({})
        self.assertEqual(report["arrival_ms"], {"mean": 11.0, "p95": 20.0, "max": 21.0})
        self.assertEqual(report["missing_frames"], 0)


class ConversionPropertyTests(unittest.TestCase):
    """Seeded so a failure names a replayable case."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "in.wav"

    def test_16_bit_mono_round_trips_and_24_bit_keeps_the_top_bits(self):
        for seed in range(40):
            generator = random.Random(seed)
            count = generator.randrange(0, 300)
            with self.subTest(seed=seed):
                values = [generator.randrange(-32768, 32768) for _ in range(count)]
                write_wav(self.path, 24000, 2, 1, values)
                self.assertEqual(spike.to_int16_mono(self.path), struct.pack(f"<{count}h", *values))
                wide = [generator.randrange(-(1 << 23), 1 << 23) for _ in range(count)]
                write_wav(self.path, 24000, 3, 1, wide)
                self.assertEqual(spike.to_int16_mono(self.path),
                                 struct.pack(f"<{count}h", *(value >> 8 for value in wide)))

    def test_copying_mono_into_every_channel_changes_nothing(self):
        for seed in range(20):
            generator = random.Random(seed)
            width = generator.choice((2, 3))
            limit = 1 << (8 * width - 1)
            values = [generator.randrange(-limit, limit) for _ in range(generator.randrange(1, 200))]
            write_wav(self.path, 24000, width, 1, values)
            mono = spike.to_int16_mono(self.path)
            for channels in (2, 3, 4):
                with self.subTest(seed=seed, channels=channels):
                    write_wav(self.path, 24000, width, channels,
                              [value for value in values for _ in range(channels)])
                    self.assertEqual(spike.to_int16_mono(self.path), mono)


class FakeClock:
    """Stands in for the time module and asyncio.sleep: sleeping advances the clock."""

    def __init__(self, now=1000.0):
        self.now = now
        self.sleeps = []

    def monotonic(self):
        return self.now

    def time(self):
        return 1_700_000_000 + self.now

    @staticmethod
    def strftime(pattern):
        return "20260101-000000"

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.now += seconds

    async def async_sleep(self, seconds):
        self.sleep(seconds)
        await asyncio.sleep(0)


class ConnectionClosed(Exception):
    """The stub for websockets.exceptions.ConnectionClosed."""


def websockets_stub():
    package, exceptions = types.ModuleType("websockets"), types.ModuleType("websockets.exceptions")
    exceptions.ConnectionClosed = ConnectionClosed
    package.exceptions = exceptions
    return {"websockets": package, "websockets.exceptions": exceptions}


class Pause:
    """In a FakeConnection's script: let the event loop run for this long."""

    def __init__(self, seconds):
        self.seconds = seconds


class FakeConnection:
    """Yields scripted messages, records what the server sends, and can close abruptly."""
    remote_address = ("192.0.2.7", 50000)

    def __init__(self, incoming, close_with=None, fail_after=None, clock=None):
        self.incoming, self.close_with, self.fail_after, self.clock = incoming, close_with, fail_after, clock
        self.sent, self.times = [], []

    async def send(self, data):
        if self.fail_after is not None and len(self.sent) >= self.fail_after:
            raise ConnectionClosed()
        self.sent.append(data)
        self.times.append(self.clock.now if self.clock else None)

    def __aiter__(self):
        return self.messages()

    async def messages(self):
        for item in self.incoming:
            if isinstance(item, Pause):
                await asyncio.sleep(item.seconds)
                continue
            await asyncio.sleep(0)
            yield item
        if self.close_with is not None:
            raise self.close_with

    def controls(self):
        return [json.loads(item) for item in self.sent if isinstance(item, str)]

    def audio(self):
        return [item for item in self.sent if isinstance(item, bytes)]


def frame(sequence, channels=2, samples=4, magic=b"JA", version=1, payload=None):
    if payload is None:
        payload = struct.pack(f"<{samples * channels}h", *range(samples * channels))
    return spike.HEADER.pack(magic, version, channels, sequence, 1234) + payload


def tone(seconds):
    count = int(seconds * spike.RATE)
    return struct.pack(f"<{count}h", *((index * 37) % 2000 - 1000 for index in range(count)))


class SessionTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.out = Path(directory.name)
        stub = unittest.mock.patch.dict(sys.modules, websockets_stub())
        stub.start()
        self.addCleanup(stub.stop)
        self.tasks = []

    def create_task(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.append(task)
        return task

    def run_session(self, connection, reply=None):
        """Runs one session; returns its output and whether playback was left running."""
        namespace = types.SimpleNamespace(create_task=self.create_task, sleep=asyncio.sleep)
        args = argparse.Namespace(out=self.out, recording_count=itertools.count(0))

        async def run():
            try:
                await spike.session(connection, args, reply)
            finally:
                await asyncio.sleep(0)
                self.left_running = [task for task in self.tasks if not task.done()]

        with unittest.mock.patch.object(spike, "asyncio", namespace), \
                contextlib.redirect_stdout(io.StringIO()) as printed:
            try:
                asyncio.run(run())
            finally:
                self.printed = printed.getvalue()
        return self.printed

    def reports(self):
        return [json.loads(path.read_text()) for path in sorted(self.out.glob("mic-*.json"))]

    def test_short_and_malformed_frames_are_ignored(self):
        printed = self.run_session(FakeConnection([
            b"", b"JA\x01", frame(0)[:11],
            frame(0, magic=b"JB"), frame(0, version=2), frame(0, channels=0, payload=b""),
            frame(0, channels=5, samples=1), frame(0, payload=b"\x00" * 6),
            frame(0), frame(1, payload=b""), frame(2),
            json.dumps({"type": "stream_end", "frames": 3})]))
        self.assertEqual(printed.count("short frame ignored"), 3)
        # An empty payload carries no audio and is ignored too, so frame 1 is missing.
        self.assertEqual(printed.count("malformed frame ignored"), 6)
        [report] = self.reports()
        self.assertEqual((report["frames"], report["missing_frames"], report["seconds"]), (2, 1, 8 / 24000))
        self.assertEqual(report["device"], {"type": "stream_end", "frames": 3})

    def test_a_recording_stops_at_its_cap_and_ends_the_session(self):
        samples = spike.RATE  # one second per frame
        frames = [frame(index, channels=1, samples=samples) for index in range(spike.MAX_RECORDING_SECONDS + 5)]
        printed = self.run_session(FakeConnection(frames + [json.dumps({"type": "stream_end"})]))
        self.assertIn("ending the session", printed)
        [report] = self.reports()
        self.assertEqual(report["seconds"], spike.MAX_RECORDING_SECONDS)
        self.assertEqual(report["device"], {"type": "disconnected"})

    def test_a_run_records_at_most_its_budget(self):
        rounds = []
        for index in range(spike.MAX_RECORDINGS + 2):
            rounds += [frame(index), json.dumps({"type": "stream_end"})]
        printed = self.run_session(FakeConnection(rounds))
        self.assertEqual(len(self.reports()), spike.MAX_RECORDINGS)
        self.assertIn(f"{spike.MAX_RECORDINGS} recordings this run; ending the session", printed)

    def test_recordings_started_in_the_same_second_never_share_a_file(self):
        first, second = spike.Recording(self.out, 1), spike.Recording(self.out, 1)
        try:
            self.assertEqual(first.started, second.started)
            self.assertNotEqual(first.path, second.path)
        finally:
            first.wav.close()
            second.wav.close()

    def test_a_channel_change_mid_stream_keeps_the_first_layout(self):
        self.run_session(FakeConnection([frame(0, channels=2), frame(1, channels=1, samples=8),
                                         frame(2, channels=2), json.dumps({"type": "stream_end"})]))
        [report] = self.reports()
        self.assertEqual((report["frames"], report["missing_frames"]), (2, 1))
        with wave.open(report["wav"]) as wav:
            self.assertEqual((wav.getnchannels(), wav.getnframes()), (2, 8))

    def test_control_messages_that_are_not_json_objects_are_ignored(self):
        # Any LAN peer can connect; junk must neither end the session nor lose audio.
        reply = tone(0.1)
        connection = FakeConnection([
            "{", "not json", "[]", "null", "42", '"hello"', "",
            frame(0), json.dumps({"type": "hello"}), Pause(0.05), json.dumps({"type": "stream_end"})])
        printed = self.run_session(connection, reply)
        self.assertEqual(printed.count("malformed message ignored"), 7)
        self.assertEqual(len(self.reports()), 1)
        self.assertEqual(b"".join(connection.audio()), reply)

    def test_hello_starts_playback_only_once(self):
        reply = tone(0.1)
        hello = json.dumps({"type": "hello", "rate": 24000})
        connection = FakeConnection([hello, hello, Pause(0.05), hello, Pause(0.01)])
        printed = self.run_session(connection, reply)
        self.assertEqual(len(self.tasks), 1)
        self.assertEqual(connection.controls(), [
            {"type": "play_begin", "rate": 24000, "channels": 1, "samples": 2400}, {"type": "play_end"}])
        self.assertEqual([len(chunk) for chunk in connection.audio()], [2880, 1920])
        self.assertEqual(b"".join(connection.audio()), reply)
        self.assertIn("played 0.10 s", printed)
        self.assertEqual(self.left_running, [])

    def test_without_a_reply_hello_plays_nothing(self):
        connection = FakeConnection([json.dumps({"type": "hello"}), Pause(0.01)])
        self.run_session(connection, None)
        self.assertEqual((self.tasks, connection.sent), ([], []))

    def test_disconnecting_during_playback_cancels_it_and_saves_the_recording(self):
        connection = FakeConnection([json.dumps({"type": "hello"}), frame(0), frame(1), Pause(0.02)])
        self.run_session(connection, tone(3.0))
        [task] = self.tasks
        self.assertTrue(task.cancelled())
        self.assertEqual(self.left_running, [])
        self.assertNotIn({"type": "play_end"}, connection.controls())
        self.assertLess(len(connection.audio()), 50)
        [report] = self.reports()
        self.assertEqual((report["frames"], report["device"]), (2, {"type": "disconnected"}))

    def test_an_abrupt_close_still_saves_the_recording_and_stops_playback(self):
        # websockets raises from iteration when the board vanishes without a close frame.
        connection = FakeConnection([json.dumps({"type": "hello"}), frame(0), frame(1), frame(2)],
                                    close_with=ConnectionClosed("no close frame"))
        with self.assertRaises(ConnectionClosed):
            self.run_session(connection, tone(3.0))
        self.assertTrue(self.tasks[0].cancelled())
        [report] = self.reports()
        self.assertEqual((report["frames"], report["device"]), (3, {"type": "disconnected"}))
        self.assertIn('"disconnected"', self.printed)

    def test_stream_end_closes_once_and_a_stray_one_is_harmless(self):
        printed = self.run_session(FakeConnection([
            json.dumps({"type": "stream_end"}), frame(0), json.dumps({"type": "stream_end", "frames": 1}),
            json.dumps({"type": "stream_end"})]))
        [report] = self.reports()
        self.assertEqual(report["device"], {"type": "stream_end", "frames": 1})
        self.assertEqual(printed.count('"wav":'), 1)

    def test_playback_into_a_closed_connection_ends_quietly(self):
        connection = FakeConnection([], fail_after=3)
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            asyncio.run(spike.play(connection, tone(0.5)))
        self.assertIn("board disconnected during playback", printed.getvalue())
        self.assertNotIn("played", printed.getvalue())
        self.assertEqual(len(connection.sent), 3)


class AdmissionTests(unittest.TestCase):
    def test_only_the_named_board_and_one_session_at_a_time(self):
        self.assertIsNone(spike.refusal("192.168.68.50", None, False))
        self.assertIsNone(spike.refusal("192.168.68.50", "192.168.68.50", False))
        self.assertEqual(spike.refusal("192.168.68.51", "192.168.68.50", False), "not the board")
        self.assertEqual(spike.refusal(None, "192.168.68.50", False), "not the board")
        self.assertEqual(spike.refusal("192.168.68.50", None, True), "a session is already running")
        self.assertEqual(spike.refusal("192.168.68.51", "192.168.68.50", True), "not the board")

    def test_the_board_option_takes_only_an_ipv4_address(self):
        argv = ["spike_server.py", "--board", "localhost"]
        with unittest.mock.patch.object(sys, "argv", argv), \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            spike.main()
        self.assertEqual(raised.exception.code, 2)


class FakeServe:
    """Stands in for websockets.asyncio.server.serve: keeps the handler for the test to call."""

    def __init__(self):
        self.handler, self.calls, self.exited = None, [], False
        self.ready = asyncio.Event()

    def __call__(self, handler, host, port, **options):
        self.handler = handler
        self.calls.append((host, port, options))
        return self

    async def __aenter__(self):
        self.ready.set()
        return self

    async def __aexit__(self, *_):
        self.exited = True


class PeerConnection(FakeConnection):
    """A FakeConnection from a chosen address that records how the server closed it."""

    def __init__(self, address, incoming=(), **options):
        super().__init__(list(incoming), **options)
        self.remote_address = (address, 50000) if address else None
        self.closes = []

    async def close(self, code=1000, reason=""):
        self.closes.append((code, reason))


class HandlerTests(unittest.TestCase):
    """main_async's connection handler: admission, one session at a time, and --once."""

    BOARD = "192.0.2.7"

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.out = Path(directory.name)
        self.server = FakeServe()
        modules = websockets_stub()
        asyncio_package, server = types.ModuleType("websockets.asyncio"), types.ModuleType("websockets.asyncio.server")
        server.serve = self.server
        asyncio_package.server = server
        modules["websockets"].asyncio = asyncio_package
        modules.update({"websockets.asyncio": asyncio_package, "websockets.asyncio.server": server})
        stub = unittest.mock.patch.dict(sys.modules, modules)
        stub.start()
        self.addCleanup(stub.stop)
        self.sessions = []

    def args(self, board=BOARD, once=True, timeout=5.0):
        return argparse.Namespace(host="127.0.0.1", port=8765, board=board, once=once,
                                  timeout=timeout, out=self.out, recording_count=itertools.count(0))

    def run_scenario(self, args, scenario, session=None):
        """Runs main_async beside scenario(server, task); returns what was printed."""
        async def run():
            task = asyncio.create_task(spike.main_async(args, None))
            await self.server.ready.wait()
            try:
                await scenario(self.server, task)
            finally:
                if not task.done():
                    task.cancel()
                    with contextlib.suppress(asyncio.CancelledError):
                        await task

        patches = [contextlib.redirect_stdout(io.StringIO())]
        if session is not None:
            patches.append(unittest.mock.patch.object(spike, "session", session))
        with contextlib.ExitStack() as stack:
            printed = [stack.enter_context(patch) for patch in patches][0]
            asyncio.run(run())
        return printed.getvalue()

    def gated_session(self, gate):
        async def session(connection, args, reply):
            self.sessions.append(connection)
            await gate.wait()
        return session

    def test_a_stranger_is_closed_with_1008_and_never_ends_a_once_run(self):
        stranger = PeerConnection("192.0.2.99", [frame(0), json.dumps({"type": "stream_end"})])
        board = PeerConnection(self.BOARD, [frame(0), json.dumps({"type": "stream_end"})])

        async def scenario(server, task):
            await asyncio.wait_for(server.handler(stranger), 1)
            await asyncio.sleep(0.01)
            self.assertFalse(task.done(), "a refused connection ended the --once run")
            self.assertEqual(list(self.out.glob("mic-*")), [])  # No session, no recording.
            await asyncio.wait_for(server.handler(board), 1)
            await asyncio.wait_for(task, 1)

        printed = self.run_scenario(self.args(), scenario)
        self.assertEqual(stranger.closes, [(1008, "not the board")])
        self.assertEqual((stranger.sent, board.closes), ([], []))
        self.assertIn("refused 192.0.2.99: not the board", printed)
        self.assertEqual(printed.count("connected:"), 1)
        self.assertEqual(len(list(self.out.glob("mic-*.json"))), 1)
        self.assertTrue(self.server.exited)
        self.assertEqual(self.server.calls, [("127.0.0.1", 8765, {"max_size": 1 << 20})])
        self.assertIn("listening on ws://127.0.0.1:8765/agent", printed)

    def test_a_second_connection_is_refused_while_the_first_runs(self):
        gate = asyncio.Event()

        async def scenario(server, task):
            first = asyncio.create_task(server.handler(PeerConnection(self.BOARD)))
            await asyncio.sleep(0)
            self.assertEqual(len(self.sessions), 1)
            second = PeerConnection(self.BOARD)
            await asyncio.wait_for(server.handler(second), 1)
            self.assertEqual(second.closes, [(1008, "a session is already running")])
            self.assertEqual(len(self.sessions), 1)
            self.assertFalse(task.done(), "a refused connection ended the --once run")
            gate.set()
            await asyncio.wait_for(first, 1)
            await asyncio.wait_for(task, 1)

        printed = self.run_scenario(self.args(), scenario, self.gated_session(gate))
        self.assertIn("a session is already running", printed)

    def test_the_server_is_free_again_after_each_session(self):
        gate = asyncio.Event()
        gate.set()

        async def scenario(server, task):
            for _ in range(3):
                connection = PeerConnection("198.51.100.4")
                await asyncio.wait_for(server.handler(connection), 1)
                self.assertEqual(connection.closes, [])
            self.assertFalse(task.done())  # Without --once it serves until stopped.

        self.run_scenario(self.args(board=None, once=False), scenario, self.gated_session(gate))
        self.assertEqual(len(self.sessions), 3)

    def test_a_session_that_fails_still_frees_the_server_and_ends_a_once_run(self):
        async def failing(connection, args, reply):
            self.sessions.append(connection)
            raise ConnectionClosed("no close frame")

        async def scenario(server, task):
            with self.assertRaises(ConnectionClosed):
                await asyncio.wait_for(server.handler(PeerConnection(self.BOARD)), 1)
            await asyncio.wait_for(task, 1)
            with self.assertRaises(ConnectionClosed):  # Not refused as busy.
                await asyncio.wait_for(server.handler(PeerConnection(self.BOARD)), 1)

        self.run_scenario(self.args(), scenario, failing)
        self.assertEqual(len(self.sessions), 2)

    def test_an_unknown_peer_address_is_refused_only_when_a_board_is_named(self):
        gate = asyncio.Event()
        gate.set()
        named, anyone = PeerConnection(None), PeerConnection(None)

        async def refuse(server, task):
            await asyncio.wait_for(server.handler(named), 1)

        async def admit(server, task):
            await asyncio.wait_for(server.handler(anyone), 1)
            await asyncio.wait_for(task, 1)

        self.run_scenario(self.args(), refuse, self.gated_session(gate))
        self.assertEqual((named.closes, self.sessions), ([(1008, "not the board")], []))
        self.server = FakeServe()
        sys.modules["websockets.asyncio.server"].serve = self.server
        self.run_scenario(self.args(board=None), admit, self.gated_session(gate))
        self.assertEqual((anyone.closes, self.sessions), ([], [anyone]))

    def test_the_recording_budget_spans_the_sessions_of_one_run(self):
        # Refused peers and audio-free sessions spend nothing; each recorded session spends one.
        def recorded(index):
            return PeerConnection(self.BOARD, [frame(index), json.dumps({"type": "stream_end"})])

        boards = [recorded(index) for index in range(spike.MAX_RECORDINGS + 2)]
        strangers = [PeerConnection("192.0.2.99", [frame(0)]) for _ in range(spike.MAX_RECORDINGS + 1)]
        quiet = PeerConnection(self.BOARD, [json.dumps({"type": "hello"})])

        async def scenario(server, task):
            for stranger in strangers:
                await asyncio.wait_for(server.handler(stranger), 1)
            await asyncio.wait_for(server.handler(quiet), 1)
            for board in boards:
                await asyncio.wait_for(server.handler(board), 1)
                self.assertEqual(board.closes, [])
            self.assertFalse(task.done())

        printed = self.run_scenario(self.args(once=False), scenario)
        self.assertEqual(len(list(self.out.glob("mic-*.json"))), spike.MAX_RECORDINGS)
        self.assertEqual(len(list(self.out.glob("mic-*.wav"))), spike.MAX_RECORDINGS)
        self.assertEqual(printed.count(f"{spike.MAX_RECORDINGS} recordings this run; ending the session"), 2)
        self.assertEqual(printed.count("refused 192.0.2.99: not the board"), len(strangers))

    def test_a_connection_refused_as_busy_spends_no_budget(self):
        gate = asyncio.Event()
        real_session = spike.session

        async def first_waits(connection, args, reply):
            if not self.sessions:
                self.sessions.append(connection)
                await gate.wait()
            await real_session(connection, args, reply)

        async def scenario(server, task):
            first = asyncio.create_task(server.handler(
                PeerConnection(self.BOARD, [frame(0), json.dumps({"type": "stream_end"})])))
            await asyncio.sleep(0)
            for _ in range(spike.MAX_RECORDINGS + 1):
                busy = PeerConnection(self.BOARD, [frame(0)])
                await asyncio.wait_for(server.handler(busy), 1)
                self.assertEqual(busy.closes, [(1008, "a session is already running")])
            gate.set()
            await asyncio.wait_for(first, 1)
            for index in range(1, spike.MAX_RECORDINGS):
                await asyncio.wait_for(server.handler(
                    PeerConnection(self.BOARD, [frame(index), json.dumps({"type": "stream_end"})])), 1)

        printed = self.run_scenario(self.args(once=False), scenario, first_waits)
        self.assertEqual(len(list(self.out.glob("mic-*.json"))), spike.MAX_RECORDINGS)
        self.assertNotIn("recordings this run", printed)

    def test_once_gives_up_after_its_timeout(self):
        async def scenario(server, task):
            with self.assertRaises(TimeoutError):
                await task

        self.run_scenario(self.args(timeout=0.01), scenario)
        self.assertTrue(self.server.exited)


class PlaybackPacingTests(unittest.TestCase):
    def setUp(self):
        stub = unittest.mock.patch.dict(sys.modules, websockets_stub())
        stub.start()
        self.addCleanup(stub.stop)

    def play(self, pcm):
        clock = FakeClock()
        connection = FakeConnection([], clock=clock)
        with unittest.mock.patch.object(spike, "time", clock), \
                unittest.mock.patch.object(spike, "asyncio", types.SimpleNamespace(sleep=clock.async_sleep)), \
                contextlib.redirect_stdout(io.StringIO()):
            asyncio.run(spike.play(connection, pcm))
        return clock, connection

    def test_chunks_are_paced_at_real_time_after_the_lead(self):
        pcm = tone(1.0)
        clock, connection = self.play(pcm)
        start = 1000.0
        chunks = connection.sent[1:-1]
        self.assertEqual(len(chunks), 17)
        self.assertEqual(connection.times[0], start)
        for index, sent_at in enumerate(connection.times[1:-1]):
            with self.subTest(chunk=index):
                # The first 300 ms go at once; then each 60 ms chunk is due 300 ms early.
                self.assertAlmostEqual(sent_at, max(start, start + index * 0.06 - 0.3), places=9)
        self.assertAlmostEqual(clock.now - start, 16 * 0.06 - 0.3, places=9)
        self.assertEqual(len(clock.sleeps), 11)
        self.assertTrue(all(delay > 0 for delay in clock.sleeps))
        self.assertEqual(json.loads(connection.sent[-1]), {"type": "play_end"})

    def test_any_length_is_sent_whole_in_even_bounded_chunks(self):
        generator = random.Random(7)
        for samples in [0, 1, 1439, 1440, 1441] + [generator.randrange(0, 20000) for _ in range(15)]:
            with self.subTest(samples=samples):
                pcm = struct.pack(f"<{samples}h", *(generator.randrange(-32768, 32768) for _ in range(samples)))
                _, connection = self.play(pcm)
                begin, *chunks, end = connection.sent
                self.assertEqual(json.loads(begin)["samples"], samples)
                self.assertEqual(json.loads(end), {"type": "play_end"})
                self.assertEqual(b"".join(chunks), pcm)
                self.assertEqual(len(chunks), -(-samples // spike.CHUNK_SAMPLES))
                self.assertTrue(all(0 < len(chunk) <= 2880 and len(chunk) % 2 == 0 for chunk in chunks))


# A made-up sentinel, not a credential: tests check it never reaches any output.
TOKEN = "fake-chatter-token-" + "x" * 12


class FakeChatter:
    """Answers urlopen like Chatter's REST API from a script of job states."""

    def __init__(self, job=None, states=("completed",), audio=b"RIFF-audio"):
        self.job = {"id": "job-1"} if job is None else job
        self.states, self.audio, self.requests = list(states), audio, []

    def __call__(self, request, timeout):
        self.requests.append((request.get_method(), request.full_url, dict(request.header_items()),
                              request.data, timeout))
        path = request.full_url[len(spike.CHATTER_URL):]
        if path == "/v1/speech":
            body = self.job
        elif path.endswith("/audio"):
            return contextlib.nullcontext(io.BytesIO(self.audio))
        else:
            state = self.states.pop(0) if len(self.states) > 1 else self.states[0]
            body = {"state": state, "error": "voice missing"} if state == "failed" else {"state": state}
        return contextlib.nullcontext(io.BytesIO(json.dumps(body).encode()))


class ChatterTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.out = Path(directory.name)
        self.token_file = self.out / "api-token"
        self.token_file.write_text(f"  {TOKEN}\n")
        for patcher in (unittest.mock.patch.dict(os.environ, {"CHATTER_TOKEN_FILE": str(self.token_file)}),
                        unittest.mock.patch.object(spike, "time", FakeClock())):
            patcher.start()
            self.addCleanup(patcher.stop)

    def render(self, chatter, tone_name=None):
        with unittest.mock.patch.object(spike.urllib.request, "urlopen", chatter), \
                contextlib.redirect_stdout(io.StringIO()) as out, \
                contextlib.redirect_stderr(io.StringIO()) as err:
            try:
                return spike.render_with_chatter("Hi there", "Ryan", tone_name, self.out)
            except spike.SpikeError as exc:
                self.assertNotIn(TOKEN, f"{exc} {exc!r}")
                raise
            finally:
                self.assertNotIn(TOKEN, out.getvalue() + err.getvalue())

    def test_a_job_is_created_polled_and_its_audio_saved(self):
        chatter = FakeChatter(states=("queued", "running", "completed"))
        path, seconds = self.render(chatter)
        self.assertEqual(path.parent, self.out)
        self.assertEqual(path.read_bytes(), b"RIFF-audio")
        self.assertAlmostEqual(seconds, 0.2)
        methods = [(method, url[len(spike.CHATTER_URL):]) for method, url, *_ in chatter.requests]
        self.assertEqual(methods, [("POST", "/v1/speech")] + [("GET", "/v1/jobs/job-1")] * 3
                         + [("GET", "/v1/jobs/job-1/audio")])
        _, _, headers, data, timeout = chatter.requests[0]
        self.assertEqual(json.loads(data), {"voice": "Ryan", "text": "Hi there", "mode": "save"})
        self.assertEqual(headers["Content-type"], "application/json")
        for _, _, headers, data, timeout in chatter.requests:
            self.assertEqual(headers["Authorization"], "Bearer " + TOKEN)  # stripped of whitespace
            self.assertEqual(timeout, 30)
        self.assertTrue(all(data is None for _, _, _, data, _ in chatter.requests[1:]))

    def test_tone_and_the_alternative_job_key(self):
        chatter = FakeChatter(job={"jobID": "j2"})
        self.render(chatter, tone_name="warm")
        self.assertEqual(json.loads(chatter.requests[0][3])["tone"], "warm")
        self.assertTrue(chatter.requests[1][1].endswith("/v1/jobs/j2"))

    def test_a_missing_job_id_names_only_the_keys(self):
        with self.assertRaisesRegex(spike.SpikeError, r"job id: \['detail', 'error'\]$"):
            self.render(FakeChatter(job={"error": "unauthorized", "detail": TOKEN}))

    def test_failed_and_cancelled_jobs_raise(self):
        with self.assertRaisesRegex(spike.SpikeError, "Chatter job failed: voice missing"):
            self.render(FakeChatter(states=("running", "failed")))
        with self.assertRaisesRegex(spike.SpikeError, "Chatter job cancelled: $"):
            self.render(FakeChatter(states=("cancelled",)))

    def test_a_job_that_never_finishes_times_out_without_real_waiting(self):
        chatter = FakeChatter(states=("running",))
        with self.assertRaisesRegex(spike.SpikeError, "longer than 120 s"):
            self.render(chatter)
        polls = sum(url.endswith("/v1/jobs/job-1") for _, url, *_ in chatter.requests)
        self.assertGreaterEqual(polls, 1200)
        self.assertLessEqual(polls, 1202)
        self.assertTrue(all(delay == 0.1 for delay in spike.time.sleeps))

    def test_a_missing_or_empty_token_stops_before_any_request(self):
        chatter = FakeChatter()
        for content, pattern in ((None, "unreadable at .*api-token"), (" \n\t", "is empty")):
            with self.subTest(content=content):
                if content is None:
                    self.token_file.unlink()
                else:
                    self.token_file.write_text(content)
                with self.assertRaisesRegex(spike.SpikeError, pattern):
                    self.render(chatter)
        self.assertEqual(chatter.requests, [])


class MainTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.out = Path(directory.name)
        self.token_file = self.out / "api-token"
        self.token_file.write_text(TOKEN)
        self.replies = []
        for patcher in (unittest.mock.patch.dict(os.environ, {"CHATTER_TOKEN_FILE": str(self.token_file)}),
                        unittest.mock.patch.object(spike, "main_async", self.serve)):
            patcher.start()
            self.addCleanup(patcher.stop)

    async def serve(self, args, reply):
        self.replies.append(reply)
        if args.once:
            raise TimeoutError

    def main(self, *arguments, urlopen=None):
        argv = ["spike_server", "--out", str(self.out), "--board", "192.168.68.50", *arguments]
        with unittest.mock.patch.object(sys, "argv", argv), \
                unittest.mock.patch.object(spike.urllib.request, "urlopen", urlopen or FakeChatter()), \
                contextlib.redirect_stdout(io.StringIO()) as out, \
                contextlib.redirect_stderr(io.StringIO()) as err:
            code = spike.main()
        self.assertNotIn(TOKEN, out.getvalue() + err.getvalue())
        return code, out.getvalue(), err.getvalue()

    def test_say_renders_and_serves_the_reply(self):
        source = self.out / "source.wav"
        write_wav(source, 24000, 3, 1, [256, -512, 768])
        code, out, _ = self.main("--say", "hello", urlopen=FakeChatter(audio=source.read_bytes()))
        self.assertEqual(code, 0)
        self.assertIn("chatter_wav", json.loads(out.splitlines()[0]))
        self.assertEqual(self.replies, [struct.pack("<3h", 1, -2, 3)])

    def test_http_and_connection_failures_exit_cleanly_without_the_token(self):
        def unauthorized(request, timeout):
            raise urllib.error.HTTPError(request.full_url, 401, "Unauthorized", {}, None)

        def refused(request, timeout):
            raise urllib.error.URLError(ConnectionRefusedError(61, "Connection refused"))

        for urlopen, message in ((unauthorized, "HTTP Error 401"), (refused, "Connection refused")):
            with self.subTest(message=message):
                code, _, err = self.main("--say", "hello", urlopen=urlopen)
                self.assertEqual(code, 1)
                self.assertIn(message, err)
        self.assertEqual(self.replies, [])

    def test_play_validates_the_wav_and_serves_without_a_reply(self):
        source = self.out / "wrong.wav"
        write_wav(source, 16000, 2, 1, [1, 2])
        code, _, err = self.main("--play", str(source))
        self.assertEqual(code, 1)
        self.assertIn("need 24000 Hz", err)
        self.assertEqual(self.main(), (0, "", ""))
        self.assertEqual(self.replies, [None])

    def test_each_run_starts_a_fresh_recording_budget(self):
        counters = []

        async def serve(args, reply):
            counters.append(args.recording_count)

        with unittest.mock.patch.object(spike, "main_async", serve):
            self.assertEqual(self.main(), (0, "", ""))
            self.assertEqual(self.main(), (0, "", ""))
        self.assertIsNot(counters[0], counters[1])
        self.assertEqual([next(counter) for counter in counters], [0, 0])

    def test_board_is_required_before_anything_starts(self):
        out = self.out / "not-yet"

        def no_request(request, timeout):
            raise AssertionError("Chatter was called")

        for extra in ([], ["--say", "hello"], ["--board"]):
            argv = ["spike_server", "--out", str(out), *extra]
            with self.subTest(argv=argv), unittest.mock.patch.object(sys, "argv", argv), \
                    unittest.mock.patch.object(spike.urllib.request, "urlopen", no_request), \
                    contextlib.redirect_stderr(io.StringIO()) as err, \
                    self.assertRaises(SystemExit) as raised:
                spike.main()
            self.assertEqual(raised.exception.code, 2)
            self.assertIn("--board", err.getvalue())
        self.assertFalse(out.exists())  # Nothing created, rendered, or served.
        self.assertEqual(self.replies, [])

    def test_once_reports_a_timeout(self):
        code, _, err = self.main("--once", "--timeout", "5")
        self.assertEqual(code, 1)
        self.assertIn("no board finished a session within 5 s", err)


if __name__ == "__main__":
    unittest.main()

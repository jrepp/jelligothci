# /// script
# requires-python = ">=3.12"
# dependencies = ["websockets==17.1"]
# ///
"""Phase 0 audio spike server for the voice agent (RFC-003); LAN only, no TLS.

The board connects to ws://HOST:PORT/agent after `link set` and `mic stream` on
its console. This records the microphone stream to a WAV with timing statistics,
and can answer with a WAV or a Chatter rendering played on the board's speaker.

Frames from the board are binary: a 12-byte header ("JA", version 1, channel
count, sequence u32 LE, capture milliseconds u32 LE) and interleaved int16 LE
samples. Control messages are JSON text. Phase 1 replaces this with jelli-agent/1
over mutually authenticated TLS.
"""
import argparse
import asyncio
import ipaddress
import itertools
import json
import math
import os
import statistics
import struct
import sys
import time
import urllib.request
import wave
from pathlib import Path

RATE = 24000
HEADER = struct.Struct("<2sBBII")
CHUNK_SAMPLES = RATE * 60 // 1000  # 60 ms of playback per message
LEAD_SECONDS = 0.3  # The board buffers this much before it starts playing.
# The board streams at most 30 s; anything longer is not the board, so stop there.
MAX_RECORDING_SECONDS = 35
# Recordings per server run: a few spike streams, never an unbounded disk fill.
MAX_RECORDINGS = 4
RECORDING_NUMBERS = itertools.count(1)
CHATTER_URL = os.environ.get("CHATTER_URL", "http://127.0.0.1:18423")


class SpikeError(Exception):
    pass


def to_int16_mono(path):
    """Reads a 24 kHz PCM WAV (16 or 24 bit, any channel count) as mono int16 bytes."""
    with wave.open(str(path), "rb") as source:
        rate, width, channels = source.getframerate(), source.getsampwidth(), source.getnchannels()
        frames = source.readframes(source.getnframes())
    if rate != RATE or width not in (2, 3) or channels < 1:
        raise SpikeError(f"{path}: need {RATE} Hz 16/24-bit PCM, got {rate} Hz {8 * width}-bit")
    out = bytearray()
    step = width * channels
    for offset in range(0, len(frames) - step + 1, step):
        total = 0
        for channel in range(channels):
            start = offset + channel * width
            sample = int.from_bytes(frames[start:start + width], "little", signed=True)
            total += sample >> 8 if width == 3 else sample
        out += struct.pack("<h", max(-32768, min(32767, round(total / channels))))
    return bytes(out)


def chatter_token():
    default = Path.home() / "Library/Application Support/Chatter/api-token"
    path = Path(os.environ.get("CHATTER_TOKEN_FILE", default))
    try:
        token = path.read_text().strip()
    except OSError as exc:
        raise SpikeError(f"Chatter token unreadable at {path}: {exc.strerror}") from None
    if not token:
        raise SpikeError(f"Chatter token file {path} is empty")
    return token


def chatter_call(path, token, body=None):
    request = urllib.request.Request(CHATTER_URL + path, method="POST" if body else "GET",
                                     data=json.dumps(body).encode() if body else None)
    request.add_header("Authorization", "Bearer " + token)  # Never logged.
    if body:
        request.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def render_with_chatter(text, voice, tone, out_dir):
    """Renders text through Chatter's REST API; returns (wav path, seconds taken)."""
    token = chatter_token()
    started = time.monotonic()
    body = {"voice": voice, "text": text, "mode": "save"}
    if tone:
        body["tone"] = tone
    job = json.loads(chatter_call("/v1/speech", token, body))
    job_id = job.get("id") or job.get("jobID")
    if not job_id:
        raise SpikeError(f"Chatter did not return a job id: {sorted(job)}")
    while True:
        state = json.loads(chatter_call(f"/v1/jobs/{job_id}", token))
        if state.get("state") == "completed":
            break
        if state.get("state") in ("failed", "cancelled"):
            raise SpikeError(f"Chatter job {state.get('state')}: {state.get('error', '')}")
        if time.monotonic() - started > 120:
            raise SpikeError("Chatter took longer than 120 s")
        time.sleep(0.1)
    audio = chatter_call(f"/v1/jobs/{job_id}/audio", token)
    elapsed = time.monotonic() - started
    path = out_dir / f"chatter-{int(time.time())}.wav"
    path.write_bytes(audio)
    return path, elapsed


class Recording:
    def __init__(self, out_dir, channels):
        self.started = time.strftime("%Y%m%d-%H%M%S")
        # Unique even for sessions that start in the same second, here or in another run.
        self.path = out_dir / f"mic-{self.started}-{os.getpid()}-{next(RECORDING_NUMBERS)}.wav"
        self.wav = wave.open(str(self.path), "wb")
        self.wav.setnchannels(channels)
        self.wav.setsampwidth(2)
        self.wav.setframerate(RATE)
        self.channels = channels
        self.arrivals, self.sequences, self.squares = [], [], [0.0] * channels
        self.samples = 0

    def full(self):
        return self.samples >= MAX_RECORDING_SECONDS * RATE

    def add(self, sequence, payload):
        self.arrivals.append(time.monotonic())
        self.sequences.append(sequence)
        values = struct.unpack(f"<{len(payload) // 2}h", payload)
        for index, value in enumerate(values):
            self.squares[index % self.channels] += value * value
        self.samples += len(values) // self.channels
        self.wav.writeframes(payload)

    def close(self, device_stats):
        self.wav.close()
        unique = set(self.sequences)
        missing = (max(unique) - min(unique) + 1 - len(unique)) if unique else 0
        intervals = [1000 * (b - a) for a, b in zip(self.arrivals, self.arrivals[1:])]
        report = {
            "wav": str(self.path), "frames": len(self.sequences), "seconds": self.samples / RATE,
            "missing_frames": missing, "duplicates": len(self.sequences) - len(unique),
            "out_of_order": sum(b < a for a, b in zip(self.sequences, self.sequences[1:])),
            "rms_dbfs": [round(20 * math.log10(max(math.sqrt(s / max(self.samples, 1)), 1) / 32768), 1)
                         for s in self.squares],
            "arrival_ms": {
                "mean": round(statistics.fmean(intervals), 2) if intervals else None,
                "p95": round(sorted(intervals)[int(0.95 * (len(intervals) - 1))], 2) if intervals else None,
                "max": round(max(intervals), 2) if intervals else None,
            },
            "device": device_stats,
        }
        self.path.with_suffix(".json").write_text(json.dumps(report, indent=2))
        return report


async def play(connection, pcm):
    """Streams mono int16 PCM paced at real time after a short lead."""
    from websockets.exceptions import ConnectionClosed
    try:
        await connection.send(json.dumps({"type": "play_begin", "rate": RATE, "channels": 1,
                                          "samples": len(pcm) // 2}))
        started = time.monotonic()
        step = CHUNK_SAMPLES * 2
        for index, offset in enumerate(range(0, len(pcm), step)):
            due = started + index * CHUNK_SAMPLES / RATE - LEAD_SECONDS
            delay = due - time.monotonic()
            if delay > 0:
                await asyncio.sleep(delay)
            await connection.send(pcm[offset:offset + step])
        await connection.send(json.dumps({"type": "play_end"}))
        print(f"played {len(pcm) / 2 / RATE:.2f} s", flush=True)
    except ConnectionClosed:
        print("board disconnected during playback", flush=True)


async def session(connection, args, reply):
    print(f"connected: {connection.remote_address}", flush=True)
    recording, playing = None, None
    # websockets ends iteration on a clean close but raises on an abrupt one (the
    # board losing Wi-Fi); either way, stop playback and keep what was recorded.
    try:
        async for message in connection:
            if isinstance(message, bytes):
                if len(message) < HEADER.size:
                    print("short frame ignored", flush=True)
                    continue
                magic, version, channels, sequence, _ = HEADER.unpack_from(message)
                payload = message[HEADER.size:]
                if (magic != b"JA" or version != 1 or not 1 <= channels <= 4 or not payload
                        or len(payload) % (2 * channels)):
                    print("malformed frame ignored", flush=True)
                    continue
                if recording is None:
                    if next(args.recording_count) >= MAX_RECORDINGS:
                        print(f"{MAX_RECORDINGS} recordings this run; ending the session", flush=True)
                        break
                    recording = Recording(args.out, channels)
                if channels == recording.channels:
                    recording.add(sequence, payload)
                if recording.full():
                    print(f"recording reached {MAX_RECORDING_SECONDS} s; ending the session", flush=True)
                    break
                continue
            try:
                event = json.loads(message)
            except ValueError:
                event = None
            if not isinstance(event, dict):
                print("malformed message ignored", flush=True)
                continue
            print(f"device: {json.dumps(event)}", flush=True)
            if event.get("type") == "hello" and reply is not None and playing is None:
                # Play concurrently: the board keeps streaming while it speaks, and its
                # reference channel should then carry the reply.
                playing = asyncio.create_task(play(connection, reply))
            elif event.get("type") == "stream_end" and recording is not None:
                print(json.dumps(recording.close(event), indent=2), flush=True)
                recording = None
    finally:
        if playing is not None and not playing.done():
            playing.cancel()
        if recording is not None:
            print(json.dumps(recording.close({"type": "disconnected"}), indent=2), flush=True)


def refusal(peer, board, busy):
    """Why a connection is refused, or None to admit it: only the board when --board
    names it, and one session at a time, so other LAN hosts cannot record, take the
    reply, or end a --once run."""
    if board is not None and peer != board:
        return "not the board"
    if busy:
        return "a session is already running"
    return None


async def main_async(args, reply):
    from websockets.asyncio.server import serve
    done = asyncio.get_running_loop().create_future()
    busy = False

    async def handler(connection):
        nonlocal busy
        peer = connection.remote_address[0] if connection.remote_address else None
        reason = refusal(peer, args.board, busy)
        if reason:
            print(f"refused {peer}: {reason}", flush=True)
            await connection.close(code=1008, reason=reason)
            return
        busy = True
        try:
            await session(connection, args, reply)
        finally:
            busy = False
            if args.once and not done.done():
                done.set_result(None)

    async with serve(handler, args.host, args.port, max_size=1 << 20):
        print(f"listening on ws://{args.host}:{args.port}/agent", flush=True)
        if args.once:
            await asyncio.wait_for(done, args.timeout)
        else:
            await asyncio.Future()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="0.0.0.0", help="Address to listen on; the board needs the LAN")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--board", type=lambda text: str(ipaddress.IPv4Address(text)), required=True,
                        help="Accept only this IPv4 address: the board's, from wifi status")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[2] / "build/phase0/audio")
    parser.add_argument("--once", action="store_true", help="Exit after the first board disconnects")
    parser.add_argument("--timeout", type=float, default=300.0, help="With --once, give up after this many seconds")
    reply_group = parser.add_mutually_exclusive_group()
    reply_group.add_argument("--play", type=Path, help="24 kHz WAV to play on the board when it connects")
    reply_group.add_argument("--say", help="Text for Chatter to render and play on the board")
    parser.add_argument("--voice", default="Ryan", help="Chatter voice for --say")
    parser.add_argument("--tone", help="Chatter tone for --say (built-in and designed voices)")
    args = parser.parse_args()
    args.recording_count = itertools.count(0)  # Shared by every session of this run.
    args.out.mkdir(parents=True, exist_ok=True)
    try:
        reply = None
        if args.play:
            reply = to_int16_mono(args.play)
        elif args.say:
            path, seconds = render_with_chatter(args.say, args.voice, args.tone, args.out)
            print(json.dumps({"chatter_wav": str(path), "chatter_seconds": round(seconds, 2)}), flush=True)
            reply = to_int16_mono(path)
        asyncio.run(main_async(args, reply))
        return 0
    except TimeoutError:
        print(f"spike-server: no board finished a session within {args.timeout:g} s", file=sys.stderr)
        return 1
    except (SpikeError, OSError, ValueError) as exc:
        print(f"spike-server: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

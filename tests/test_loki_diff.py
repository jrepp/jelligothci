"""The board's loki/1 codec through tests/loki_diff.c, against references that share
no code with it: the golden vectors, Python's json and hashlib. The bridge's Swift
tests drive the same program to compare the two codecs on generated inputs."""
from pathlib import Path
import hashlib
import json
import random
import re
import subprocess
import sys
import unittest

VECTORS = Path(__file__).resolve().parent / "vectors" / "loki-1.tsv"
TOOL = None

# The board's encoder: each type's fields in order, and which are strings.
SENT = {
    "hello": ["proto", "app", "fw", "board", "rate", "channels", "frame_ms"],
    "ptt_start": ["stream"],
    "ptt_end": ["stream", "frames", "dropped"],
    "cancel": [],
    "answer": ["id", "nonce", "digest", "choice"],
    "played": ["reply", "complete"],
    "status": ["battery", "charging", "rssi"],
    "set_model": ["model"],
}


def vectors():
    for line in VECTORS.read_text(encoding="utf-8").split("\n"):
        if line and not line.startswith("#"):
            fields = line.split("\t")
            yield fields + [None] * (5 - len(fields))


def raw(text):
    for prefix in ("json-hex:", "hex:"):
        if text.startswith(prefix):
            return bytes.fromhex(text[len(prefix):])
    return text.encode("utf-8")


def run(commands):
    result = subprocess.run([TOOL], input="\n".join(commands) + "\n", capture_output=True,
                            text=True, check=False)
    if result.returncode != 0:
        raise AssertionError("loki_diff failed: " + result.stderr)
    lines = result.stdout.splitlines()
    assert len(lines) == len(commands), (len(lines), len(commands))
    return lines


def card_digest(canonical):
    """The answer digest: SHA-256 of the card's part of the canonical form."""
    card = canonical[canonical.index("kind="):canonical.index(" timeout_ms=")]
    return hashlib.sha256(card.encode("ascii")).hexdigest()


def board_can_send(text):
    return all(0x20 <= ord(c) <= 0x7E and c not in '"\\' for c in text)


class LokiDiff(unittest.TestCase):
    def test_board_decodes_the_vectors(self):
        commands, expected, names = [], [], []
        for name, direction, verdict, data, canonical in vectors():
            if direction != "b2d" or data.startswith("hex:"):
                continue
            commands.append("d x" + raw(data).hex())
            names.append(name)
            if verdict == "ok":
                line = "ok\t" + canonical
                expected.append(line + ("\t" + card_digest(canonical) if canonical.startswith("ask ") else ""))
            elif verdict == "ignore":
                expected.append("ignore\t%d" % json.loads(raw(data))["seq"])
            else:
                expected.append("reject")
        self.assertGreater(len(commands), 300)
        for name, want, got in zip(names, expected, run(commands)):
            self.assertEqual(got, want, name)

    def test_audio_vectors(self):
        cases = [(name, direction, verdict, canonical, raw(data))
                 for name, direction, verdict, data, canonical in vectors() if data.startswith("hex:")]
        commands = ["a %d 1 x%s" % (2 if c[1] == "b2d" else 1, c[4].hex()) for c in cases]
        for (name, _, verdict, canonical, _), got in zip(cases, run(commands)):
            self.assertEqual(got, "ok\t" + canonical if verdict == "ok" else "reject", name)

    def test_sha256_matches_hashlib(self):
        generator = random.Random(0x10C1)
        inputs = [bytes(generator.randrange(256) for _ in range(n)) for n in range(0, 300)]
        inputs += [bytes(generator.randrange(256) for _ in range(generator.randrange(5000)))
                   for _ in range(40)]
        lines = run(["h x" + data.hex() for data in inputs])
        for data, got in zip(inputs, lines):
            self.assertEqual(got, "ok\t" + hashlib.sha256(data).hexdigest(), len(data))

    def test_board_encoder_writes_the_d2b_vectors(self):
        """Every d2b vector the board could send, it writes byte for byte; the rest it
        refuses rather than sending something else."""
        commands, expected, names = [], [], []
        for name, direction, verdict, data, _ in vectors():
            if direction != "d2b" or verdict != "ok" or data.startswith("hex:"):
                continue
            message = json.loads(raw(data), object_pairs_hook=list)
            keys = [key for key, _ in message]
            fields = dict(message)
            kind = fields["t"]
            compact = json.dumps(dict(message), separators=(",", ":"), ensure_ascii=False)
            if kind not in SENT or keys != ["t", "seq"] + SENT[kind] or compact.encode() != raw(data):
                continue
            args = []
            for key in SENT[kind]:
                value = fields[key]
                if key in ("proto", "app", "rate"):
                    continue
                if isinstance(value, bool):
                    args.append("1" if value else "0")
                elif isinstance(value, str):
                    args.append("x" + value.encode("utf-8").hex())
                else:
                    args.append(str(value))
            commands.append("e %s %d %s" % (kind, fields["seq"], " ".join(args)))
            strings = [v for k, v in fields.items() if isinstance(v, str) and k != "t"]
            expected.append("ok\t" + raw(data).hex() if all(map(board_can_send, strings)) else "reject")
            names.append(name)
        self.assertGreater(len(commands), 20)
        self.assertGreater(sum(e.startswith("ok") for e in expected), 15)
        for name, want, got in zip(names, expected, run(commands)):
            self.assertEqual(got, want, name)

    def test_board_sends_only_model_names(self):
        """set_model carries a name of 1 to 40 characters of A-Z a-z 0-9 . _ : / + -,
        written as Python's json writes it, and the board refuses any other name."""
        generator = random.Random(0x5E7)
        alphabet = "abcXYZ019._:/+-" + ' "\\,\t\x7f\u00e9\u2603'
        names = ["", "a", "a" * 40, "a" * 41, "Claude", "gpt-oss:20b", "hf.co/Org_1/Model-2:Q4+K"]
        names += ["".join(generator.choice(alphabet) for _ in range(generator.randrange(46)))
                  for _ in range(600)]
        lines = run(["e set_model 7 x" + name.encode("utf-8").hex() for name in names])
        valid = 0
        for name, got in zip(names, lines):
            if re.fullmatch(r"[A-Za-z0-9._:/+\-]{1,40}", name):
                sent = json.dumps({"t": "set_model", "seq": 7, "model": name}, separators=(",", ":"))
                self.assertEqual(got, "ok\t" + sent.encode("ascii").hex(), name)
                valid += 1
            else:
                self.assertEqual(got, "reject", name)
        self.assertGreater(valid, 30)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: test_loki_diff.py LOKI_DIFF")
    TOOL = sys.argv.pop(1)
    unittest.main()

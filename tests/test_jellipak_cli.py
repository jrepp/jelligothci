"""Independent wire fixtures for the native Jellipak verifier (stdlib only)."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


VERIFIER = None


def pack(blocks):
    """Build the provisional block envelope independently of the C reader."""
    directory = bytearray()
    payload = bytearray()
    offset = 32 + 32 * len(blocks)
    for index, (codec, stored, decoded) in enumerate(blocks, 1):
        directory.extend(struct.pack(
            "<8I", index, 0x4B4F4C42, codec, offset, len(stored),
            len(decoded), zlib.crc32(stored), zlib.crc32(decoded),
        ))
        payload.extend(stored)
        offset += len(stored)
    header = struct.pack(
        "<4sHH5I", b"JPAK", 1, 32, offset, len(blocks), 32, 32,
        zlib.crc32(directory),
    )
    return header + struct.pack("<I", zlib.crc32(header)) + directory + payload


def expected_stream(blocks):
    """Build JRAW v1 independently: id, decoded length, decoded bytes per block."""
    result = bytearray(b"JRAW" + struct.pack("<II", 1, len(blocks)))
    for block_id, _, decoded in blocks:
        result.extend(struct.pack("<II", block_id, len(decoded)))
        result.extend(decoded)
    return bytes(result)


class JellipakCliTest(unittest.TestCase):
    def run_pack(self, data):
        with tempfile.TemporaryDirectory(prefix="jellipak-") as directory:
            path = Path(directory) / "fixture.jpak"
            path.write_bytes(data)
            return subprocess.run(
                [str(VERIFIER), str(path)], capture_output=True, text=True,
                timeout=10, check=False,
            )

    def run_compare(self, packed, expected):
        with tempfile.TemporaryDirectory(prefix="jellipak-compare-") as directory:
            pack_path = Path(directory) / "fixture.jpak"
            expected_path = Path(directory) / "fixture.jraw"
            pack_path.write_bytes(packed)
            expected_path.write_bytes(expected)
            before = pack_path.read_bytes(), expected_path.read_bytes()
            result = subprocess.run(
                [str(VERIFIER), str(pack_path), "--compare", str(expected_path)],
                capture_output=True, text=True, timeout=10, check=False,
            )
            self.assertEqual((pack_path.read_bytes(), expected_path.read_bytes()), before)
            return result

    def test_empty_and_mixed_blocks(self):
        # Hand-authored LZ4: one A literal, an overlapping 12-byte match,
        # then five terminal literals. Decodes to thirteen As + BCDEF.
        compressed = bytes([0x18, 65, 1, 0, 0x50]) + b"BCDEF"
        for blocks in ([], [(0, b"raw", b"raw"),
                           (1, compressed, b"A" * 13 + b"BCDEF")]):
            with self.subTest(blocks=len(blocks)):
                result = self.run_pack(pack(blocks))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout.strip())

    def test_every_truncation_and_payload_corruption(self):
        valid = pack([(0, b"raw bytes", b"raw bytes")])
        for length in range(len(valid)):
            with self.subTest(length=length):
                self.assertNotEqual(self.run_pack(valid[:length]).returncode, 0)
        corrupt = bytearray(valid)
        corrupt[-1] ^= 1
        self.assertNotEqual(self.run_pack(corrupt).returncode, 0)
        self.assertNotEqual(self.run_pack(valid + b"\0").returncode, 0)

    def test_checksummed_invalid_codec_and_stream(self):
        for codec, encoded, decoded in (
            (2, b"x", b"x"),
            (1, bytes([0x10, 65, 0, 0, 0x50]) + b"BCDEF", b"A" * 5 + b"BCDEF"),
            (1, b"\xf0\xff", b"x"),
            (1, b"\x10x", b"xx"),
        ):
            with self.subTest(encoded=encoded):
                self.assertNotEqual(self.run_pack(pack([(codec, encoded, decoded)])).returncode, 0)

    def test_usage_and_missing_file(self):
        for args in ([], ["--unknown"], ["one", "two"]):
            result = subprocess.run(
                [str(VERIFIER), *args], capture_output=True, text=True,
                timeout=10, check=False,
            )
            self.assertNotEqual(result.returncode, 0)

    def test_compare_raw_lz4_and_empty_pack(self):
        decoded_lz4 = b"A" * 13 + b"BCDEF"
        compressed = bytes([0x18, 65, 1, 0, 0x50]) + b"BCDEF"
        blocks = [(1, b"metadata-like raw payload", b"metadata-like raw payload"),
                  (2, compressed, decoded_lz4)]
        result = self.run_compare(pack([(codec, stored, decoded)
                                        for codec, (_, stored, decoded) in enumerate(blocks)]),
                                  expected_stream(blocks))
        self.assertEqual(result.returncode, 0, result.stderr)

        empty = self.run_compare(pack([]), expected_stream([]))
        self.assertEqual(empty.returncode, 0, empty.stderr)

    def test_compare_rejects_expected_content_and_directory_mismatches(self):
        compressed = bytes([0x18, 65, 1, 0, 0x50]) + b"BCDEF"
        blocks = [(1, b"raw metadata", b"raw metadata"),
                  (2, compressed, b"A" * 13 + b"BCDEF")]
        valid_pack = pack([(codec, stored, decoded)
                           for codec, (_, stored, decoded) in enumerate(blocks)])
        valid_expected = expected_stream(blocks)
        self.assertEqual(self.run_compare(valid_pack, valid_expected).returncode, 0)

        changed_raw = bytearray(valid_expected)
        changed_raw[20] ^= 1
        changed_lz4 = bytearray(valid_expected)
        # Header (12), first record (8) + first decoded payload, then second record.
        changed_lz4[12 + 8 + len(blocks[0][2]) + 8] ^= 1
        wrong_count = bytearray(valid_expected)
        struct.pack_into("<I", wrong_count, 8, 1)
        wrong_id = bytearray(valid_expected)
        struct.pack_into("<I", wrong_id, 12, 4)
        wrong_order = expected_stream(list(reversed(blocks)))
        wrong_length = bytearray(valid_expected)
        struct.pack_into("<I", wrong_length, 16, len(blocks[0][2]) + 1)

        for label, candidate in (
            ("raw content", changed_raw), ("decoded LZ4 content", changed_lz4),
            ("block count", wrong_count), ("block id", wrong_id),
            ("block order", wrong_order), ("decoded length", wrong_length),
        ):
            with self.subTest(mismatch=label):
                self.assertNotEqual(self.run_compare(valid_pack, bytes(candidate)).returncode, 0)

    def test_compare_requires_exact_expected_stream_and_valid_cli(self):
        blocks = [(1, b"raw", b"raw"), (2, b"more", b"more")]
        valid_pack = pack([(0, stored, decoded) for _, stored, decoded in blocks])
        valid_expected = expected_stream(blocks)
        for length in range(len(valid_expected)):
            with self.subTest(truncated_at=length):
                self.assertNotEqual(self.run_compare(valid_pack, valid_expected[:length]).returncode, 0)
        self.assertNotEqual(self.run_compare(valid_pack, valid_expected + b"\0").returncode, 0)

        with tempfile.TemporaryDirectory(prefix="jellipak-cli-") as directory:
            pack_path = Path(directory) / "fixture.jpak"
            missing = Path(directory) / "missing.jraw"
            pack_path.write_bytes(valid_pack)
            for args in ((str(pack_path), "--compare", str(missing)),
                         (str(pack_path), "--unknown", str(missing))):
                with self.subTest(args=args):
                    result = subprocess.run(
                        [str(VERIFIER), *args], capture_output=True, text=True,
                        timeout=10, check=False,
                    )
                    self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    VERIFIER = Path(sys.argv.pop(1)).resolve()
    unittest.main()

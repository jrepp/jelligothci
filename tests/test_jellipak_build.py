# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0", "lz4==4.4.5"]
# ///
"""Artwork compiler integration checks, with an independent wire/pixel oracle.

Run scripts/uv run --python 3.12 tests/test_jellipak_build.py VERIFIER.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zlib

import lz4.block
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/assets"))
VERIFIER = None
from test_jellipak_cli import pack as wire_pack


def decode_container(data):
    """Read the envelope without using compiler parsing or fidelity helpers."""
    magic, version, header_size, size, count, directory, entry_size, crc, header_crc = struct.unpack_from("<4sHH6I", data)
    assert (magic, version, header_size, size, directory, entry_size) == (b"JPAK", 1, 32, len(data), 32, 32)
    assert zlib.crc32(data[:28]) == header_crc
    assert zlib.crc32(data[directory:directory + count * entry_size]) == crc
    blocks = {}
    for position in range(count):
        ident, kind, codec, offset, stored_size, decoded_size, stored_crc, decoded_crc = struct.unpack_from("<8I", data, directory + position * entry_size)
        stored = data[offset:offset + stored_size]
        assert len(stored) == stored_size and zlib.crc32(stored) == stored_crc
        assert codec in (0, 1)
        decoded = stored if codec == 0 else lz4.block.decompress(stored, uncompressed_size=decoded_size)
        assert len(decoded) == decoded_size and zlib.crc32(decoded) == decoded_crc
        assert ident not in blocks
        blocks[ident] = decoded
    metadata = blocks[1]
    magic, version, count, palettes, assets_at, palettes_at, strings_at, strings_size = struct.unpack_from("<4s7I", metadata)
    assert magic == b"JART" and version == 1 and assets_at == 32
    assert strings_at + strings_size == len(metadata)
    records = []
    for index in range(count):
        record = struct.unpack_from("<20I", metadata, assets_at + index * 80)
        key = metadata[strings_at + record[16]:strings_at + record[16] + record[17]].decode("utf-8")
        records.append((key, record))
    colours = []
    for index in range(palettes):
        entry = struct.unpack_from("<I16H", metadata, palettes_at + index * 36)
        assert entry[0] <= 16
        colours.append(entry[1:1 + entry[0]])
    return records, colours, blocks


def assert_source_pixels(test, data, images):
    records, palettes, blocks = decode_container(data)
    test.assertEqual({key for key, _ in records}, set(images))
    for key, record in records:
        ident, width, height, format_id, palette_id, block, offset, size, index_stride, mask_stride = record[:10]
        image = images[key].convert("RGBA")
        test.assertEqual((width, height), image.size)
        payload = blocks[block][offset:offset + size]
        test.assertEqual(len(payload), size)
        if format_id == 2:
            # Font mask uses glyph order rather than full-image row order.
            expected = bytearray()
            for glyph in range(96):
                for y in range(12):
                    expected.append(sum(bool(image.getpixel(((glyph % 16) * 8 + x, (glyph // 16) * 12 + y))[3]) << (7 - x) for x in range(8)))
            test.assertEqual(payload, expected)
            continue
        test.assertEqual(format_id, 1)
        test.assertEqual(index_stride, (width + 1) // 2)
        test.assertEqual(mask_stride, (width + 7) // 8)
        test.assertEqual(size, (index_stride + mask_stride) * height)
        mask_start = index_stride * height
        for y in range(height):
            for x in range(width):
                r, g, b, alpha = image.getpixel((x, y))
                covered = (payload[mask_start + y * mask_stride + x // 8] >> (7 - x % 8)) & 1
                test.assertEqual(covered, bool(alpha), (key, x, y))
                if alpha:
                    packed = payload[y * index_stride + x // 2]
                    colour_index = (packed >> (4 if x % 2 == 0 else 0)) & 15
                    test.assertEqual(palettes[palette_id][colour_index], ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3), (key, x, y))
            if width % 2:
                test.assertEqual(payload[y * index_stride + index_stride - 1] & 15, 0)
            if width % 8:
                test.assertEqual(payload[mask_start + y * mask_stride + mask_stride - 1] & ((1 << (8 - width % 8)) - 1), 0)
    return records, palettes, blocks



class ArtworkBuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import jellipak
        cls.compiler = jellipak

    def cli(self, *args):
        return subprocess.run([sys.executable, str(ROOT / "tools/assets/jellipak.py"), *map(str, args)], capture_output=True, text=True, timeout=120, check=False)

    def fixture(self):
        # The first two RGB888 colours collide in RGB565. Transparency is
        # separate from all sixteen opaque palette indices, including zero.
        palette = ["#000000", "#010101"] + [f"#{n * 8:02x}0000" for n in range(1, 16)]
        image = Image.new("RGBA", (17, 2))
        values = [(int(c[1:3], 16), 0, 0, 255) for c in palette]
        image.putdata((values + [(1, 1, 1, 0)] * 17)[:34])
        images = {"one": image, "two": image.copy(), "empty": Image.new("RGBA", (3, 2), (250, 250, 250, 0))}
        manifest = {"assets": [{"id": index, "key": key, "kind": "icons"} for index, key in enumerate(images, 1)], "clips": [], "palette": palette}
        return manifest, images

    def test_collision_transparency_odd_rows_grouped_subranges(self):
        manifest, images = self.fixture()
        data, _ = self.compiler.compile_art(manifest, images, 16384)
        records, palettes, blocks = assert_source_pixels(self, data, images)
        nonempty = [record for key, record in records if key != "empty"]
        self.assertEqual(len(palettes[nonempty[0][4]]), 16)
        self.assertEqual(nonempty[0][4], nonempty[1][4])
        self.assertEqual(nonempty[0][5], nonempty[1][5])
        self.assertNotEqual(nonempty[0][6], nonempty[1][6])
        with tempfile.TemporaryDirectory(prefix="jellipak-fixture-") as directory:
            path = Path(directory) / "fixture.jpak"
            path.write_bytes(data)
            subprocess.run([str(VERIFIER), str(path)], check=True, capture_output=True, timeout=30)

    def test_deterministic_order_and_oversized_image(self):
        manifest, images = self.fixture()
        first, _ = self.compiler.compile_art(manifest, images, 16384)
        reordered = dict(manifest, assets=list(reversed(manifest["assets"])))
        second, _ = self.compiler.compile_art(reordered, dict(reversed(list(images.items()))), 16384)
        self.assertEqual(first, second)
        oversized = Image.new("RGBA", (1024, 1024), (0, 0, 0, 255))
        with self.assertRaises(ValueError):
            self.compiler.compile_art({"assets": [{"id": 1, "key": "large", "kind": "icons"}], "clips": [], "palette": ["#000000"]}, {"large": oversized}, 16384)

    def test_empty_palette_and_exact_decoded_capacity(self):
        manifest = {"assets": [{"id": 1, "key": "blank", "kind": "icons"}], "clips": [], "palette": ["#000000"]}
        images = {"blank": Image.new("RGBA", (3, 2), (123, 45, 67, 0))}
        data, _ = self.compiler.compile_art(manifest, images, 16384)
        records, palettes, _ = assert_source_pixels(self, data, images)
        self.assertEqual(palettes, [()])
        self.assertEqual(records[0][1][10:16], (0, 0, 0, 0, 0, 0))
        # 64 rows of 32 index bytes and 8 coverage bytes: exactly 2560.
        # Two images must become separate bounded blocks at this capacity.
        manifest["assets"] = [{"id": index, "key": key, "kind": "icons"} for index, key in enumerate(("a", "b"), 1)]
        images = {key: Image.new("RGBA", (64, 64), (0, 0, 0, 255)) for key in ("a", "b")}
        data, _ = self.compiler.compile_art(manifest, images, 2560)
        records, _, blocks = assert_source_pixels(self, data, images)
        self.assertNotEqual(records[0][1][5], records[1][1][5])
        self.assertTrue(all(len(block) <= 2560 for block in blocks.values()))
        with self.assertRaises(ValueError):
            self.compiler.compile_art(manifest, images, 2559)

    def test_full_catalog_fidelity_and_noop_publication(self):
        from build_slice import load_assets
        manifest, images = load_assets()
        with tempfile.TemporaryDirectory(prefix="jellipak-catalog-") as directory:
            path = Path(directory) / "catalog.jpak"
            command = ("build", "--output", path, "--verifier", VERIFIER, "--decoded-limit", 65536)
            result = self.cli(*command)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = path.read_bytes()
            records, _, _ = assert_source_pixels(self, data, images)
            by_key = dict(records)
            for asset in manifest["assets"]:
                record = by_key[asset["key"]]
                self.assertEqual(record[0], asset["id"])
                self.assertEqual(list(record[12:16]), asset["bounds"])
                self.assertEqual(list(record[10:12]), asset.get("ground_anchor_q8", [0, 0]))
                self.assertEqual(list(record[18:20]), asset["centroid_q8"])
            stamp = path.stat().st_mtime_ns
            result = self.cli(*command)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(path.read_bytes(), data)
            self.assertEqual(path.stat().st_mtime_ns, stamp)

    def test_full_catalog_precompression_bytes_match_native(self):
        from build_slice import load_assets
        manifest, images = load_assets()
        captured = []
        encoder = self.compiler.lz4.block.compress
        with mock.patch.object(self.compiler.lz4.block, "compress", wraps=encoder) as compress:
            data, report = self.compiler.compile_art(manifest, images, 16384, captured)
        # The oracle must be the exact buffers presented to the encoder, before
        # codec selection or pack parsing can reconstruct an equivalent stream.
        self.assertEqual([raw for _, raw in captured],
                         [call.args[0] for call in compress.call_args_list])
        self.assertGreater(len(captured), 0)
        compared_bytes = sum(len(raw) for _, raw in captured)
        self.assertGreater(compared_bytes, 0)
        self.assertEqual(report["pack_bytes"], len(data))
        self.assertEqual(report.get("native_byte_comparison"), None)

        with tempfile.TemporaryDirectory(prefix="jellipak-native-oracle-") as directory:
            path = Path(directory) / "catalog.jpak"
            path.write_bytes(data)
            output = self.compiler.native_verify(VERIFIER, path, captured)
            self.assertIn(f"compared_bytes={compared_bytes}", output.splitlines())
            self.assertNotEqual(self.compiler.native_verify(VERIFIER, path), "")
            self.assertEqual([entry.name for entry in Path(directory).iterdir()], [path.name])

            changed = list(captured)
            block_id, raw = changed[-1]
            changed[-1] = (block_id, bytes([raw[0] ^ 1]) + raw[1:])
            with self.assertRaisesRegex(ValueError, "native verifier rejected candidate"):
                self.compiler.native_verify(VERIFIER, path, changed)
            self.assertEqual(path.read_bytes(), data)
            self.assertEqual([entry.name for entry in Path(directory).iterdir()], [path.name])

    def test_precompression_mismatch_fails_build_without_replacing_pack(self):
        with tempfile.TemporaryDirectory(prefix="jellipak-oracle-failure-") as directory:
            output = Path(directory) / "art.jpak"
            previous = b"previous committed generation"
            output.write_bytes(previous)
            manifest, images = self.fixture()
            compile_art = self.compiler.compile_art

            def altered_oracle(manifest_arg, images_arg, decoded_limit, expected_blocks=None):
                candidate, report = compile_art(
                    manifest_arg, images_arg, decoded_limit, expected_blocks
                )
                block_id, raw = expected_blocks[-1]
                expected_blocks[-1] = (block_id, bytes([raw[0] ^ 1]) + raw[1:])
                return candidate, report

            args = ["build", "--output", str(output), "--verifier", str(VERIFIER),
                    "--decoded-limit", "16384"]
            with mock.patch.object(self.compiler, "load_assets", return_value=(manifest, images)), \
                    mock.patch.object(self.compiler, "compile_art", side_effect=altered_oracle):
                self.assertNotEqual(self.compiler.main(args), 0)
            self.assertEqual(output.read_bytes(), previous)
            self.assertEqual([entry.name for entry in Path(directory).iterdir()], [output.name])

    def test_source_destinations_rejected_before_compiling(self):
        with tempfile.TemporaryDirectory(prefix="jellipak-protected-") as directory:
            protected = self.compiler.SOURCE / (Path(directory).name + "-must-not-exist") / "output.jpak"
            self.assertFalse(protected.parent.exists())
            ordinary = Path(directory) / "output.jpak"
            for option in ("output", "report"):
                args = ["build", "--output", str(protected if option == "output" else ordinary), "--verifier", str(VERIFIER), "--decoded-limit", "65536"]
                if option == "report":
                    args += ["--report", str(protected)]
                with self.subTest(option=option), mock.patch.object(self.compiler, "load_assets", side_effect=AssertionError("source protection must precede loading")):
                    self.assertNotEqual(self.compiler.main(args), 0)
                self.assertFalse(protected.parent.exists())
                self.assertFalse(ordinary.exists())

    def test_checksummed_art_metadata_rejections(self):
        manifest = {"assets": [{"id": 1, "key": "tiny", "kind": "icons"}], "clips": [], "palette": ["#000000"]}
        data, _ = self.compiler.compile_art(manifest, {"tiny": Image.new("RGBA", (3, 2), (0, 0, 0, 255))}, 16384)
        records, _, original = decode_container(data)
        record = records[0][1]
        # Re-envelope each mutation with fresh directory/payload/header CRCs.
        # Thus failures must concern artwork semantics rather than corruption.
        mutations = {
            "palette reference": (1, 32 + 4 * 4, 1),
            "subrange": (1, 32 + 6 * 4, 1),
            "length": (1, 32 + 7 * 4, 1),
            "index stride": (1, 32 + 8 * 4, 0),
            "mask stride": (1, 32 + 9 * 4, 0),
            "palette count": (1, 32 + 80, 17),
        }
        for label, (block, offset, value) in mutations.items():
            with self.subTest(case=label):
                blocks = dict(original)
                changed = bytearray(blocks[block])
                struct.pack_into("<I", changed, offset, value)
                blocks[block] = bytes(changed)
                candidate = wire_pack([(0, blocks[key], blocks[key]) for key in sorted(blocks)])
                with self.assertRaises(ValueError):
                    self.compiler.read_pack(candidate)
        blocks = dict(original)
        changed = bytearray(blocks[record[5]])
        changed[record[6]] = 0xf0  # Visible index fifteen in a one-colour palette.
        blocks[record[5]] = bytes(changed)
        with self.assertRaises(ValueError):
            self.compiler.read_pack(wire_pack([(0, blocks[key], blocks[key]) for key in sorted(blocks)]))

    def test_aggregate_decoded_limit_precedes_decompression(self):
        block_count = self.compiler.MAX_DECODED_BYTES // (1024 * 1024) + 1
        data = bytearray(wire_pack([(1, b"x", b"x")] * block_count))
        for index in range(block_count):
            struct.pack_into("<I", data, 32 + 32 * index + 20, 1024 * 1024)
        struct.pack_into("<I", data, 24, zlib.crc32(data[32:32 + 32 * block_count]))
        struct.pack_into("<I", data, 28, zlib.crc32(data[:28]))
        with mock.patch.object(self.compiler.lz4.block, "decompress", side_effect=AssertionError("must reject aggregate before allocating")):
            with self.assertRaisesRegex(ValueError, "aggregate decoded"):
                self.compiler.read_pack(data)

    def test_verifier_timeout_preserves_prior_pack(self):
        with tempfile.TemporaryDirectory(prefix="jellipak-timeout-") as directory:
            path = Path(directory) / "art.jpak"
            path.write_bytes(b"previous generation")
            manifest, images = self.fixture()
            data, report = self.compiler.compile_art(manifest, images, 16384)
            args = ["build", "--output", str(path), "--verifier", str(VERIFIER), "--decoded-limit", "16384"]
            with mock.patch.object(self.compiler, "load_assets", return_value=(manifest, images)), mock.patch.object(self.compiler, "compile_art", return_value=(data, report)), mock.patch.object(self.compiler.subprocess, "run", side_effect=subprocess.TimeoutExpired("verifier", 60)) as run:
                self.assertNotEqual(self.compiler.main(args), 0)
                self.assertEqual(run.call_args.kwargs["timeout"], 60)
            self.assertEqual(path.read_bytes(), b"previous generation")
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_failed_verifier_preserves_previous_pack(self):
        with tempfile.TemporaryDirectory(prefix="jellipak-build-") as directory:
            output = Path(directory) / "art.jpak"
            output.write_bytes(b"previous committed generation")
            before = output.stat().st_mtime_ns
            # Python rejects the binary candidate with a nonzero exit. This
            # exercises an actual launched verifier failure as well as absence.
            for verifier in (sys.executable, Path(directory) / "missing-verifier"):
                with self.subTest(verifier=str(verifier)):
                    result = self.cli("build", "--output", output, "--verifier", verifier, "--decoded-limit", 65536)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(output.read_bytes(), b"previous committed generation")
                    self.assertEqual(output.stat().st_mtime_ns, before)

    def test_invalid_cli_limits(self):
        with tempfile.TemporaryDirectory(prefix="jellipak-build-") as directory:
            output = Path(directory) / "art.jpak"
            for limit in ("0", "-1", "not-an-integer", "4294967296"):
                with self.subTest(limit=limit):
                    result = self.cli("build", "--output", output, "--verifier", VERIFIER, "--decoded-limit", limit)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertFalse(output.exists())


if __name__ == "__main__":
    VERIFIER = Path(sys.argv.pop(1)).resolve()
    unittest.main()

"""Bounded authoring packs, stale metadata, atomic failure and watcher debounce."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
import zlib

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/assets"))
import live_assets


class LiveAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "assets"
        self.source.mkdir()
        self.destination = self.root / "live.jlap"
        self.creature = Image.new("RGBA", (32, 32))
        self.creature.putpixel((12, 23), (200, 50, 111, 255))
        self.creature.save(self.source / "pet.png")
        font = Image.new("RGBA", (128, 72))
        font.putpixel((9, 13), (255, 255, 255, 255))
        font.save(self.source / "font.png")
        self.manifest = {
            "schema_version": 1, "palette": ["#000000"],
            "assets": [
                {"id": 1001, "kind": "creatures", "width": 32, "height": 32,
                 "path": "pet.png", "bounds": [0, 0, 1, 1], "centroid_q8": [0, 0]},
                {"id": 4001, "kind": "font", "width": 128, "height": 72,
                 "path": "font.png", "bounds": [0, 0, 1, 1]},
            ],
        }
        self.write_manifest()

    def tearDown(self):
        self.temporary.cleanup()

    def write_manifest(self):
        (self.source / "assets.json").write_text(json.dumps(self.manifest))

    def test_binary_layout_and_actual_geometry(self):
        data = live_assets.build_pack(self.source)
        magic, version, count, crc = live_assets.HEADER.unpack_from(data)
        self.assertEqual((magic, version, count), (b"JLAP", 1, 2))
        self.assertEqual(crc, zlib.crc32(data[16:]))
        record = live_assets.RECORD.unpack_from(data, 16)
        self.assertEqual(record[:3], (1001, 32, 32))
        self.assertEqual(record[3:7], (3200, 6144, 3200, 6016))
        self.assertEqual(record[7:11], (12, 23, 13, 24))
        self.assertEqual(record[11], 2176)
        pixel = struct.unpack_from("<H", data, 40 + (23 * 32 + 12) * 2)[0]
        self.assertEqual(pixel, (200 >> 3) << 11 | (50 >> 2) << 5 | (111 >> 3))
        self.assertEqual(data[40 + 2048 + 23 * 4 + 1], 8)
        font_offset = 40 + 2176
        font_record = live_assets.RECORD.unpack_from(data, font_offset)
        self.assertEqual(font_record[0], 4001)
        self.assertEqual(font_record[-1], 1152)
        self.assertEqual(data[font_offset + 24 + 17 * 12 + 1], 64)

    def test_bad_png_preserves_pack_and_error_is_reported_once(self):
        messages = []
        publisher = live_assets.Publisher(self.source, self.destination, messages.append)
        self.assertTrue(publisher.refresh())
        original = self.destination.read_bytes()
        self.creature.putpixel((12, 23), (200, 50, 111, 123))
        self.creature.save(self.source / "pet.png")
        self.assertFalse(publisher.refresh())
        self.assertFalse(publisher.refresh())
        self.assertEqual(len(messages), 2)
        self.assertEqual(original, self.destination.read_bytes())
        Image.new("RGBA", (31, 32)).save(self.source / "pet.png")
        self.assertFalse(publisher.refresh())
        self.assertEqual(len(messages), 3)
        self.assertEqual(original, self.destination.read_bytes())
        self.creature.putpixel((12, 23), (100, 200, 50, 255))
        self.creature.save(self.source / "pet.png")
        self.assertTrue(publisher.refresh())
        self.assertNotEqual(original, self.destination.read_bytes())

    def test_atomic_replace_failure_keeps_previous_and_cleans_temporary(self):
        live_assets.publish_pack(self.source, self.destination)
        original = self.destination.read_bytes()
        with patch.object(live_assets.os, "replace", side_effect=OSError("busy file")):
            with self.assertRaises(OSError):
                live_assets.publish_pack(self.source, self.destination)
        self.assertEqual(original, self.destination.read_bytes())
        self.assertEqual(list(self.root.glob("live.jlap.*.tmp")), [])

    def test_path_escape_duplicate_and_empty_art_are_rejected(self):
        self.manifest["assets"][0]["path"] = "../pet.png"
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "escapes"):
            live_assets.build_pack(self.source)
        self.manifest["assets"][0]["path"] = "pet.png"
        self.manifest["assets"].append(dict(self.manifest["assets"][0]))
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            live_assets.build_pack(self.source)
        self.manifest["assets"].pop()
        self.write_manifest()
        Image.new("RGBA", (32, 32)).save(self.source / "pet.png")
        with self.assertRaisesRegex(ValueError, "Empty"):
            live_assets.build_pack(self.source)

    def test_debounce_publishes_once_after_stable_edit(self):
        child = Mock(returncode=0)
        child.poll.side_effect = [None, None, None, None, 0]
        publisher = Mock(source=self.source, last_error=None)
        with patch.object(live_assets, "fingerprint", side_effect=[0, 1, 1, 2, 2]), \
                patch.object(live_assets.time, "monotonic", side_effect=[0, 0, 0.3, 0.4, 0.4, 1]), \
                patch.object(live_assets.time, "sleep"):
            self.assertEqual(live_assets.watch(child, publisher, 0.5), 0)
        publisher.refresh.assert_called_once()

    def test_release_binary_detection(self):
        self.assertEqual(live_assets.find_binary(self.root), self.root / "build/desktop/jelligotchi")
        windows = self.root / "jelligotchi.exe"
        windows.touch()
        self.assertEqual(live_assets.find_binary(self.root), windows)
        mac = self.root / "Jelligotchi.app/Contents/MacOS/jelligotchi"
        mac.parent.mkdir(parents=True)
        mac.touch()
        self.assertEqual(live_assets.find_binary(self.root), mac)

    def test_launch_stages_before_start_and_forwards_sdl_arguments(self):
        child = Mock(returncode=0)
        child.poll.return_value = 0
        arguments = []

        def launch(command, cwd):
            self.assertTrue(self.destination.is_file())
            arguments.extend(command)
            return child

        argv = ["live_assets.py", "--assets", str(self.source), "--pack", str(self.destination),
                "--binary", str(self.root / "game"), "--", "--headless", "--frames", "1"]
        with patch.object(sys, "argv", argv), patch.object(live_assets.subprocess, "Popen", launch), \
                patch("builtins.print"):
            self.assertEqual(live_assets.main(), 0)
        self.assertEqual(arguments, [str((self.root / "game").resolve()), "--pet", "--asset-pack",
                                     str(self.destination.resolve()), "--headless", "--frames", "1"])

    def test_large_pack_is_rejected_before_replacing_old_pack(self):
        live_assets.publish_pack(self.source, self.destination)
        original = self.destination.read_bytes()
        background = Image.new("RGBA", (64, 64), (100, 150, 200, 255))
        self.manifest["assets"] = []
        for index in range(33):
            name = f"background-{index}.png"
            background.save(self.source / name)
            self.manifest["assets"].append({"id": 10000 + index, "kind": "backgrounds",
                                             "width": 64, "height": 64, "path": name})
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "live banks"):
            live_assets.publish_pack(self.source, self.destination)
        self.assertEqual(original, self.destination.read_bytes())

    def test_file_capacity_is_enforced_before_replacing_old_pack(self):
        live_assets.publish_pack(self.source, self.destination)
        original = self.destination.read_bytes()
        with patch.object(live_assets, "MAX_PACK_BYTES", len(original) - 1):
            with self.assertRaisesRegex(ValueError, "byte buffer"):
                live_assets.publish_pack(self.source, self.destination)
        self.assertEqual(original, self.destination.read_bytes())


if __name__ == "__main__":
    unittest.main()

# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Focused contract checks for scripts/engine-evidence.py."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("engine_evidence", ROOT / "scripts/engine-evidence.py")
evidence = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(evidence)


class EngineEvidenceTests(unittest.TestCase):
    def test_all_scenes_render_isolated_deterministic_images_and_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "evidence"
            binary = Path(temporary) / "jelligotchi"
            binary.write_bytes(b"fake executable")
            binary.chmod(0o755)
            commands = []

            def run(command, **kwargs):
                commands.append(command)
                snapshot = Path(command[command.index("--snapshot") + 1])
                speed = int(command[command.index("--travel-speed") + 1]) if "--travel-speed" in command else 0
                image = Image.new("RGB", evidence.SIZE, (80 + speed, 90, 110))
                for x in range(0, evidence.SIZE[0], 8):
                    image.paste((x % 255, 170, 40), (x, 0, min(x + 4, evidence.SIZE[0]), evidence.SIZE[1]))
                image.save(snapshot)
                return subprocess.CompletedProcess(command, 0, "", "")

            with mock.patch.object(evidence, "ROOT", ROOT), \
                 mock.patch.object(evidence.subprocess, "run", side_effect=run), \
                 mock.patch.object(evidence.subprocess, "check_output", return_value="test-sha\n"), \
                 mock.patch("sys.argv", ["engine-evidence", "--binary", str(binary), "--output", str(output)]):
                self.assertEqual(evidence.main(), 0)

            scenes = evidence.scenes_from(ROOT / "content/scenes.json")
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(len(commands), len(scenes) * 2)
            self.assertEqual(manifest["git_sha"], "test-sha")
            self.assertEqual(manifest["scene_count"], len(scenes))
            self.assertEqual(len(manifest["renders"]), len(scenes) * 2)
            self.assertTrue(manifest["checks"]["all_images_nonblank"])
            self.assertGreater(manifest["checks"]["market_travel_changed_pixels"], 100)
            for index in range(0, len(commands), 2):
                self.assertIn("--headless", commands[index])
                self.assertIn("--scene", commands[index])
                self.assertIn("--save", commands[index])
                self.assertNotEqual(commands[index][commands[index].index("--save") + 1],
                                    commands[index + 1][commands[index + 1].index("--save") + 1])
            self.assertTrue(list(output.glob("contact-sheet-*.png")))

    def test_nonzero_game_exit_is_a_hard_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "game"
            binary.write_bytes(b"binary")
            binary.chmod(0o755)
            scene = {"key": "market-street"}
            with mock.patch.object(evidence.subprocess, "run", return_value=subprocess.CompletedProcess([], 2, "", "bad")):
                with self.assertRaisesRegex(RuntimeError, "render failed"):
                    evidence.render(binary, Path(temporary), scene, "early", 1, 0)


if __name__ == "__main__":
    unittest.main()

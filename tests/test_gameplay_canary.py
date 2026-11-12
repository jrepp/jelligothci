# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Contract checks for the native gameplay canary."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gameplay_canary", ROOT / "scripts/gameplay-canary.py")
canary = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(canary)


class GameplayCanaryTests(unittest.TestCase):
    def test_scenarios_cover_nine_distinct_cases(self):
        cases = canary.scenarios()
        self.assertEqual(len(cases), 9)
        self.assertEqual(len({case["key"] for case in cases}), 9)
        self.assertEqual({case["expected"]["form_name"] for case in cases},
                         {"MINT", "LILAC", "BABY AXO", "AXOLOTL"})
        self.assertTrue(any(case["expected"].get("activity") == "eating" for case in cases))
        self.assertTrue(any("--effect" in case["args"] for case in cases))

    def test_state_mismatch_fails_loudly(self):
        with self.assertRaisesRegex(ValueError, "menu is False, expected True"):
            canary.validate_state("menu", {"menu": False}, {"menu": True})
        with self.assertRaisesRegex(ValueError, "entry is None"):
            canary.validate_state("pet", {}, {"entry": 1})

    def test_grid_contains_all_nine_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            records = []
            for index in range(9):
                path = folder / f"{index}.png"
                Image.new("RGB", canary.SIZE, (index * 20, 30, 50)).save(path)
                records.append({"label": f"case {index}", "png": path.name,
                                "state": {"form_name": "MINT", "activity": "idle", "page": 0, "menu": False}})
            output = folder / "grid.png"
            canary.grid(records, folder, output)
            with Image.open(output) as image:
                self.assertEqual(image.size, (1398, 1584))
                self.assertEqual(image.getpixel((5, 62)), (0, 30, 50))
                self.assertEqual(image.getpixel((2 * 466 + 5, 2 * 528 + 62)), (160, 30, 50))
            with self.assertRaisesRegex(ValueError, "exactly nine"):
                canary.grid(records[:8], folder, output)


if __name__ == "__main__":
    unittest.main()

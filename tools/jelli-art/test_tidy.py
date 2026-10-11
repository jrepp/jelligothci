# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Tidy outline (POST /api/tidy) and the icon re-ink pass give the same pixels in every process.

Run: ./scripts/uv run --python 3.12 tools/jelli-art/test_tidy.py
"""
import hashlib
import os
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SOURCE = REPO / "assets" / "slice" / "source"
sys.path.insert(0, str(SOURCE))
from pixel_kit import despeck  # noqa: E402

# menus.water has despeck ties, and barbell is an ordinary 32 px menu icon.
ASSETS = ("menus/water.png", "menus/barbell.png")
# Each run prints a hash of every tidied asset; Python randomises string hashes per process.
PROBE = """
import hashlib, sys
sys.path.insert(0, sys.argv[1])
from PIL import Image
from icon_polish import reink
from pixel_kit import to_image
for path in sys.argv[2:]:
    image = Image.open(path)
    print(hashlib.sha256(to_image(reink(image), image.size).tobytes()).hexdigest())
"""


def tidy_hashes(seed):
    env = {**os.environ, "PYTHONHASHSEED": str(seed)}
    paths = [str(REPO / "assets" / "slice" / a) for a in ASSETS]
    run = subprocess.run([sys.executable, "-c", PROBE, str(SOURCE), *paths], env=env,
                         capture_output=True, text=True, check=True)
    return run.stdout.split()


class TidyTest(unittest.TestCase):
    def test_same_pixels_under_every_hash_seed(self):
        # Before the fix, these seeds gave six different results for menus.water.
        first = tidy_hashes(0)
        self.assertEqual(len(first), len(ASSETS))
        for seed in (1, 2, 6, 7):
            self.assertEqual(tidy_hashes(seed), first, f"PYTHONHASHSEED={seed}")

    def test_despeck_ties_go_to_the_first_neighbour(self):
        # A coral speck with two mint and two gold neighbours: N8 lists (1, 0) first, so mint wins.
        sprite = {(5, 5): "coral", (6, 5): "mint", (4, 5): "gold", (5, 6): "mint", (5, 4): "gold"}
        self.assertEqual(despeck(sprite)[(5, 5)], "mint")
        flipped = {**sprite, (6, 5): "gold", (4, 5): "mint", (5, 6): "gold", (5, 4): "mint"}
        self.assertEqual(despeck(flipped)[(5, 5)], "gold")


if __name__ == "__main__":
    unittest.main()

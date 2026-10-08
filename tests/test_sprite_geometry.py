# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Contact anchors are invariant under padding and ignore upper-body motion."""
from pathlib import Path
import sys
import unittest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/assets"))
from sprite_geometry import ground_anchor_q8, opaque_centroid_q8
from build_slice import load_assets


def shape(points, size=(12, 12)):
    image = Image.new("RGBA", size)
    for point in points:
        image.putpixel(point, (255, 255, 255, 255))
    return image


class GroundAnchorTests(unittest.TestCase):
    def test_pixel_centers_and_empty(self):
        self.assertEqual(ground_anchor_q8(shape([(3, 5)])), [896, 1536])
        with self.assertRaisesRegex(ValueError, "empty"):
            ground_anchor_q8(shape([]))

    def test_lower_band_centroid_not_bounds_center(self):
        # Uneven feet: four pixels on the left, one on the right.
        foot = [(2, 8), (3, 8), (2, 9), (3, 9), (9, 9)]
        self.assertEqual(ground_anchor_q8(shape(foot)), [1101, 2560])
        self.assertEqual(ground_anchor_q8(shape(foot + [(0, 0), (11, 2)])), [1101, 2560])

    def test_padding_and_sitting_pose(self):
        body = shape([(3, y) for y in range(2, 10)] + [(4, 9), (5, 9)])
        sit = shape([(3, y) for y in range(6, 10)] + [(4, 9), (5, 9)])
        self.assertEqual(ground_anchor_q8(body), ground_anchor_q8(sit))
        padded = Image.new("RGBA", (20, 20))
        padded.paste(body, (5, 7))
        original = ground_anchor_q8(body)
        self.assertEqual(ground_anchor_q8(padded), [original[0] + 5 * 256, original[1] + 7 * 256])

    def test_full_centroid_padding_and_asymmetry(self):
        image = shape([(1, 1), (1, 2), (7, 8)])
        self.assertEqual(opaque_centroid_q8(image), [896, 1067])
        padded = Image.new("RGBA", (30, 30))
        padded.paste(image, (5, 7))
        self.assertEqual(opaque_centroid_q8(padded), [896 + 1280, 1067 + 1792])
        with self.assertRaises(ValueError):
            opaque_centroid_q8(shape([]))

    def test_real_pose_metadata(self):
        manifest, images = load_assets()
        creatures = [a for a in manifest["assets"] if a["kind"] == "creatures"]
        self.assertEqual(len(creatures), 16)
        for asset in creatures:
            x, y = asset["ground_anchor_q8"]
            self.assertEqual(y, asset["bounds"][3] * 256)
            self.assertTrue(asset["bounds"][0] * 256 < x < asset["bounds"][2] * 256)
            self.assertEqual([x, y], ground_anchor_q8(images[asset["key"]]))


if __name__ == "__main__":
    unittest.main()

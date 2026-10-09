# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Contact anchors are invariant under padding and ignore upper-body motion."""
from pathlib import Path
import copy
import sys
import tempfile
import unittest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/assets"))
from sprite_geometry import ground_anchor_q8, logical_bounds, opaque_centroid_q8
from build_slice import export_pixels, load_assets


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
        self.assertEqual(len(creatures), 32)  # Mint, Lilac, ninja and unicorn: eight poses each.
        for asset in creatures:
            x, y = asset["ground_anchor_q8"]
            self.assertEqual(y, asset["bounds"][3] * 256)
            self.assertTrue(asset["bounds"][0] * 256 < x < asset["bounds"][2] * 256)
            self.assertEqual([x, y], ground_anchor_q8(images[asset["key"]], asset.get("density", 1)))


class DensityTests(unittest.TestCase):
    """Stored measurements divide by density into the same logical Q8 units."""

    def test_nearest_upscale_keeps_logical_metadata(self):
        foot = shape([(2, 8), (3, 8), (2, 9), (3, 9), (9, 9), (5, 1), (6, 4)])
        for density in (2, 3, 4, 6, 8):
            big = foot.resize((12 * density, 12 * density), Image.Resampling.NEAREST)
            with self.subTest(density=density):
                self.assertEqual(ground_anchor_q8(big, density), ground_anchor_q8(foot))
                self.assertEqual(opaque_centroid_q8(big, density), opaque_centroid_q8(foot))
                self.assertEqual(logical_bounds(big, density), list(foot.getbbox()))

    def test_threshold_and_partial_logical_pixels(self):
        image = Image.new("RGBA", (12, 12))
        image.putpixel((7, 5), (9, 9, 9, 127))  # Below the coverage threshold.
        image.putpixel((7, 6), (9, 9, 9, 128))
        # Stored (7, 6) at density 6 is inside logical (1, 1): bounds round outwards.
        self.assertEqual(logical_bounds(image, 6), [1, 1, 2, 2])
        self.assertEqual(opaque_centroid_q8(image, 6), [320, 277])  # (15/6, 13/6) x 128.
        self.assertEqual(ground_anchor_q8(image, 6), [320, 299])  # Bottom edge 7/6 = 298.7.
        with self.assertRaisesRegex(ValueError, "empty"):
            logical_bounds(Image.new("RGBA", (6, 6), (1, 2, 3, 127)), 6)


class PackBudgetTests(unittest.TestCase):
    """The shipped high-resolution export is pinned against the 6 MiB budget."""

    def export(self, manifest, images):
        with tempfile.TemporaryDirectory() as folder:
            return export_pixels(Path(folder), manifest, images)

    def test_real_inventory_within_ceiling(self):
        manifest, images = load_assets()
        report = self.export(manifest, images)
        self.assertEqual(report["pixel_bytes"], 4981664)
        self.assertEqual(report["planned_pack_bytes"], 4993952)
        self.assertEqual(report["pack_ceiling"], 6 * 1024 * 1024)
        self.assertEqual(report["hires_assets"], 95)
        creatures = [r for r in report["assets"] if 1001 <= r["id"] <= 1048]
        self.assertEqual(len(creatures), 32)
        # 192x192 stored RGB565 plus an 8-bit alpha plane, for every creature frame.
        self.assertTrue(all(r["bytes"] == 192 * 192 * 3 for r in creatures))

    def test_growth_is_rejected(self):
        manifest, images = load_assets()
        frame = next(a for a in manifest["assets"] if a["id"] == 1031)
        hires = {**frame, "density": 6}  # 110,592 runtime bytes per frame.
        images["hires"] = images[frame["key"]].resize((192, 192), Image.Resampling.NEAREST)
        # 11 more high-resolution frames still fit 6 MiB; 12 exceed it.
        for extra, fits in ((11, True), (12, False)):
            grown = copy.deepcopy(manifest)
            for n in range(extra):
                key = f"creatures.test-extra-{n}"
                grown["assets"].append({**hires, "id": 1900 + n, "key": key})
                images[key] = images["hires"]
            with self.subTest(extra=extra):
                if fits:
                    self.export(grown, images)
                else:
                    with self.assertRaisesRegex(ValueError, "6 MiB"):
                        self.export(grown, images)


if __name__ == "__main__":
    unittest.main()

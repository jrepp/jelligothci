# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""High-resolution assets: manifest validation, exports, budget, C embedding, live pack."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/assets"))
sys.path.insert(0, str(ROOT / "tests"))
import build_slice  # noqa: E402
import embed_slice  # noqa: E402
import live_assets  # noqa: E402
from hires_fixture import DENSITIES, make_source  # noqa: E402

# Full production set: 32 creatures (four pets) and every density in the README.
FULL_SET_BYTES = 4_981_664
HIRES_ASSETS = 95


class HiresPipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.source, cls.inventory = make_source(cls.root / "hires")
        cls.legacy, _ = build_slice.load_assets(make_source(cls.root / "legacy", legacy=True)[0])
        cls.pristine = (cls.source / "assets.json").read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def manifest(self):
        return json.loads(self.pristine)

    def write(self, manifest):
        (self.source / "assets.json").write_text(json.dumps(manifest))

    def tearDown(self):
        make_source(self.source)  # Undo any per-test corruption.

    def load_error(self, change, pattern):
        manifest = self.manifest()
        change(manifest, next(a for a in manifest["assets"] if a["id"] == 6001))
        self.write(manifest)
        with self.assertRaisesRegex(ValueError, pattern):
            build_slice.load_assets(self.source)

    def test_logical_metadata_matches_legacy(self):
        manifest, images = build_slice.load_assets(self.source)
        legacy = {a["id"]: a for a in self.legacy["assets"]}
        hires = [a for a in manifest["assets"] if "density" in a]
        self.assertEqual(len(hires), HIRES_ASSETS)
        for asset in hires:
            original = legacy[asset["id"]]
            self.assertEqual(images[asset["key"]].size,
                             (asset["width"] * asset["density"], asset["height"] * asset["density"]))
            for field in ("bounds", "centroid_q8", "ground_anchor_q8"):
                self.assertEqual(asset.get(field), original.get(field), (asset["key"], field))

    def test_full_set_budget_is_pinned(self):
        manifest, images = build_slice.load_assets(self.source, self.inventory)
        with tempfile.TemporaryDirectory() as folder:
            report = build_slice.export_pixels(Path(folder), manifest, images)
            files = {p.name for p in (Path(folder) / "pixels").iterdir()}
        self.assertEqual(report["pixel_bytes"], FULL_SET_BYTES)
        self.assertEqual(report["hires_assets"], HIRES_ASSETS)
        self.assertLessEqual(report["planned_pack_bytes"], report["pack_ceiling"])
        unicorn = next(r for r in report["assets"] if r["id"] == 1041)
        self.assertEqual(unicorn["bytes"], 192 * 192 * 3)
        self.assertEqual(unicorn["stored_size"], [192, 192])
        self.assertTrue(unicorn["files"]["creatures-unicorn-idle-a.a8"]["runtime"])
        self.assertFalse(unicorn["files"]["creatures-unicorn-idle-a.mask"]["runtime"])
        self.assertIn("creatures-unicorn-idle-a.mask", files)

    def test_growth_past_ceiling_is_rejected(self):
        manifest, images = build_slice.load_assets(self.source, self.inventory)
        frame = next(a for a in manifest["assets"] if a["id"] == 1041)
        grown = copy.deepcopy(manifest)
        # One more eight-pose pet fits; two do not.
        for extra in range(16):
            key = f"creatures.test-extra-{extra}"
            grown["assets"].append({**frame, "id": 1900 + extra, "key": key})
            images[key] = images[frame["key"]]
        with tempfile.TemporaryDirectory() as folder:
            build_slice.export_pixels(Path(folder), {**grown, "assets": grown["assets"][:-8]}, images)
            with self.assertRaisesRegex(ValueError, "6 MiB"):
                build_slice.export_pixels(Path(folder), grown, images)

    def test_shipped_art_is_high_resolution(self):
        manifest, images = build_slice.load_assets()
        for asset in manifest["assets"]:
            density = DENSITIES.get(asset["kind"])
            with self.subTest(key=asset["key"]):
                self.assertEqual(asset.get("density"), density)
                if density:
                    self.assertTrue(asset["path"].startswith(f"hires/{asset['kind']}/"))
                    alpha = images[asset["key"]].getchannel("A").getdata()
                    self.assertTrue(any(0 < a < 255 for a in alpha))  # Soft, not binary.
        poses = ["idle-a", "idle-b", "eating", "happy", "asleep", "unwell", "curious", "content"]
        by_id = {a["id"]: a for a in manifest["assets"]}
        for n, pose in enumerate(poses):
            unicorn = by_id[1041 + n]
            self.assertEqual((unicorn["key"], unicorn["form"], unicorn["pose"]),
                             (f"creatures.unicorn-{pose}", "unicorn", pose))
        # The unicorn's clips mirror the ninja's frame for frame.
        clips = {c["key"]: c for c in manifest["clips"]}
        ninja = [c for c in manifest["clips"] if c["key"].startswith("ninja.")]
        self.assertEqual(len(ninja), 7)
        for clip in ninja:
            twin = clips["unicorn." + clip["key"].split(".", 1)[1]]
            self.assertEqual(twin["frames"], [f.replace("ninja-", "unicorn-") for f in clip["frames"]])
            self.assertEqual((twin["durations_ms"], twin["loop"]), (clip["durations_ms"], clip["loop"]))

    def test_invalid_entries_are_rejected(self):
        self.load_error(lambda m, a: a.update(density=4), "cannot draw at scales")
        self.load_error(lambda m, a: a.update(density=1), "Invalid density")
        self.load_error(lambda m, a: a.update(density="3"), "Invalid density")
        self.load_error(lambda m, a: a.update(density=9), "Invalid density")
        self.load_error(lambda m, a: a.update(path="menus/food.png"), "must be hires/menus/")
        self.load_error(lambda m, a: a.update(bounds=[0, 0, 32, 32]), "Bounds mismatch")
        self.load_error(lambda m, a: a.update(width=96, height=96), "Manifest dimensions")

        def background(manifest, _):
            entry = next(a for a in manifest["assets"] if a["kind"] == "backgrounds")
            entry.update(density=2)
        self.load_error(background, "legacy-only")

    def test_image_format_is_enforced(self):
        path = self.source / "hires/menus/food.png"
        original = Image.open(path).convert("RGBA")
        cases = [(original.resize((95, 96)), "Wrong dimensions"),
                 (original.convert("RGB"), "8-bit RGBA"),
                 (original.convert("P"), "8-bit RGBA"),
                 (Image.new("RGBA", (96, 96), (255, 0, 0, 127)), "Empty artwork")]
        for image, pattern in cases:
            with self.subTest(pattern=pattern):
                image.save(path)
                with self.assertRaisesRegex(ValueError, pattern):
                    build_slice.load_assets(self.source)
        original.save(path)

    def test_embedded_c_carries_alpha_and_density(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "assets.c"
            embed_slice.generate(output, self.source)
            text = output.read_text()
        self.assertIn("static const uint8_t asset_1001_alpha[36864] = {", text)
        self.assertIn("static const uint16_t asset_1001_pixels[36864] = {", text)
        self.assertNotIn("asset_1001_mask", text)
        self.assertIn("static const uint8_t asset_3001_mask[72] = {", text)  # Props stay legacy.
        line = next(l for l in text.splitlines() if l.startswith("    {1001u,"))
        self.assertTrue(line.endswith("asset_1001_pixels, 0, 0u, 4224u, 7424u, 4224u, 5505u, 6u, 12u, 27u, 29u, 6u, asset_1001_alpha},"), line)
        line = next(l for l in text.splitlines() if l.startswith("    {12004u,"))
        self.assertTrue(line.endswith(", 4u, asset_12004_alpha},"), line)

    def test_key_prefixes_embed_a_subset_and_always_the_font(self):
        # The voice agent embeds only the unicorn's poses (ports/esp32-agent/main).
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "agent_assets.c"
            embed_slice.generate(output, self.source, ("creatures.unicorn-",))
            text = output.read_text()
            ids = sorted(int(line.split("u,")[0].strip(" {")) for line in text.splitlines()
                         if line.startswith("    {") and "u, " in line)
            self.assertEqual(ids, [*range(1041, 1049), 4001])  # eight poses and the font
            self.assertIn("const uint8_t *jelli_asset_glyph(uint8_t codepoint)", text)
            self.assertNotIn("asset_1001_pixels", text)
            with self.assertRaisesRegex(ValueError, "No asset key starts with"):
                embed_slice.generate(Path(folder) / "none.c", self.source, ("no.such.prefix",))
            # Without prefixes everything is embedded, as for the pet.
            everything = Path(folder) / "all.c"
            embed_slice.generate(everything, self.source, ())
            self.assertIn("asset_1001_pixels", everything.read_text())

    def test_live_pack_records_density_and_alpha(self):
        data = live_assets.build_pack(self.source)
        magic, version, count, _ = live_assets.HEADER.unpack_from(data)
        self.assertEqual((magic, version, count), (b"JLAP", 2, 102))
        manifest, _ = build_slice.load_assets(self.source)
        expected = {a["id"]: a for a in manifest["assets"]}
        at = live_assets.HEADER.size
        while at < len(data):
            ident, width, height, gx, gy, cx, cy, left, top, right, bottom, length, density = \
                live_assets.RECORD.unpack_from(data, at)
            asset = expected.pop(ident)
            self.assertEqual(density, asset.get("density", 1))
            self.assertEqual([left, top, right, bottom], asset["bounds"])
            self.assertEqual([cx, cy], asset["centroid_q8"])
            self.assertEqual([gx, gy], asset.get("ground_anchor_q8", [0, 0]))
            if density > 1:
                self.assertEqual(length, width * height * density * density * 3)
                alpha = data[at + 28 + length * 2 // 3: at + 28 + length]
                self.assertTrue(any(0 < a < 255 for a in alpha), ident)  # Soft edges kept.
            at += live_assets.RECORD.size + length
        self.assertEqual(expected, {})
        self.assertLess(len(data), live_assets.MAX_PACK_BYTES)

    def test_preview_shows_hires(self):
        manifest, images = build_slice.load_assets(self.source)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            report = build_slice.export_pixels(output, manifest, images)
            build_slice.preview(output, manifest, images, report, self.source)
            html = (output / "preview.html").read_text()
            with Image.open(output / "contact-sheet.png") as sheet:
                self.assertEqual(sheet.width, 960)
        self.assertIn('"density": 6', html)
        self.assertIn("onScreen", html)


if __name__ == "__main__":
    unittest.main()

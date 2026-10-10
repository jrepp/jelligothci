# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Jelli Art animation timeline: new, duplicated and retired creature frames, and clip timing.

Run: ./scripts/uv run --python 3.12 tools/jelli-art/test_animation.py
"""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import animation  # noqa: E402
import creatures  # noqa: E402
import jelli_art  # noqa: E402


class FrameTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        root = Path(self.scratch.name)
        self.source, self.content = root / "slice", root / "content"
        shutil.copytree(REPO / "assets/slice", self.source)
        shutil.copytree(REPO / "content", self.content)
        self.manifest_path = self.source / "assets.json"
        names = ("SOURCE", "MANIFEST", "HAND_PAINTED", "PETS", "CREATURE_DATA", "GIT")
        self.saved = {n: getattr(jelli_art, n) for n in names}
        jelli_art.SOURCE, jelli_art.MANIFEST = self.source, self.manifest_path
        jelli_art.HAND_PAINTED = self.source / "source/hand-painted.json"
        jelli_art.PETS, jelli_art.CREATURE_DATA = self.content / "pets.json", self.content / "creatures.json"
        jelli_art.GIT = None

    def tearDown(self):
        for name, value in self.saved.items():
            setattr(jelli_art, name, value)
        self.scratch.cleanup()

    def manifest(self):
        return json.loads(self.manifest_path.read_text())

    def asset(self, key):
        return next((a for a in self.manifest()["assets"] if a["key"] == key), None)

    def clip(self, key):
        return next(c for c in self.manifest()["clips"] if c["key"] == key)

    def ledger(self):
        return json.loads((self.source / animation.LEDGER).read_text())["frames"]

    def test_duplicate_appends_after_the_import_spec_and_inserts_into_a_clip(self):
        before = self.manifest()
        spec_path = self.source / "source/axolotl-import.json"
        spec = json.loads(spec_path.read_text())
        expected_id = spec["first_id"] + len(spec["frames"])
        result = jelli_art.edit_frame({"action": "add", "form": "axolotl", "from": "creatures.axolotl-idle-2",
                                       "clip": "axolotl.idle", "index": 2})
        self.assertEqual((result["key"], result["id"]), ("creatures.axolotl-idle-2-copy", expected_id))
        new = self.asset(result["key"])
        source = self.asset("creatures.axolotl-idle-2")
        for field in ("width", "height", "pivot", "palette", "bounds", "kind", "form"):
            self.assertEqual(new[field], source[field], field)
        same = Image.open(self.source / new["path"]).convert("RGBA").tobytes()
        self.assertEqual(same, Image.open(self.source / source["path"]).convert("RGBA").tobytes())
        old_clip = next(c for c in before["clips"] if c["key"] == "axolotl.idle")
        frames = list(old_clip["frames"])
        frames.insert(2, result["key"])
        self.assertEqual(self.clip("axolotl.idle")["frames"], frames)
        self.assertEqual(len(self.clip("axolotl.idle")["durations_ms"]), len(frames))
        # Existing IDs never move; the new asset is appended.
        after = self.manifest()
        self.assertEqual([a["id"] for a in after["assets"][:-1]], [a["id"] for a in before["assets"]])
        self.assertEqual(after["assets"][-1]["key"], result["key"])
        updated = json.loads(spec_path.read_text())
        self.assertEqual(updated["frames"][:-1], spec["frames"])
        self.assertEqual(updated["frames"][-1]["studio"], True)
        self.assertIn(result["key"], json.loads(jelli_art.HAND_PAINTED.read_text()))
        self.assertEqual(self.ledger()[-1]["id"], expected_id)
        creatures.validate(after, after, self.source, creatures.load_validator(REPO))

    def test_blank_frame_takes_the_next_free_id_in_the_form_range(self):
        manifest = self.manifest()
        block = [a["id"] for a in manifest["assets"] if 1000 <= a["id"] < 1100]
        result = jelli_art.edit_frame({"action": "add", "form": "baby"})
        self.assertEqual(result["id"], max(block) + 1)
        self.assertEqual(result["key"], "creatures.baby-frame")
        image = Image.open(self.source / "creatures/baby-frame.png").convert("RGBA")
        pivot = tuple(self.asset("creatures.baby-idle-a")["pivot"])
        self.assertEqual(image.getchannel("A").getbbox(), (pivot[0], pivot[1], pivot[0] + 1, pivot[1] + 1))
        second = jelli_art.edit_frame({"action": "add", "form": "baby"})
        self.assertEqual((second["key"], second["id"]), ("creatures.baby-frame-2", result["id"] + 1))
        self.assertIsNone(second["clip"])

    def test_retired_ids_and_keys_are_never_reused(self):
        first = jelli_art.edit_frame({"action": "add", "form": "baby", "pose": "wave"})
        jelli_art.edit_frame({"action": "retire", "key": first["key"]})
        self.assertIsNone(self.asset(first["key"]))
        self.assertFalse((self.source / "creatures/baby-wave.png").exists())
        self.assertNotIn(first["key"], json.loads(jelli_art.HAND_PAINTED.read_text()))
        self.assertEqual(self.ledger()[-1], {"id": first["id"], "key": first["key"], "form": "baby", "retired": True})
        again = jelli_art.edit_frame({"action": "add", "form": "baby"})
        self.assertEqual(again["id"], first["id"] + 1)
        with self.assertRaisesRegex(jelli_art.StudioError, "used before"):
            jelli_art.edit_frame({"action": "add", "form": "baby", "pose": "wave"})

    def test_retiring_an_imported_frame_marks_its_spec_entry(self):
        unused = "creatures.axolotl-idle-4"
        clip = self.clip("axolotl.idle")
        with self.assertRaisesRegex(jelli_art.StudioError, r"clip axolotl\.idle"):
            jelli_art.edit_frame({"action": "retire", "key": unused})
        jelli_art.save_clips([{"key": "axolotl.idle", "frames": clip["frames"][:3], "durations_ms": clip["durations_ms"][:3],
                               "loop": True}])
        result = jelli_art.edit_frame({"action": "retire", "key": unused})
        self.assertEqual(result["id"], 1104)
        self.assertIsNone(self.asset(unused))
        spec = json.loads((self.source / "source/axolotl-import.json").read_text())
        entry = next(f for f in spec["frames"] if f"creatures.axolotl-{f['pose']}" == unused)
        self.assertTrue(entry["retired"])

    def test_frames_in_use_are_refused_with_their_users(self):
        original = self.manifest_path.read_text()
        with self.assertRaisesRegex(jelli_art.StudioError, r"clip baby\.idle"):
            jelli_art.edit_frame({"action": "retire", "key": "creatures.baby-idle-a"})
        portrait = jelli_art.edit_frame({"action": "add", "form": "baby", "pose": "portrait"})
        pets = json.loads(jelli_art.PETS.read_text())
        pets["forms"][0]["portrait"] = portrait["id"]
        jelli_art.PETS.write_text(json.dumps(pets))
        with self.assertRaisesRegex(jelli_art.StudioError, "portrait"):
            jelli_art.edit_frame({"action": "retire", "key": portrait["key"]})
        self.assertIn(portrait["key"], self.manifest_path.read_text())
        self.assertNotEqual(original, self.manifest_path.read_text())

    def test_rejected_requests_write_nothing(self):
        original = self.manifest_path.read_text()
        pngs = sorted(self.source.glob("creatures/*.png"))
        bad = [
            {"action": "add", "form": "dragon"},
            {"action": "add", "form": "baby", "from": "creatures.axolotl-idle-1"},
            {"action": "add", "form": "baby", "pose": "Bad Name"},
            {"action": "add", "form": "baby", "pose": "idle-a"},
            {"action": "add", "form": "baby", "clip": "axolotl.idle"},
            {"action": "add", "form": "baby", "clip": "baby.idle", "index": 9},
            {"action": "add", "form": "baby", "clip": "baby.study"},
            {"action": "add", "form": "baby", "clip": "baby.idle", "duration_ms": 0},
            {"action": "retire", "key": "icons.heart"},
            {"action": "explode"},
        ]
        for request in bad:
            with self.subTest(request=request), self.assertRaises(jelli_art.StudioError):
                jelli_art.edit_frame(request)
        self.assertEqual(self.manifest_path.read_text(), original)
        self.assertEqual(sorted(self.source.glob("creatures/*.png")), pngs)
        self.assertFalse((self.source / animation.LEDGER).exists())

    def test_full_clip_is_refused(self):
        clip = self.clip("axolotl.idle")
        edit = {"key": "axolotl.idle", "frames": [clip["frames"][0]] * 6, "durations_ms": [100] * 6, "loop": True}
        jelli_art.save_clips([edit])
        with self.assertRaisesRegex(jelli_art.StudioError, "1 to 6 frames"):
            jelli_art.edit_frame({"action": "add", "form": "axolotl", "clip": "axolotl.idle"})

    def test_failed_validation_writes_nothing(self):
        real = creatures.load_validator(REPO)

        def load_assets():  # build_slice on the scratch copy, which rejects the new frame
            manifest = json.loads((fake.SOURCE / "assets.json").read_text())
            if any(a["key"] == "creatures.baby-frame" for a in manifest["assets"]):
                raise ValueError("Off-palette pixel: creatures.baby-frame")
        fake = type("Validator", (), {"SOURCE": None, "load_assets": staticmethod(load_assets),
                                      "check_creature_clips": staticmethod(real.check_creature_clips)})
        original = self.manifest_path.read_text()
        with mock.patch.object(creatures, "load_validator", return_value=fake), \
                self.assertRaisesRegex(jelli_art.StudioError, "Off-palette"):
            jelli_art.edit_frame({"action": "add", "form": "baby"})
        self.assertEqual(self.manifest_path.read_text(), original)
        self.assertFalse((self.source / "creatures/baby-frame.png").exists())
        self.assertFalse((self.source / animation.LEDGER).exists())

    def test_preexisting_failure_warns_and_still_saves(self):
        manifest = self.manifest()
        manifest["palettes"]["axolotl"] = manifest["palettes"]["axolotl"][1:]
        self.manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        before = self.manifest_path.read_text()
        result = jelli_art.edit_frame({"action": "add", "form": "axolotl", "from": "creatures.axolotl-idle-1"})
        self.assertIn("already failed validation", result["warning"])  # baseline failed too: warn, still checked
        self.assertNotEqual(before, self.manifest_path.read_text())

    def test_budget_is_checked(self):
        manifest = self.manifest()
        fake = type("Live", (), {"MAX_ASSETS": len(manifest["assets"])})
        with self.assertRaisesRegex(animation.FrameError, "at most"):
            manifest["assets"].append(dict(manifest["assets"][0]))
            animation.check_budget(manifest, None, fake)

    def test_overview_lists_users_and_next_ids(self):
        overview = jelli_art.frames_overview()["frames"]
        self.assertIn("clip baby.idle", overview["baby"]["frames"]["creatures.baby-idle-a"])
        self.assertEqual(overview["axolotl"]["import_spec"], "axolotl-import.json")
        self.assertEqual(overview["baby"]["next_id"], 1025)

    def test_importer_keeps_studio_slots(self):
        sys.path.insert(0, str(REPO / "tools/assets"))
        import import_creature
        spec = json.loads((self.source / "source/axolotl-import.json").read_text())
        spec["frames"].append({"pose": "wave", "studio": True})
        spec["frames"].append({"source": "x.png", "pose": "late"})
        manifest = {"assets": [], "palettes": {}}
        imported = [f for f in spec["frames"] if not f.get("studio")]
        frames = [Image.new("RGBA", (48, 48), (1, 2, 3, 255)) for _ in imported]
        saved = import_creature.SOURCE
        import_creature.SOURCE = self.source
        try:
            import_creature.upsert(manifest, spec, frames)
        finally:
            import_creature.SOURCE = saved
        ids = {a["key"]: a["id"] for a in manifest["assets"]}
        self.assertNotIn("creatures.axolotl-wave", ids)
        self.assertEqual(ids["creatures.axolotl-late"], spec["first_id"] + len(spec["frames"]) - 1)


class ClipTimingTest(unittest.TestCase):
    """animation.js clipFrame mirrors core/creature.c jelli_clip_frame."""

    def test_clip_frame_matches_the_engine(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed")
        script = f"""
          global.window = {{}};
          require({json.dumps(str(HERE / 'animation.js'))});
          const f = window.JelliAnimation.clipFrame, out = [];
          const loop = {{frames: ['a', 'b', 'c'], durations_ms: [100, 200, 50], loop: true}};
          const once = {{...loop, loop: false}};
          for (const t of [0, 99, 100, 299, 300, 349, 350, 351, 699, 700, 10000]) out.push(f(loop, t), f(once, t));
          out.push(f({{frames: [], durations_ms: [], loop: true}}, 5));
          out.push(f({{frames: ['a', 'b'], durations_ms: [0, 0], loop: true}}, 5));
          console.log(JSON.stringify(out));
        """
        result = subprocess.run([node, "-e", script], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = [0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 2, 0, 2, 2, 2, 0, 2, 1, 2, 0, 0]
        self.assertEqual(json.loads(result.stdout), expected)


if __name__ == "__main__":
    unittest.main()

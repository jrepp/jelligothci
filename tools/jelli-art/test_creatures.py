# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Clip edits in Jelli Art: only clips change, and build_slice decides what is valid.

Run: ./scripts/uv run --python 3.12 tools/jelli-art/test_creatures.py
"""
import json
import shutil
import sys
import tempfile
import types
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import creatures  # noqa: E402
import jelli_art  # noqa: E402


class CreatureClipTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.source = Path(self.scratch.name) / "slice"
        shutil.copytree(REPO / "assets/slice", self.source)
        self.manifest_path = self.source / "assets.json"
        self.saved = (jelli_art.SOURCE, jelli_art.MANIFEST, jelli_art.GIT)
        jelli_art.SOURCE, jelli_art.MANIFEST, jelli_art.GIT = self.source, self.manifest_path, None

    def tearDown(self):
        jelli_art.SOURCE, jelli_art.MANIFEST, jelli_art.GIT = self.saved
        self.scratch.cleanup()

    def clip(self, manifest, key):
        return next(c for c in manifest["clips"] if c["key"] == key)

    def test_save_changes_only_the_named_clip(self):
        before = json.loads(self.manifest_path.read_text())
        edit = {"key": "axolotl.eating", "frames": ["creatures.axolotl-eating-2", "creatures.axolotl-eating-1"],
                "durations_ms": [300, 120], "loop": False}
        result = jelli_art.save_clips([edit])
        self.assertEqual(result["changed"], ["axolotl.eating"])
        self.assertNotIn("warning", result)
        text = self.manifest_path.read_text()
        after = json.loads(text)
        self.assertEqual(text, json.dumps(after, indent=2) + "\n")
        self.clip(before, "axolotl.eating").update({k: edit[k] for k in ("frames", "durations_ms", "loop")})
        self.assertEqual(after, before)

    def test_unchanged_edit_writes_nothing(self):
        stamp = self.manifest_path.stat().st_mtime_ns
        clip = self.clip(json.loads(self.manifest_path.read_text()), "baby.idle")
        self.assertEqual(jelli_art.save_clips([dict(clip)])["changed"], [])
        self.assertEqual(self.manifest_path.stat().st_mtime_ns, stamp)

    def test_rejected_edits_leave_the_manifest_alone(self):
        original = self.manifest_path.read_text()
        bad = [
            {"key": "axolotl.eating", "frames": ["creatures.baby-eating"], "durations_ms": [300], "loop": True},
            {"key": "axolotl.eating", "frames": ["creatures.axolotl-eating-1"], "durations_ms": [0], "loop": True},
            {"key": "axolotl.eating", "frames": ["creatures.axolotl-eating-1"], "durations_ms": [10001], "loop": True},
            {"key": "axolotl.eating", "frames": ["creatures.axolotl-eating-1"] * 7, "durations_ms": [90] * 7, "loop": True},
            {"key": "axolotl.eating", "frames": [], "durations_ms": [], "loop": True},
            {"key": "axolotl.eating", "frames": ["creatures.axolotl-eating-1"], "durations_ms": [90], "loop": 1},
            {"key": "axolotl.dance", "frames": ["creatures.axolotl-eating-1"], "durations_ms": [90], "loop": True},
        ]
        for edit in bad:
            with self.subTest(edit=edit), self.assertRaises(jelli_art.StudioError):
                jelli_art.save_clips([edit])
        self.assertEqual(self.manifest_path.read_text(), original)

    def test_validation_uses_build_slice_on_a_copy(self):
        manifest = json.loads(self.manifest_path.read_text())
        validator = creatures.load_validator(REPO)
        self.assertTrue(hasattr(validator, "load_assets"))
        self.assertEqual(creatures.validate(manifest, manifest, self.source, validator), "")
        broken = json.loads(json.dumps(manifest))
        broken["clips"] = [c for c in broken["clips"] if c["key"] != "axolotl.happy"]
        with self.assertRaises(creatures.ClipError):
            creatures.validate(broken, manifest, self.source, validator)
        self.assertEqual(validator.SOURCE, REPO / "assets/slice")

    def test_preexisting_failure_warns_but_still_checks_clips(self):
        def load_assets():
            raise ValueError("Off-palette pixel: creatures.baby-idle-a")
        fake = types.SimpleNamespace(SOURCE=None, load_assets=load_assets,
                                     check_creature_clips=creatures_coverage_check)
        manifest = json.loads(self.manifest_path.read_text())
        warning = creatures.validate(manifest, manifest, self.source, fake)
        self.assertIn("Off-palette pixel", warning)
        missing = json.loads(json.dumps(manifest))
        missing["clips"].pop()
        with self.assertRaises(creatures.ClipError):
            creatures.validate(missing, manifest, self.source, fake)

    def test_named_and_inline_palettes_resolve(self):
        manifest = json.loads(self.manifest_path.read_text())
        details = creatures.asset_details(manifest)
        self.assertEqual(details["creatures.axolotl-idle-1"]["palette"], manifest["palettes"]["axolotl"])
        self.assertEqual(details["creatures.axolotl-idle-1"]["palette_name"], "axolotl")
        self.assertEqual(details["creatures.baby-idle-a"]["palette"], manifest["palette"])
        self.assertEqual(details["backgrounds.home"]["palette_name"], "inline")

    def test_form_names_come_from_pets_art_when_present(self):
        manifest = json.loads(self.manifest_path.read_text())
        pets = Path(self.scratch.name) / "pets.json"
        pets.write_text(json.dumps({"forms": [{"id": 0, "name": "MINT", "portrait": 1001}]}))
        self.assertEqual([f["name"] for f in creatures.creature_forms(manifest, pets)], [None, None, None])
        pets.write_text(json.dumps({"forms": [{"id": 2, "name": "AXOLOTL", "portrait": 1101, "art": "axolotl"}]}))
        named = {f["art"]: f for f in creatures.creature_forms(manifest, pets)}
        self.assertEqual((named["axolotl"]["name"], named["axolotl"]["id"]), ("AXOLOTL", 2))
        self.assertEqual(creatures.creature_forms(manifest, pets.with_name("missing.json"))[0]["art"], "baby")


def creatures_coverage_check(manifest):
    forms = {a["form"] for a in manifest["assets"] if a["kind"] == "creatures"}
    keys = {c["key"] for c in manifest["clips"]}
    if keys != {f"{f}.{p}" for f in forms for p in manifest["creature_poses"]}:
        raise ValueError("Creature clip coverage mismatch")


if __name__ == "__main__":
    unittest.main()

# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Jelli Art creature edits: clips (build_slice decides) and creatures.json (creature_data decides).

Run: ./scripts/uv run --python 3.12 tools/jelli-art/test_creatures.py
"""
import json
import shutil
import sys
import tempfile
import types
import unittest
from unittest import mock
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import creatures  # noqa: E402
import jelli_art  # noqa: E402
import profiles  # noqa: E402


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
        missing["clips"] = [c for c in missing["clips"] if c["key"] != "axolotl.happy"]  # A base pose.
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


class CreatureProfileTest(unittest.TestCase):
    """content/creatures.json saves: creature_data.load decides, and nothing else is written."""

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.content = Path(self.scratch.name) / "content"
        self.content.mkdir()
        for name in ("pets.json", "creatures.json"):
            shutil.copyfile(REPO / "content" / name, self.content / name)
        self.path = self.content / "creatures.json"
        self.saved = (jelli_art.CONTENT, jelli_art.PETS, jelli_art.CREATURE_DATA, jelli_art.CONTENT_WRITABLE, jelli_art.GIT)
        jelli_art.CONTENT, jelli_art.PETS, jelli_art.CREATURE_DATA = self.content, self.content / "pets.json", self.path
        jelli_art.CONTENT_WRITABLE, jelli_art.GIT = True, None
        self.pets = (self.content / "pets.json").read_text()

    def tearDown(self):
        (jelli_art.CONTENT, jelli_art.PETS, jelli_art.CREATURE_DATA, jelli_art.CONTENT_WRITABLE, jelli_art.GIT) = self.saved
        self.scratch.cleanup()

    def data(self):
        return json.loads(self.path.read_text())

    def save(self, data):
        return jelli_art.save_creature_data(data, profiles.digest(self.path))

    def test_valid_edit_writes_only_creatures_json(self):
        data = self.data()
        data["profiles"][2]["scale"] = 4
        data["behaviors"][1]["pose_rules"].insert(0, data["behaviors"][1]["pose_rules"].pop(5))
        data["behaviors"][1]["idle_beats"][0] = "idle-alt"
        data["behaviors"][1]["quiet_cycle"] = 0
        result = self.save(data)
        self.assertTrue(result["changed"])
        self.assertEqual(result["sha"], profiles.digest(self.path))
        self.assertEqual(self.path.read_text(), json.dumps(data, indent=2) + "\n")
        self.assertEqual((self.content / "pets.json").read_text(), self.pets)

    def test_rejections_leave_the_file_alone(self):
        original = self.path.read_text()
        mutations = [
            lambda d: d["profiles"][2].update(scale=8),  # the axolotl would overlap the heading
            lambda d: d["profiles"][2].update(icon_scale=3),  # a 144 px icon in a 96 px cell
            lambda d: d["behaviors"][0]["pose_rules"].append({"when": "flying", "pose": "happy"}),
            lambda d: d["behaviors"][0]["pose_rules"].append({"when": "eating", "pose": "happy"}),
            lambda d: d["behaviors"][0].update(idle_beats=["eating"]),
            lambda d: d["behaviors"][0].update(quiet_cycle=17),
            lambda d: d["profiles"][0].update(behavior="missing"),
            lambda d: d["profiles"].pop(),
            lambda d: d["behaviors"][0].pop("idle_beats"),
            lambda d: d.update(extra=True),
        ]
        for mutate in mutations:
            data = self.data()
            mutate(data)
            with self.subTest(data=data), self.assertRaises(jelli_art.StudioError):
                self.save(data)
        self.assertEqual(self.path.read_text(), original)

    def test_git_sync_commits_only_creatures_json(self):
        data = self.data()
        data["profiles"][1]["icon_scale"] = 1
        git = mock.Mock()
        git.commit.return_value = "abc1234"
        module = creatures.load_checkout_module(REPO, "creature_data")
        saved_repo = jelli_art.REPO
        jelli_art.REPO, jelli_art.GIT = Path(self.scratch.name), git  # the scratch dir stands in for the checkout
        try:
            with mock.patch.object(creatures, "load_checkout_module", return_value=module):
                result = self.save(data)
        finally:
            jelli_art.REPO = saved_repo
        self.assertEqual(result["commit"], "abc1234")
        paths, subject, _ = git.commit.call_args.args
        self.assertEqual(paths, [Path("content/creatures.json")])
        self.assertIn("Jelli Art", subject)

    def test_stale_base_and_trials_are_refused(self):
        data = self.data()
        data["profiles"][0]["portrait_scale"] = 3
        with self.assertRaises(jelli_art.StudioError):
            jelli_art.save_creature_data(data, "0000000000000000")
        jelli_art.CONTENT_WRITABLE = False
        with self.assertRaises(jelli_art.StudioError):
            self.save(data)
        self.assertFalse(jelli_art.creature_profiles()["creature_data_editable"])

    def test_page_limits_come_from_the_checkout_validator(self):
        module = creatures.load_checkout_module(REPO, "creature_data")
        data = jelli_art.creature_profiles()
        limits = data["creature_limits"]
        self.assertEqual(limits["conditions"], list(module.CREATURE_CONDITIONS))
        self.assertEqual(limits["idle_poses"], list(module.IDLE_POSES))
        self.assertEqual(limits["max_actor_height"], module.FLOOR_Y - module.HEADING_BOTTOM)
        self.assertEqual(limits["max_actor_width"], module.MAX_ACTOR_WIDTH)
        self.assertEqual(data["creature_data"], self.data())
        self.assertEqual(module.CREATURES, REPO / "content/creatures.json")  # restored after validation


def creatures_coverage_check(manifest):
    forms = {a["form"] for a in manifest["assets"] if a["kind"] == "creatures"}
    keys = {c["key"] for c in manifest["clips"]}
    base = {f"{f}.{p}" for f in forms for p in manifest["creature_poses"]}
    optional = {f"{f}.{p['name']}" for f in forms for p in manifest.get("state_poses", [])}
    if not base <= keys <= base | optional:  # State poses may fall back to a base pose.
        raise ValueError("Creature clip coverage mismatch")


if __name__ == "__main__":
    unittest.main()

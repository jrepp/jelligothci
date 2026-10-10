# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Jelli Art creature edits: clips (build_slice decides), creatures.json (creature_data decides),
behaviors.json (cmake/JelliBehaviors.cmake decides) and the stimulus simulator (core/behavior.c).

Run: ./scripts/uv run --python 3.12 tools/jelli-art/test_creatures.py
"""
import json
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import behaviors  # noqa: E402
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

    def test_palette_ramps_are_validated(self):
        manifest = json.loads(self.manifest_path.read_text())
        validator = creatures.load_validator(REPO)
        validator.check_palette_ramps(manifest)  # the shipped ramps and names pass
        data = creatures.creature_data(manifest, REPO / "content/pets.json")
        self.assertEqual(data["palette_names"]["axolotl"]["#ffcbdd"], "body")
        ramp = lambda *colours, name="x": [{"name": name, "colours": list(colours)}]  # noqa: E731
        bad = {
            "ramp colour outside its palette": ("palette_ramps", "shared", ramp("#cff5cf", "#123456")),
            "one-colour ramp": ("palette_ramps", "shared", ramp("#cff5cf")),
            "repeated colour": ("palette_ramps", "shared", ramp("#cff5cf", "#cff5cf")),
            "duplicate ramp names": ("palette_ramps", "shared", ramp("#cff5cf", "#85e4b6") * 2),
            "unknown palette": ("palette_ramps", "nope", ramp("#cff5cf", "#85e4b6")),
            "uppercase colour": ("palette_ramps", "shared", ramp("#CFF5CF", "#85e4b6")),
            "deep to light": ("palette_ramps", "shared", ramp("#85e4b6", "#cff5cf")),
            "reserved row name": ("palette_ramps", "shared", ramp("#cff5cf", "#85e4b6", name="other")),
            "named colour outside its palette": ("palette_names", "axolotl", {"#123456": "body"}),
            "names as a list": ("palette_names", "axolotl", ["body"]),
            "duplicate colour names": ("palette_names", "axolotl", {"#ffcbdd": "body", "#ffadca": "body"}),
        }
        for why, (field, key, value) in bad.items():
            broken = json.loads(json.dumps(manifest))
            broken[field][key] = value
            with self.subTest(why), self.assertRaises(ValueError):
                validator.check_palette_ramps(broken)

    def test_palette_slot_change_moves_the_ramp_colour_and_name(self):
        manifest = json.loads(self.manifest_path.read_text())
        old = manifest["palette"][5]  # mint, in the mint jelly ramp
        jelli_art.set_palette_slot(5, "#80e0b0", base=old)
        after = json.loads(self.manifest_path.read_text())
        mint = next(r for r in after["palette_ramps"]["shared"] if r["name"] == "mint jelly")
        self.assertIn("#80e0b0", mint["colours"])
        self.assertNotIn(old, mint["colours"])
        self.assertEqual(after["palette_names"]["shared"]["#80e0b0"], "mint")
        self.assertNotIn(old, after["palette_names"]["shared"])
        creatures.load_validator(REPO).check_palette_ramps(after)

    def test_palette_slot_change_that_breaks_a_ramp_is_refused(self):
        original = self.manifest_path.read_text()
        old = json.loads(original)["palette"][5]
        with self.assertRaises(jelli_art.StudioError):
            jelli_art.set_palette_slot(5, "#f0fff0", base=old)  # lighter than foam, above it in the ramp
        self.assertEqual(self.manifest_path.read_text(), original)

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


class StatePoseClipTest(unittest.TestCase):
    """State pose clips are optional: the studio may add one or remove it so the fallback plays."""

    def setUp(self):
        self.manifest = json.loads((REPO / "assets/slice/assets.json").read_text())

    def test_remove_and_create_state_pose_clips(self):
        manifest = json.loads(json.dumps(self.manifest))
        self.assertEqual(creatures.apply_edits(manifest, [{"key": "axolotl.study", "remove": True}]), ["axolotl.study"])
        self.assertNotIn("axolotl.study", {c["key"] for c in manifest["clips"]})
        fallback = next(c for c in manifest["clips"] if c["key"] == "baby.content")
        edit = {"key": "baby.study", "frames": list(fallback["frames"]), "durations_ms": [300], "loop": True}
        self.assertEqual(creatures.apply_edits(manifest, [edit]), ["baby.study"])
        made = next(c for c in manifest["clips"] if c["key"] == "baby.study")
        ids = [a["id"] for a in manifest["assets"]] + [c["id"] for c in manifest["clips"]]
        self.assertEqual(ids.count(made["id"]), 1)
        keys = [c["key"] for c in manifest["clips"]]
        self.assertEqual(keys.index("baby.study"), max(i for i, k in enumerate(keys) if k.startswith("baby.")))
        validator = creatures.load_validator(REPO)
        validator.check_creature_clips(manifest)

    def test_base_poses_and_unknown_poses_are_refused(self):
        for edit in ({"key": "axolotl.happy", "remove": True},
                     {"key": "axolotl.dance", "frames": ["creatures.axolotl-idle-1"], "durations_ms": [90], "loop": True}):
            with self.subTest(edit=edit), self.assertRaises(creatures.ClipError):
                creatures.apply_edits(json.loads(json.dumps(self.manifest)), [edit])


class BehaviourContentTest(unittest.TestCase):
    """content/behaviors.json: cmake/JelliBehaviors.cmake validates, looks stay in step, both save together."""

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.content = Path(self.scratch.name) / "content"
        shutil.copytree(REPO / "content", self.content)
        self.saved = (jelli_art.CONTENT, jelli_art.PETS, jelli_art.CREATURE_DATA, jelli_art.CONTENT_WRITABLE, jelli_art.GIT)
        jelli_art.CONTENT, jelli_art.PETS = self.content, self.content / "pets.json"
        jelli_art.CREATURE_DATA, jelli_art.CONTENT_WRITABLE, jelli_art.GIT = self.content / "creatures.json", True, None
        self.files = {name: self.content / f"{name}.json" for name in ("behaviors", "creatures")}

    def tearDown(self):
        (jelli_art.CONTENT, jelli_art.PETS, jelli_art.CREATURE_DATA, jelli_art.CONTENT_WRITABLE, jelli_art.GIT) = self.saved
        self.scratch.cleanup()

    def doc(self, name):
        return json.loads(self.files[name].read_text())

    def save(self, **docs):
        return jelli_art.save_content(docs, {name: profiles.digest(self.files[name]) for name in docs})

    def snapshot(self):
        return {name: path.read_text() for name, path in self.files.items()}

    def test_vocabulary_follows_the_engine(self):
        vocab = behaviors.vocabulary(REPO, self.content)
        cmake = (REPO / "cmake/JelliBehaviors.cmake").read_text()
        self.assertIn(" ".join(vocab["lists"]["stimuli"][:3]), cmake)
        self.assertEqual(list(vocab["numbers"]["stimuli"].values()), list(range(len(vocab["lists"]["stimuli"]))))
        self.assertEqual(vocab["missing_numbers"], [])
        self.assertEqual(vocab["numbers"]["activity_code"], {"moment": 0, "health": 32, "care": 64})
        self.assertEqual(vocab["moments"], [m["name"] for m in json.loads((self.content / "activities.json").read_text())["moments"]])
        self.assertIn("AXOLOTL", [f["name"] for f in vocab["forms"]])

    def test_valid_behaviour_edit_writes_only_behaviors_json(self):
        creatures_text = self.files["creatures"].read_text()
        data = self.doc("behaviors")
        data["states"][0]["cooldown_s"] = 45
        data["repertoires"][0]["reactions"][3]["weight"] = 5
        result = self.save(behaviors=data)
        self.assertEqual(result["changed"], ["behaviors"])
        self.assertEqual(self.files["behaviors"].read_text(), json.dumps(data, indent=2) + "\n")
        self.assertEqual(self.files["creatures"].read_text(), creatures_text)

    def test_renaming_a_state_needs_both_files(self):
        before = self.snapshot()
        data, looks = self.doc("behaviors"), self.doc("creatures")
        data["states"][2]["name"] = "reading_hard"
        for rep in data["repertoires"]:
            for reaction in rep["reactions"]:
                if reaction["state"] == "studying":
                    reaction["state"] = "reading_hard"
        with self.assertRaises(jelli_art.StudioError):  # creatures.json still describes "studying"
            self.save(behaviors=data)
        self.assertEqual(self.snapshot(), before)
        for look in looks["state_presentation"]:
            if look["state"] == "studying":
                look["state"] = "reading_hard"
        result = self.save(behaviors=data, creatures=looks)
        self.assertEqual(result["changed"], ["behaviors", "creatures"])
        self.assertEqual(self.doc("behaviors"), data)
        self.assertEqual(self.doc("creatures"), looks)

    def test_engine_rejections_leave_files_alone(self):
        before = self.snapshot()
        mutations = [
            ("behaviors", lambda d: d["states"][0]["ends_on"].append("flying")),
            ("behaviors", lambda d: d["repertoires"][0]["reactions"][0].update(weight=0)),
            ("behaviors", lambda d: d["repertoires"][0]["reactions"][0].update(state="dancing")),
            ("behaviors", lambda d: d["repertoires"].append(dict(d["repertoires"][0], name="twin"))),
            ("behaviors", lambda d: d["states"][0].update(duration_s=[20, 10])),
            ("behaviors", lambda d: d["states"][4]["request"].update(command="sing")),
            ("creatures", lambda d: d["state_presentation"][0].update(caption="lower")),
            ("creatures", lambda d: d["state_presentation"][0].update(pose="dancing")),
            ("creatures", lambda d: d["state_presentation"][0].update(effect="effects.missing")),
            ("creatures", lambda d: d["state_presentation"].pop()),
        ]
        for name, mutate in mutations:
            data = self.doc(name)
            mutate(data)
            with self.subTest(name=name, data=data), self.assertRaises(jelli_art.StudioError):
                self.save(**{name: data})
        self.assertEqual(self.snapshot(), before)

    def test_stale_saves_and_missing_cmake_are_refused(self):
        data = self.doc("behaviors")
        data["need_low"] = 300
        with self.assertRaises(jelli_art.StudioError):
            jelli_art.save_content({"behaviors": data}, {"behaviors": "0000000000000000"})
        with mock.patch.object(behaviors.shutil, "which", return_value=None):
            with self.assertRaises(behaviors.BehaviorError):
                behaviors.validate(data, REPO, self.content)
            self.assertFalse(jelli_art.behaviour_data()["behavior_editable"])
        self.assertTrue(jelli_art.behaviour_data()["behavior_editable"])

    def test_simulator_matches_recorded_engine_outcomes(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed")
        result = subprocess.run([node, str(HERE / "testdata/replay_engine_cases.js"), str(HERE / "simulator.js"),
                                 str(HERE / "testdata/behavior_engine_cases.json")], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)


def creatures_coverage_check(manifest):
    forms = {a["form"] for a in manifest["assets"] if a["kind"] == "creatures"}
    keys = {c["key"] for c in manifest["clips"]}
    base = {f"{f}.{p}" for f in forms for p in manifest["creature_poses"]}
    optional = {f"{f}.{p['name']}" for f in forms for p in manifest.get("state_poses", [])}
    if not base <= keys <= base | optional:  # State poses may fall back to a base pose.
        raise ValueError("Creature clip coverage mismatch")


if __name__ == "__main__":
    unittest.main()

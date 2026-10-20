# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Creation integration: real checkout generators and transactional failure recovery."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import creation
import animation
import creatures
import storage


class CreationTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        self.source, self.content = self.root / "slice", self.root / "content"
        shutil.copytree(REPO / "assets/slice", self.source)
        shutil.copytree(REPO / "content", self.content)

    def tearDown(self):
        self.scratch.cleanup()

    def bases(self):
        return creation.data(REPO, self.source, self.content)["bases"]

    def create(self, request, bases=None):
        return creation.create(REPO, self.source, self.content, request, bases or self.bases())

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}

    def test_icon_prop_prize_and_background_save_reload(self):
        original = creation.read(self.source / "assets.json")
        for kind, size in (("icons", 16), ("props", 24), ("prizes", 32), ("backgrounds", 64)):
            with self.subTest(kind=kind):
                request = {"type": "asset", "kind": kind, "key": "test-" + kind, "name": "TEST ART", "size": size,
                           "pivot": [size // 2, size - 1]}
                if kind == "backgrounds":
                    request["location"] = {"key": "test_place", "name": "TEST PLACE", "outdoors": True}
                result = self.create(request)
                self.assertEqual(result["key"], kind + ".test-" + kind)
                manifest = creation.read(self.source / "assets.json")
                creation.validate(REPO, self.source, self.content, manifest,
                                  {n: creation.read(self.content / f"{n}.json") for n in creation.DOCS if (self.content / f"{n}.json").exists()}, {})
                self.assertEqual(manifest["assets"][:len(original["assets"])], original["assets"])
        places = creation.read(self.content / "locations.json")["locations"]
        self.assertEqual(places[-1]["key"], "test_place")

    def test_validation_and_stale_leave_every_file_intact(self):
        before = self.snapshot()
        valid = {"type": "asset", "kind": "icons", "key": "my-icon", "name": "MY ICON", "size": 16}
        for change in ({"size": 32}, {"palette": ["#xx0000"]}, {"pivot": [-1, 2]},
                       {"location": {"key": "bad", "name": "BAD", "outdoors": False}}):
            with self.assertRaises(creation.CreationError):
                self.create({**valid, **change})
            self.assertEqual(before, self.snapshot())
        stale = self.bases();stale["manifest"] = "old"
        with self.assertRaises(creation.StaleCreationError):
            self.create(valid, stale)
        self.assertEqual(before, self.snapshot())

    def test_wrong_location_or_background_reference_fails_real_generator(self):
        before = self.snapshot()
        with self.assertRaises(creation.CreationError):
            self.create({"type": "asset", "kind": "backgrounds", "key": "test-room", "name": "TEST ROOM", "size": 64,
                         "location": {"key": "home", "name": "DUPLICATE", "outdoors": False}})
        self.assertEqual(before, self.snapshot())

    def test_mid_commit_failure_rolls_back_new_files_and_existing_json(self):
        before = self.snapshot()
        original = storage.os.replace
        calls = 0
        def fail_once(src, dst):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("injected rename failure")
            return original(src, dst)
        with mock.patch.object(storage.os, "replace", side_effect=fail_once):
            with self.assertRaises(OSError):
                self.create({"type": "asset", "kind": "icons", "key": "rollback", "name": "ROLLBACK", "size": 16})
        # Rollback also cleans staged temporary siblings.
        self.assertEqual(before, self.snapshot())

    def test_collectible_registration_is_explicit_and_survives_reload(self):
        request = {"type": "asset", "kind": "prizes", "key": "new-keepsake", "name": "NEW KEEPSAKE", "size": 32}
        self.create(request)
        registry = creation.read(self.content / "prizes.json")["prizes"]
        self.assertNotIn("new-keepsake", {p["key"] for p in registry})
        result = self.create({**request, "key": "new-collectible", "collectible": True})
        registry = creation.read(self.content / "prizes.json")["prizes"]
        self.assertEqual(registry[-1]["key"], "new-collectible")
        asset = next(a for a in creation.read(self.source / "assets.json")["assets"] if a["key"] == result["key"])
        self.assertEqual(registry[-1]["asset"], asset["id"])
        self.assertEqual(registry[-1]["id"], 10)

    def test_unregistered_existing_png_is_never_overwritten(self):
        from PIL import Image
        path = self.source / "icons/unregistered.png"
        Image.new("RGBA", (16, 16), "white").save(path)
        before = self.snapshot()
        with self.assertRaisesRegex(creation.CreationError, "Artwork file already exists"):
            self.create({"type": "asset", "kind": "icons", "key": "unregistered", "name": "EXISTING", "size": 16})
        self.assertEqual(before, self.snapshot())

    def test_external_change_during_validation_is_not_overwritten(self):
        original_validate = creation.validate
        path = self.content / "activities.json"
        expected = path.read_bytes() + b"\n"
        def change_after_validation(*args):
            original_validate(*args)
            path.write_bytes(expected)
        with mock.patch.object(creation, "validate", side_effect=change_after_validation):
            with self.assertRaisesRegex(creation.StaleCreationError, "during validation"):
                self.create({"type": "asset", "kind": "icons", "key": "race", "name": "RACE", "size": 16})
        self.assertEqual(path.read_bytes(), expected)
        self.assertFalse((self.source / "icons/race.png").exists())

    def reserve_frame(self, key="creatures.retired-example"):
        manifest = creation.read(self.source / "assets.json")
        used = {a["id"] for a in manifest["assets"]} | {c["id"] for c in manifest["clips"]}
        ident = next(i for i in range(1001, 2001) if i not in used)
        path = self.source / animation.LEDGER
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(storage.json_bytes({"frames": [{"id": ident, "key": key, "retired": True}]}))
        return ident, path

    def test_pet_creation_skips_retired_frame_ids(self):
        ident, path = self.reserve_frame()
        ledger_before = path.read_bytes()
        self.create(self.pet_request())
        assets = creation.read(self.source / "assets.json")["assets"]
        self.assertNotIn(ident, {a["id"] for a in assets})
        self.assertEqual(path.read_bytes(), ledger_before)

    def test_retired_frame_key_cannot_be_reused(self):
        self.reserve_frame("creatures.new-jelli-idle-a")
        before = self.snapshot()
        with self.assertRaisesRegex(creation.CreationError, "reserved by frame history"):
            self.create(self.pet_request())
        self.assertEqual(before, self.snapshot())

    def test_reservation_change_during_validation_is_stale(self):
        _, path = self.reserve_frame()
        original_validate = creation.validate
        expected = path.read_bytes() + b"\n"
        def change_after_validation(*args):
            original_validate(*args)
            path.write_bytes(expected)
        with mock.patch.object(creation, "validate", side_effect=change_after_validation):
            with self.assertRaisesRegex(creation.StaleCreationError, "during validation"):
                self.create(self.pet_request())
        self.assertEqual(path.read_bytes(), expected)
        manifest = creation.read(self.source / "assets.json")
        self.assertFalse(any(a.get("form") == "new-jelli" for a in manifest["assets"]))
        self.assertFalse((self.source / "creatures/new-jelli-idle-a.png").exists())

    def test_png_source_change_is_stale(self):
        bases = self.bases()
        path = self.source / "icons/food.png"
        if not path.exists():
            path = next((self.source / "icons").glob("*.png"))
        path.write_bytes(path.read_bytes() + b"changed")
        before = self.snapshot()
        with self.assertRaises(creation.StaleCreationError):
            self.create({"type": "asset", "kind": "icons", "key": "stale", "name": "STALE", "size": 16}, bases)
        self.assertEqual(before, self.snapshot())

    def test_form_and_location_slot_exhaustion_preserves_files(self):
        pets = creation.read(self.content / "pets.json")
        pets["forms"] += [dict(pets["forms"][0]) for _ in range(8-len(pets["forms"]))]
        (self.content / "pets.json").write_bytes(storage.json_bytes(pets))
        before = self.snapshot()
        with self.assertRaisesRegex(creation.CreationError, "No available saved form"):
            self.create(self.pet_request())
        self.assertEqual(before, self.snapshot())
        locations = creation.read(self.content / "locations.json")
        locations["locations"] += [dict(locations["locations"][0]) for _ in range(8-len(locations["locations"]))]
        (self.content / "locations.json").write_bytes(storage.json_bytes(locations))
        before = self.snapshot()
        with self.assertRaisesRegex(creation.CreationError, "No available location"):
            self.create({"type": "asset", "kind": "backgrounds", "key": "full", "name": "FULL", "size": 64,
                         "location": {"key": "full", "name": "FULL", "outdoors": False}})
        self.assertEqual(before, self.snapshot())

    def test_id_exhaustion_preserves_existing_ids(self):
        used = set(range(2001, 3001))
        with self.assertRaises(creation.CreationError):
            creation.allocate(used, 2001, 3000, "icon")
        self.assertEqual(used, set(range(2001, 3001)))
        self.assertEqual(creation.allocate(set(range(2001, 3000)), 2001, 3000, "icon"), 3000)

    def pet_request(self, key="new-jelli", size=32):
        return {"type": "pet", "key": key, "name": "NEW JELLI", "template_form": "baby", "size": size,
                "evolution": "single-form", "gameplay_policy": "copy-template", "behavior": "jelly",
                "scale": 3, "icon_scale": 1, "portrait_scale": 3, "unlock_prize": 1, "hint": "CATCH BUTTERFLY"}

    def test_pet_templates_geometry_and_reload(self):
        old = creation.read(self.content / "pets.json")
        for size in (32, 48):
            request = self.pet_request("new-jelli-" + str(size), size)
            request["name"] = "NEW JELLI " + str(size)
            result = self.create(request)
            manifest = creation.read(self.source / "assets.json")
            frames = [a for a in manifest["assets"] if a.get("form") == request["key"]]
            self.assertTrue(frames)
            self.assertTrue(all(a["width"] == size and a["height"] == size for a in frames))
            clips = [c for c in manifest["clips"] if c["key"].startswith(request["key"] + ".")]
            self.assertEqual(len(clips), len([c for c in manifest["clips"] if c["key"].startswith("baby.")]))
            self.assertIn(result["key"], {a["key"] for a in frames})
            # Paint and reload the new frame; the shared art validator accepts it.
            from PIL import Image
            path = self.source / frames[0]["path"]
            image = Image.open(path).convert("RGBA")
            x, y = frames[0]["bounds"][:2]
            image.putpixel((x,y), tuple(bytes.fromhex(manifest["palette"][0][1:])) + (255,))
            image.save(path)
            frames[0]["bounds"] = list(image.getchannel("A").getbbox())
            (self.source / "assets.json").write_bytes(storage.json_bytes(manifest))
            self.assertIsNone(creatures._run_load_assets(creatures.load_validator(REPO), manifest, self.source))
        pets = creation.read(self.content / "pets.json")
        self.assertEqual(pets["entries"][:len(old["entries"])], old["entries"])
        self.assertEqual(pets["forms"][:len(old["forms"])], old["forms"])

    def test_pet_requires_explicit_policy_and_valid_template(self):
        before = self.snapshot()
        for change in ({"template_form": "missing"}, {"evolution": "grow"}, {"gameplay_policy": ""},
                       {"size": 40}, {"behavior": "missing"}, {"unlock_prize": 99}, {"hint": None}, {"hint": []}):
            with self.assertRaises(creation.CreationError):
                self.create({**self.pet_request(), **change})
            self.assertEqual(before, self.snapshot())


if __name__ == "__main__":
    unittest.main()

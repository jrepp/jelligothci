# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Scene document validation and save isolation, including optimistic concurrency."""
import copy
import json
from pathlib import Path
import tempfile
import shutil
import subprocess
import unittest
from unittest.mock import patch

import jelli_art as studio
import scenes
import scene_data
import embed_slice

ROOT = Path(__file__).resolve().parents[2]


class SceneTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((ROOT / "assets/slice/assets.json").read_text())
        self.doc = json.loads((ROOT / "content/scenes.json").read_text())

    def test_seed_scenes_are_valid_and_share_artwork(self):
        scenes.validate(self.doc, self.manifest)
        self.assertEqual(self.doc["scenes"][0]["layers"][0]["asset"],
                         self.doc["scenes"][1]["layers"][0]["asset"])

    def test_generated_asset_translation_unit_ends_with_newline(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "jelli_assets.c"
            embed_slice.generate(output)
            with output.open("rb") as generated:
                generated.seek(-1, 2)
                self.assertEqual(generated.read(), b"\n")

    def test_compiler_resolves_ids_and_deduplicates_resources(self):
        emitted = scene_data.emit(self.doc, self.manifest)
        self.assertIn('const JelliSceneDefinition', emitted)
        self.assertIn('.key = "market-street"', emitted)
        first = scene_data.resources(self.doc, self.manifest)["market-street"]
        self.doc["scenes"][0]["layers"].append(copy.deepcopy(self.doc["scenes"][0]["layers"][0]))
        repeated = scene_data.resources(self.doc, self.manifest)["market-street"]
        self.assertEqual(first, repeated)

    def test_reject_invalid_and_unbounded_data(self):
        changes = (("asset", "creatures.baby-idle-a"), ("x", True), ("x", 257),
                   ("tilt_x", -1), ("tilt_y", 17), ("travel", 101),
                   ("travel", float("nan")), ("visible", 1), ("repeat_x", "true"),
                   ("asset", []))
        for key, value in changes:
            with self.subTest(key=key, value=value):
                doc = copy.deepcopy(self.doc)
                doc["scenes"][0]["layers"][0][key] = value
                with self.assertRaises(ValueError):
                    scenes.validate(doc, self.manifest)
        for mutation in (
                lambda d: d["scenes"].append(copy.deepcopy(d["scenes"][0])),
                lambda d: d["scenes"][0].update(layers=[]),
                lambda d: d["scenes"][0].update(layers=d["scenes"][0]["layers"]*4),
                lambda d: d["scenes"][0]["layers"][0].update(travel=1, repeat_x=False),
                lambda d: d.update(version=True),
                lambda d: d.update(unrecognized="discard me")):
            doc = copy.deepcopy(self.doc)
            mutation(doc)
            with self.assertRaises(ValueError):
                scenes.validate(doc, self.manifest)

    def test_names_and_variation_validation(self):
        invalid = ({"name":" "}, {"name":7}, {"random":{}},
                   {"random":{"seed":0,"x":17,"y":0,"travel":0,"chance":100}},
                   {"random":{"seed":True,"x":0,"y":0,"travel":0,"chance":100}},
                   {"random":{"seed":0,"x":0,"y":0,"travel":0,"chance":101}},
                   {"random":{"seed":0,"x":0,"y":0,"travel":1,"chance":100},"repeat_x":False})
        for update in invalid:
            doc=copy.deepcopy(self.doc)
            doc["scenes"][0]["layers"][0].update(update)
            with self.subTest(update=update), self.assertRaises(ValueError):
                scene_data.validate(doc,self.manifest)
        legacy=copy.deepcopy(self.doc)
        for scene in legacy["scenes"]:
            scene.pop("seed",None)
            for layer in scene["layers"]:
                layer.pop("name",None)
                layer.pop("random",None)
        self.assertEqual(scene_data.emit(legacy,self.manifest),scene_data.emit(self.doc,self.manifest))
        self.doc["scenes"][0]["seed"]=65536
        with self.assertRaises(ValueError):
            scene_data.validate(self.doc,self.manifest)

    def test_seeded_variation_bounds_and_stable_identity(self):
        scene=self.doc["scenes"][0]
        layer=scene["layers"][0]
        layer.update(x=256,y=-256,travel=99)
        layer["random"]={"seed":12,"x":16,"y":16,"travel":100,"chance":0}
        for seed in range(256):
            scene["seed"]=seed
            value=scene_data.resolve_layer(scene,layer)
            self.assertTrue(240 <= value["x"] <= 256)
            self.assertTrue(-256 <= value["y"] <= -240)
            self.assertTrue(0 <= value["travel"] <= 100)
            self.assertFalse(value["visible"])
        layer["random"]["chance"]=100
        before=scene_data.resolve_layer(scene,layer)
        self.assertTrue(before["visible"])
        layer["name"]="Renamed"
        scene["layers"].reverse()
        after=scene_data.resolve_layer(scene,layer)
        for key in ("x","y","travel","visible"):
            self.assertEqual(before[key],after[key])
        layer["visible"]=False
        self.assertFalse(scene_data.resolve_layer(scene,layer)["visible"])

    @unittest.skipUnless(shutil.which("node"), "Node required for browser/compiler parity")
    def test_browser_compiler_variation_parity(self):
        scene=copy.deepcopy(self.doc["scenes"][0])
        layer=scene["layers"][0]
        layer.update(x=-250,y=250,travel=50)
        layer["random"]={"seed":65535,"x":16,"y":16,"travel":70,"chance":37}
        cases=[]
        for seed in (0,1,42,32768,65535):
            scene["seed"]=seed
            cases.append(copy.deepcopy([scene,layer]))
        script="""const fs=require('fs'),vm=require('vm'); const ctx={};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(process.argv[1],'utf8'),ctx);
const cases=JSON.parse(fs.readFileSync(0,'utf8'));
process.stdout.write(JSON.stringify(cases.map(([s,l])=>ctx.resolveSceneLayer(s,l))));"""
        result=subprocess.run([shutil.which("node"),"-e",script,
                               str(ROOT/"tools/jelli-art/scene_variation.js")],
                              input=json.dumps(cases),text=True,capture_output=True,check=True)
        self.assertEqual(json.loads(result.stdout),[scene_data.resolve_layer(s,l) for s,l in cases])

    def test_save_stale_readonly_and_invalid_do_not_change_disk(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(studio, "CONTENT", Path(temp)), \
                patch.object(studio, "read_manifest", return_value=self.manifest), \
                patch.object(studio, "record", return_value={}):
            path = Path(temp) / "scenes.json"
            result = studio.save_scenes({"data":self.doc, "base":None})
            before = path.read_bytes()
            with self.assertRaises(studio.StaleError):
                studio.save_scenes({"data":self.doc, "base":None})
            with self.assertRaises(studio.StudioError):
                studio.save_scenes({"data":self.doc})
            bad = copy.deepcopy(self.doc)
            bad["scenes"][0]["layers"][0]["asset"] = "../escape"
            with self.assertRaises(ValueError):
                studio.save_scenes({"data":bad, "base":result["base"]})
            with patch.object(studio, "CONTENT_WRITABLE", False):
                with self.assertRaises(studio.StudioError):
                    studio.save_scenes({"data":self.doc, "base":result["base"]})
            self.assertEqual(before, path.read_bytes())
            self.assertEqual(result["base"], studio.scene_data()["base"])

    def test_read_hash_describes_exact_document_snapshot(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(studio, "CONTENT", Path(temp)), \
                patch.object(studio, "read_manifest", return_value=self.manifest):
            path = Path(temp) / "scenes.json"
            original = json.dumps(self.doc).encode()
            path.write_bytes(original)
            newer = copy.deepcopy(self.doc)
            newer["scenes"][0]["name"] = "External edit"
            read = Path.read_bytes

            def replace_after_read(target):
                raw = read(target)
                if target == path:
                    path.write_text(json.dumps(newer))
                return raw

            with patch.object(Path, "read_bytes", replace_after_read):
                result = studio.scene_data()
            self.assertEqual(result["data"], self.doc)
            self.assertEqual(result["base"], studio.storage.digest_bytes(original))
            self.assertNotEqual(result["base"], studio.storage.digest(path))


if __name__ == "__main__":
    unittest.main()

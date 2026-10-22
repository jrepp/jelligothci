# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Jelli Art Test in game: scenario validation, build output, the endpoint, and a real render.

The end-to-end tests build tools/game-preview/preview.c against the real engine
from scratch copies of assets/slice and content/; they skip without cmake and a
C compiler. Run: ./scripts/uv run --script tools/jelli-art/test_game_preview.py
"""
import base64
import io
import json
import shutil
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path
from unittest import mock

from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import game_preview as gp  # noqa: E402
import creation  # noqa: E402
import jelli_art  # noqa: E402

CAT = gp.catalogue(REPO, REPO / "content")


def png(url):
    return Image.open(io.BytesIO(base64.b64decode(url.split(",", 1)[1])))


class ScenarioTest(unittest.TestCase):
    def fields(self, raw):
        with self.assertRaises(gp.ScenarioError) as caught:
            gp.parse_scenario(raw, CAT)
        return {e["field"] for e in caught.exception.errors}

    def test_catalogue_follows_engine_and_content(self):
        pages = {p["name"]: p for p in CAT["pages"]}
        self.assertEqual(CAT["pages"][0]["name"], "home")
        self.assertTrue(pages["care"]["ring"])
        self.assertFalse(pages["collection"]["ring"])
        self.assertFalse(pages["brush"]["ring"])
        bubble = next(e for e in CAT["entries"] if e["name"] == "BUBBLE")
        self.assertEqual([f["name"] for f in bubble["forms"]], ["BABY AXO", "AXOLOTL"])
        self.assertIn("asking_potty", CAT["states"])

    def test_defaults_render_the_starting_pet(self):
        argv, summary = gp.parse_scenario({}, CAT)
        args = dict(zip(argv[::2], argv[1::2]))
        self.assertEqual((args["--entry"], args["--form"], args["--page"]), ("1", "0", "0"))
        self.assertEqual((args["--behavior"], args["--moment"], args["--frames"]), ("0", "0", "12"))
        self.assertEqual(summary["pet"], "JELLI")

    def test_background_motion_validation(self):
        argv, summary = gp.parse_scenario({"scene":"market-street", "tilt_x":-100,
                                          "tilt_y":100, "travel_speed":-32}, CAT)
        args = dict(zip(argv[::2], argv[1::2]))
        self.assertEqual(args["--scene"], "market-street")
        self.assertEqual(args["--travel_speed"], "-32")
        self.assertEqual(summary["tilt_x"], -100)
        self.assertEqual(self.fields({"scene":"missing"}), {"scene"})
        self.assertEqual(self.fields({"tilt_x":101, "tilt_y":True, "travel_speed":33}),
                         {"tilt_x", "tilt_y", "travel_speed"})

    def test_names_map_to_engine_numbers(self):
        raw = {"pet": "bubble", "page": "care", "menu": True, "behavior": "asking_potty", "moment": "tea",
               "needs": {"satiety": 100}, "potty": 900, "mess": True, "minute": 1300, "frames": 3}
        argv, summary = gp.parse_scenario(raw, CAT)
        args = dict(zip(argv[::2], argv[1::2]))
        self.assertEqual(args["--entry"], "3")
        self.assertEqual(args["--form"], "3")  # BUBBLE starts as the baby axolotl
        self.assertEqual(args["--behavior"], str(CAT["states"].index("asking_potty") + 1))
        self.assertEqual(args["--moment"], str(CAT["moments"].index("TEA") + 1))
        self.assertEqual(args["--needs"], "100,700,700,700,700")
        self.assertEqual((args["--menu"], args["--mess"], args["--asleep"]), ("1", "1", "0"))
        self.assertEqual(summary["form"], "BABY AXO")
        argv, summary = gp.parse_scenario({**raw, "form": "AXOLOTL"}, CAT)
        self.assertEqual((dict(zip(argv[::2], argv[1::2]))["--form"], summary["form"]), ("2", "AXOLOTL"))

    def test_rejections_name_the_field(self):
        self.assertEqual(self.fields({"pet": "NOPE"}), {"pet"})
        self.assertEqual(self.fields({"pet": "BUBBLE", "form": "MINT"}), {"form"})
        self.assertEqual(self.fields({"needs": {"satiety": 1001, "hunger": 3}}), {"needs.satiety", "needs"})
        self.assertEqual(self.fields({"frames": gp.MAX_FRAMES + 1, "step_ms": 0}), {"frames", "step_ms"})
        self.assertEqual(self.fields({"frames": 2.5, "mess": "yes"}), {"frames", "mess"})
        self.assertEqual(self.fields({"potty": True}), {"potty"})
        self.assertEqual(self.fields({"asleep": True, "moment": "TEA"}), {"moment"})
        self.assertEqual(self.fields({"page": "collection", "menu": True}), {"menu"})
        self.assertEqual(self.fields({"behavior": "dancing"}), {"behavior"})
        self.assertEqual(self.fields([]), {"scenario"})

    def test_scene_and_completion_validation(self):
        for raw, field in (({"location": "missing"}, "location"),
                           ({"present_state": "owned"}, "present"),
                           ({"completion": "repeat"}, "moment"),
                           ({"completion": "invalid"}, "completion")):
            self.assertIn(field, self.fields(raw))
        argv, summary = gp.parse_scenario({"location": "garden"}, CAT)
        self.assertEqual(dict(zip(argv[::2], argv[1::2]))["--location"], "1")
        self.assertEqual(summary["location"], "garden")

    def test_alt_text_describes_the_scenario(self):
        _, summary = gp.parse_scenario({"pet": "BUBBLE", "form": "AXOLOTL", "behavior": "studying", "mess": True, "asleep": True,
                                        "minute": 61, "frames": 4, "step_ms": 250}, CAT)
        text = gp.alt_text(summary, 2)
        for part in ("frame 3 of 4 at 500 ms", "BUBBLE as AXOLOTL", "home page", "behaviour studying", "asleep",
                     "mess", "clock 01:01", "satiety 700"):
            self.assertIn(part, text)


class BuildPlumbingTest(unittest.TestCase):
    def test_build_messages_classify_the_failing_step(self):
        root = Path("/stage")
        cmake = "CMake Error at cmake/JelliBehaviors.cmake:30 (message):\n  State curious minimum duration (s) must be 1..3600, got '0'\nCall Stack (most recent call first):\n  x\n"
        self.assertEqual(gp.build_messages(cmake, root), ("content", ["State curious minimum duration (s) must be 1..3600, got '0'"]))
        art = "Traceback (most recent call last):\n  File x\nValueError: Missing base pose clips: ['axolotl.happy']\nFAILED: x\n"
        self.assertEqual(gp.build_messages(art, root), ("art", ["Missing base pose clips: ['axolotl.happy']"]))
        cc = "/stage/core/pet_draw.c:10:5: error: use of undeclared identifier 'x'\n"
        self.assertEqual(gp.build_messages(cc, root), ("compile", ["core/pet_draw.c:10:5: error: use of undeclared identifier 'x'"]))
        stage, messages = gp.build_messages("CMake Error: No CMAKE_C_COMPILER could be found.\n", root)
        self.assertEqual(stage, "tools")
        self.assertIn("No C compiler", messages[0])

    def test_sync_copies_only_changes_and_drops_stale_files(self):
        with tempfile.TemporaryDirectory() as scratch:
            src, dst = Path(scratch) / "src", Path(scratch) / "dst"
            (src / "a").mkdir(parents=True)
            (src / "a/one.json").write_text("1")
            (src / "a/__pycache__").mkdir()
            (src / "a/__pycache__/x.pyc").write_text("cache")
            self.assertTrue(gp.sync_tree(src, dst))
            self.assertFalse((dst / "a/__pycache__/x.pyc").exists())
            stamp = (dst / "a/one.json").stat().st_mtime_ns
            (dst / "a/__pycache__").mkdir()
            (dst / "a/__pycache__/y.pyc").write_text("generated by the build")
            self.assertFalse(gp.sync_tree(src, dst))
            self.assertEqual((dst / "a/one.json").stat().st_mtime_ns, stamp)
            (src / "a/one.json").unlink()
            (src / "two.json").write_text("2")
            self.assertTrue(gp.sync_tree(src, dst))
            self.assertEqual(sorted(p.name for p in dst.rglob("*.json")), ["two.json"])

    def test_run_bounded_stops_at_the_timeout(self):
        code, message = gp.run_bounded([sys.executable, "-c", "import time; time.sleep(30)"], None, 0.3)
        self.assertIsNone(code)
        self.assertIn("timed out", message)

    def test_missing_tools_are_reported_not_raised_from_status(self):
        service = gp.GamePreview(REPO, REPO / "assets/slice", REPO / "content", cache=tempfile.gettempdir())
        with mock.patch.object(gp.shutil, "which", return_value=None):
            status = service.status()
            self.assertFalse(status["available"])
            self.assertIn("cmake", status["reason"])
            with self.assertRaises(gp.PreviewUnavailable):
                service.ensure_built()

    def test_render_refuses_beyond_the_concurrency_bound(self):
        service = gp.GamePreview(REPO, REPO / "assets/slice", REPO / "content", cache=tempfile.gettempdir())
        for _ in range(gp.MAX_RENDERS):
            service.renders.acquire()
        with self.assertRaises(gp.PreviewUnavailable):
            service.render({})


class EndpointTest(unittest.TestCase):
    """The HTTP contract through jelli_art.Handler, with the build mocked."""

    def setUp(self):
        self.saved = gp.SERVICE
        self.service = gp.configure(REPO, REPO / "assets/slice", REPO / "content", cache=tempfile.gettempdir())
        self.server = jelli_art.StudioServer(("127.0.0.1", 0), jelli_art.Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/api/game-preview"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        gp.SERVICE = self.saved

    def post(self, body):
        request = urllib.request.Request(self.url, data=json.dumps(body).encode(), method="POST",
                                         headers={"Content-Type": "application/json"})  # Like the panel.
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                return response.status, json.loads(response.read())
        except urllib.error.HTTPError as error:
            return error.code, json.loads(error.read())

    def test_get_lists_the_scenario_vocabulary(self):
        with urllib.request.urlopen(self.url, timeout=10) as response:
            data = json.loads(response.read())
        self.assertIn("available", data)
        self.assertEqual(data["limits"]["frames"], gp.MAX_FRAMES)
        self.assertTrue(any(e["name"] == "BUBBLE" for e in data["entries"]))

    def test_bad_scenario_is_a_400_with_field_errors(self):
        status, data = self.post({"scenario": {"pet": "NOPE"}})
        self.assertEqual(status, 400)
        self.assertEqual(data["stage"], "scenario")
        self.assertEqual(data["errors"][0]["field"], "pet")

    def test_build_failure_is_reported_as_data(self):
        failure = gp.BuildFailed("content", ["State curious minimum duration (s) must be 1..3600, got '0'"], ["log"])
        with mock.patch.object(self.service, "ensure_built", side_effect=failure):
            status, data = self.post({"scenario": {}})
        self.assertEqual(status, 200)
        self.assertFalse(data["ok"])
        self.assertEqual(data["stage"], "content")
        self.assertIn("minimum duration", data["messages"][0])

    def test_unavailable_host_is_a_503(self):
        with mock.patch.object(self.service, "missing", return_value="No C compiler here."):
            status, data = self.post({"scenario": {}})
        self.assertEqual(status, 503)
        self.assertEqual(data["error"], "No C compiler here.")


@unittest.skipUnless(gp.GamePreview(REPO, REPO, REPO).missing() is None, "needs cmake and a C compiler")
class RealEngineTest(unittest.TestCase):
    """Builds the real engine from scratch copies; one build is shared by the class."""

    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="jelli-preview-test-")
        root = Path(cls.scratch.name)
        cls.source, cls.content = root / "slice", root / "content"
        shutil.copytree(REPO / "assets/slice", cls.source)
        shutil.copytree(REPO / "content", cls.content)
        cls.service = gp.GamePreview(REPO, cls.source, cls.content, cache=root / "cache")

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def test_1_renders_engine_frames_and_reports_state(self):
        result = self.service.render({"pet": "BUBBLE", "form": "AXOLOTL", "behavior": "studying", "mess": True, "potty": 800,
                                      "frames": 3, "step_ms": 300})
        self.assertTrue(result["ok"])
        self.assertEqual(len(result["frames"]), 3)
        self.assertEqual(png(result["frames"][0]).size, (466, 466))
        engine = result["engine"]
        self.assertEqual((engine["form_name"], engine["behavior"], engine["mess"], engine["potty"]),
                         ("AXOLOTL", "studying", True, 800))
        self.assertEqual(result["times_ms"], [0, 300, 600])
        again = self.service.render({"asleep": True, "minute": 1380})
        self.assertFalse(again["rebuilt"])
        self.assertTrue(again["engine"]["asleep"])
        self.assertEqual(again["engine"]["night"], 255)

    def test_newly_authored_pet_renders_with_new_entry_and_form(self):
        request = {"type": "pet", "key": "preview-pet", "name": "PREVIEW PET", "template_form": "baby",
                   "size": 32, "evolution": "single-form", "gameplay_policy": "copy-template",
                   "behavior": "jelly", "scale": 3, "icon_scale": 1, "portrait_scale": 3,
                   "unlock_prize": 1, "hint": "CATCH BUTTERFLY"}
        creation.create(REPO, self.source, self.content, request,
                        creation.data(REPO, self.source, self.content)["bases"])
        rendered = self.service.render({"pet": "PREVIEW PET", "form": "PREVIEW PET", "frames": 1})
        self.assertTrue(rendered["ok"])
        self.assertEqual(rendered["engine"]["entry"], 10)
        self.assertEqual(rendered["engine"]["form_name"], "PREVIEW PET")
        self.assertEqual(png(rendered["frames"][0]).size, (466, 466))

    def test_scene_and_present_state_reach_engine(self):
        home = self.service.render({"frames": 1})
        garden = self.service.render({"location": "garden", "frames": 1})
        self.assertEqual(garden["engine"]["location"], 1)
        self.assertEqual(garden["engine"]["rendered_location"], 1)
        self.assertNotEqual(png(home["frames"][0]).tobytes(), png(garden["frames"][0]).tobytes())
        offered = self.service.render({"present": "teddy-bear", "present_state": "offered", "frames": 1})
        owned = self.service.render({"present": "teddy-bear", "present_state": "owned", "frames": 1})
        self.assertEqual(offered["engine"]["offered"], 10)
        self.assertEqual(offered["engine"]["visible_offered"], 10)
        self.assertEqual(owned["engine"]["owned"], 1 << 9)
        self.assertEqual(owned["engine"]["offered"], 0)

    def test_effect_and_hardpoint_incremental_build(self):
        scenario = {"effect":"celebrate", "start_ms":1000, "frames":1, "minute":720}
        before = self.service.render(scenario)
        plain = self.service.render({**scenario, "effect":""})
        self.assertNotEqual(png(before["frames"][0]).tobytes(), png(plain["frames"][0]).tobytes())
        path = self.service.content / "effects.json"
        original = path.read_bytes()
        doc = json.loads(original)
        for point in doc["hardpoints"]:
            for art in point["positions"]:
                point["positions"][art] = [30, 0]
        try:
            path.write_text(json.dumps(doc))
            changed = self.service.render(scenario)
            self.assertNotEqual(png(before["frames"][0]).tobytes(), png(changed["frames"][0]).tobytes())
        finally:
            path.write_bytes(original)

    def test_layered_scene_tilt_travel_and_incremental_build(self):
        scenario = {"scene":"market-street", "frames":2, "step_ms":200, "minute":720}
        still = self.service.render(scenario)
        tilted = self.service.render({**scenario, "tilt_x":100})
        moving = self.service.render({**scenario, "travel_speed":32})
        # Sample background beside the actor/UI: pet animation cannot cause this difference.
        box = (50, 160, 120, 240)
        sample = lambda result, i: png(result["frames"][i]).crop(box).tobytes()
        self.assertEqual(sample(still, 0), sample(still, 1))
        self.assertNotEqual(sample(still, 0), sample(tilted, 0))
        self.assertNotEqual(sample(moving, 0), sample(moving, 1))
        path = self.service.content / "scenes.json"
        original = path.read_bytes()
        doc = json.loads(original)
        for layer in doc["scenes"][0]["layers"]:
            layer["random"] = {"seed":0,"x":0,"y":0,"travel":0,"chance":0}
        doc["scenes"][0]["fill"] = "#123456"
        try:
            path.write_text(json.dumps(doc))
            changed = self.service.render(scenario)
            self.assertNotEqual(sample(still, 0), sample(changed, 0))
            self.assertEqual(len(set(png(changed["frames"][0]).crop(box).getdata())), 1)
        finally:
            path.write_bytes(original)

    def test_daily_snuggle_completion_uses_engine_policy(self):
        scenario = {"pet": "BUBBLE", "form": "BABY AXO", "moment": "SNUGGLE", "frames": 1,
                    "minute": 540}
        first = self.service.render({**scenario, "completion": "first"})["engine"]
        repeat = self.service.render({**scenario, "completion": "repeat"})["engine"]
        self.assertEqual(first["moment"], 0)
        self.assertEqual(repeat["moment"], 0)
        self.assertGreater(first["completed_moments"], 0)
        self.assertEqual(first["bond"], repeat["bond"])
        self.assertEqual(first["offered"], 10)
        # A second eight seconds decays needs, but pays no second 30-point energy cost.
        self.assertLess(abs(first["needs"][1] - repeat["needs"][1]), 10)

    def test_completion_rewards_with_simulation_and_live_frames(self):
        scenario = {"pet": "BUBBLE", "moment": "SNUGGLE", "minute": 540}
        advanced = self.service.render({**scenario, "frames": 1, "advance_s": 8})["engine"]
        live = self.service.render({**scenario, "frames": 2, "step_ms": 8000, "live": True})["engine"]
        self.assertEqual(advanced["offered"], 10)
        self.assertEqual(live["offered"], 10)
        self.assertEqual(advanced["bond"], live["bond"])
        self.assertEqual(advanced["moment"], 0)
        self.assertEqual(live["moment"], 0)

    def test_every_run_policy_rewards_repeated_completion(self):
        path = self.content / "activities.json"
        original = path.read_text()
        doc = json.loads(original)
        recipe = next(m for m in doc["moments"] if m["name"] == "SNUGGLE")
        recipe["daily_reward"] = False
        recipe["jitter_pct"] = 0
        path.write_text(json.dumps(doc))
        scenario = {"pet": "BUBBLE", "moment": "SNUGGLE", "frames": 1, "minute": 540}
        try:
            first = self.service.render({**scenario, "completion": "first"})["engine"]
            repeat = self.service.render({**scenario, "completion": "repeat"})["engine"]
            initial = self.service.render({"pet": "BUBBLE", "frames": 1})["engine"]["bond"]
            self.assertEqual(repeat["bond"] - initial, (first["bond"] - initial) * 2)
            self.assertGreater(first["needs"][1] - repeat["needs"][1], 20)
        finally:
            path.write_text(original)

    def test_2_working_copy_art_reaches_the_render(self):
        scenario = {"frames": 1}
        before = png(self.service.render(scenario)["frames"][0]).tobytes()
        manifest = json.loads((self.source / "assets.json").read_text())
        paths = {a["key"]: a["path"] for a in manifest["assets"]}
        idle = next(c for c in manifest["clips"] if c["key"] == "baby.idle")["frames"][0]
        shutil.copy(self.source / paths["creatures.baby-happy"], self.source / paths[idle])
        happy = next(a for a in manifest["assets"] if a["key"] == "creatures.baby-happy")
        next(a for a in manifest["assets"] if a["key"] == idle)["bounds"] = happy["bounds"]  # as a studio save records it
        (self.source / "assets.json").write_text(json.dumps(manifest, indent=2) + "\n")
        result = self.service.render(scenario)
        self.assertTrue(result["rebuilt"])
        self.assertNotEqual(png(result["frames"][0]).tobytes(), before)

    def test_3_content_errors_come_from_the_real_generators(self):
        path = self.content / "behaviors.json"
        good = path.read_text()
        doc = json.loads(good)
        doc["states"][0]["duration_s"] = [0, 5]
        path.write_text(json.dumps(doc))
        try:
            with self.assertRaises(gp.BuildFailed) as caught:
                self.service.render({})
            self.assertEqual(caught.exception.stage, "content")
            self.assertIn("minimum duration", caught.exception.messages[0])
            with self.assertRaises(gp.BuildFailed):  # unchanged input: the cached failure, no rebuild
                self.service.render({})
        finally:
            path.write_text(good)
        self.assertTrue(self.service.render({"frames": 1})["ok"])

    def test_4_the_tool_bounds_its_own_input(self):
        binary, _ = self.service.ensure_built()
        with tempfile.TemporaryDirectory() as out:
            for bad in (["--frames", "999"], ["--needs", "1,2,3"], ["--entry", "0"], ["--form", "x"]):
                code, _ = gp.run_bounded([binary, "--out", out, *bad], out, 10)
                self.assertEqual(code, 2, bad)
            code, text = gp.run_bounded([binary, "--out", out, "--entry", "3", "--form", "0"], out, 10)
            self.assertEqual(code, 3)
            self.assertIn("evolution set", text)
            code, text = gp.run_bounded([binary, "--out", out, "--moment", "1"], out, 10)
            self.assertEqual(json.loads(text)["activity"], "eating")

if __name__ == "__main__":
    unittest.main()

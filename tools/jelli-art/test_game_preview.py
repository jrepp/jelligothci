# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Jelli Art Test in game: scenario validation, build output, the endpoint, and a real render.

The end-to-end tests build tools/game-preview/preview.c against the real engine
from scratch copies of assets/slice and content/; they skip without cmake and a
C compiler. Run: ./scripts/uv run --python 3.12 tools/jelli-art/test_game_preview.py
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
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest import mock

from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
import game_preview as gp  # noqa: E402
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
        self.assertEqual([f["name"] for f in bubble["forms"]], ["AXOLOTL"])
        self.assertIn("asking_potty", CAT["states"])

    def test_defaults_render_the_starting_pet(self):
        argv, summary = gp.parse_scenario({}, CAT)
        args = dict(zip(argv[::2], argv[1::2]))
        self.assertEqual((args["--entry"], args["--form"], args["--page"]), ("1", "0", "0"))
        self.assertEqual((args["--behavior"], args["--moment"], args["--frames"]), ("0", "0", "12"))
        self.assertEqual(summary["pet"], "JELLI")

    def test_names_map_to_engine_numbers(self):
        raw = {"pet": "bubble", "page": "care", "menu": True, "behavior": "asking_potty", "moment": "tea",
               "needs": {"satiety": 100}, "potty": 900, "mess": True, "minute": 1300, "frames": 3}
        argv, summary = gp.parse_scenario(raw, CAT)
        args = dict(zip(argv[::2], argv[1::2]))
        self.assertEqual(args["--entry"], "3")
        self.assertEqual(args["--form"], "2")
        self.assertEqual(args["--behavior"], str(CAT["states"].index("asking_potty") + 1))
        self.assertEqual(args["--moment"], str(CAT["moments"].index("TEA") + 1))
        self.assertEqual(args["--needs"], "100,700,700,700,700")
        self.assertEqual((args["--menu"], args["--mess"], args["--asleep"]), ("1", "1", "0"))
        self.assertEqual(summary["form"], "AXOLOTL")

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

    def test_alt_text_describes_the_scenario(self):
        _, summary = gp.parse_scenario({"pet": "BUBBLE", "behavior": "studying", "mess": True, "asleep": True,
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
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), jelli_art.Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/api/game-preview"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        gp.SERVICE = self.saved

    def post(self, body):
        request = urllib.request.Request(self.url, data=json.dumps(body).encode(), method="POST")
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
        result = self.service.render({"pet": "BUBBLE", "behavior": "studying", "mess": True, "potty": 800,
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
